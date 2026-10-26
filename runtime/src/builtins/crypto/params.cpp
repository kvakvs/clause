#include "pkey.hpp"
#include <algorithm>
#include <cstring>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <terms.hpp>

// Owned keys and OSSL_PARAM lists of crypto's public key builtins (plan 21 step LIB79C), as OTP's bn.c builds them.
namespace clause::runtime::builtins::crypto {
void PkeyDeleter::operator()(EVP_PKEY *key) const noexcept { EVP_PKEY_free(key); }

void PkeyContextDeleter::operator()(EVP_PKEY_CTX *context) const noexcept { EVP_PKEY_CTX_free(context); }

bool ParamList::add_number(const char *key, const Term &binary) {
    if (!binary.is_binary()) {
        return false;
    }
    const auto number = bignum_of(need(binary.binary_bytes()));
    if (!number) {
        return false;
    }
    add_bignum(key, number.get());
    return true;
}

void ParamList::add_bignum(const char *key, const BIGNUM *number) {
    // BN_bn2nativepad: OSSL_PARAM big numbers are in native byte order, as OTP's get_ossl_BN_param_from_bn gives them.
    // Zero has no bytes, but OpenSSL refuses a parameter without data: its buffer holds one unused byte.
    const auto size = static_cast<std::size_t>(BN_num_bytes(number));
    auto &bytes = buffers_.emplace_back(std::max<std::size_t>(size, 1));
    BN_bn2nativepad(number, bytes.data(), static_cast<int>(size));
    params_.push_back(OSSL_PARAM_construct_BN(key, bytes.data(), size));
}

bool ParamList::add_number_from_list(const char *key, Term &list) {
    if (!list.is_cons()) {
        return false;
    }
    const auto head = list.head_unchecked();
    list = list.tail_unchecked();
    return add_number(key, head);
}

bool ParamList::add_octets(const char *key, const Term &binary) {
    if (!binary.is_binary()) {
        return false;
    }
    const auto bytes = need(binary.binary_bytes());
    auto &stored = buffers_.emplace_back(reinterpret_cast<const unsigned char *>(bytes.data()),
                                         reinterpret_cast<const unsigned char *>(bytes.data()) + bytes.size());
    params_.push_back(OSSL_PARAM_construct_octet_string(key, stored.data(), stored.size()));
    return true;
}

void ParamList::add_utf8(const char *key, std::string_view text) {
    auto &stored = buffers_.emplace_back(text.begin(), text.end());
    stored.push_back(0);
    params_.push_back(OSSL_PARAM_construct_utf8_string(key, reinterpret_cast<char *>(stored.data()), 0));
}

void ParamList::add_uint64(const char *key, std::uint64_t value) {
    auto &stored = buffers_.emplace_back(sizeof(value));
    std::memcpy(stored.data(), &value, sizeof(value));
    params_.push_back(OSSL_PARAM_construct_uint64(key, reinterpret_cast<std::uint64_t *>(stored.data())));
}

bool ParamList::replace_number(std::size_t index, const char *key, const Term &binary) {
    ParamList single;
    if (!single.add_number(key, binary)) {
        return false;
    }
    // Moving the buffer keeps its bytes where the entry points.
    buffers_.push_back(std::move(single.buffers_.front()));
    params_[index] = single.params_.front();
    return true;
}

void ParamList::truncate(std::size_t count) { params_.resize(count); }

OSSL_PARAM *ParamList::ended() {
    ended_ = params_;
    ended_.push_back(OSSL_PARAM_construct_end());
    return ended_.data();
}

Pkey key_from_params(const char *type, int selection, ParamList &params) {
    const PkeyContext context(EVP_PKEY_CTX_new_from_name(nullptr, type, nullptr));
    EVP_PKEY *key = nullptr;
    if (!context || EVP_PKEY_fromdata_init(context.get()) <= 0 ||
        EVP_PKEY_fromdata(context.get(), &key, selection, params.ended()) <= 0) {
        return nullptr;
    }
    return Pkey(key);
}

std::optional<std::vector<std::byte>> key_number(const EVP_PKEY *key, const char *name) {
    BIGNUM *number = nullptr;
    if (EVP_PKEY_get_bn_param(key, name, &number) != 1) {
        return std::nullopt;
    }
    const Bignum owned(number);
    return bytes_of(owned.get());
}

void raise_failure(ProcessContext &context, const KeyFailure &failure) {
    reject(context, failure.fault, failure.rejection, failure.location);
}

std::optional<std::int64_t> integer_of(const Term &term) {
    if (!term.is_integer()) {
        return std::nullopt;
    }
    const auto value = term.integer_value();
    return value ? std::optional(*value) : std::nullopt;
}

bool is_atom_named(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}
} // namespace clause::runtime::builtins::crypto
