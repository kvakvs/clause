#include "../../terms/identities.hpp"
#include "crypto.hpp"
#include <array>
#include <cstring>
#include <openssl/evp.h>
#include <terms.hpp>
#include <type_traits>

// Digests of the crypto builtins (plan 21 step LIB79): hash/2 and the hash_init/update/final states, as OTP's NIF
// computes them. A state is a handle reference holding an EVP_MD_CTX; hash_update/2 returns a new state over a copy,
// so earlier states stay usable, as OTP's do. crypto.erl splits long inputs into updates.
namespace clause::runtime::builtins::crypto {
namespace {
// The state of a hash handle, kept in its buffer; trivially copyable, so it starts as the buffer's bytes.
struct HashState {
    // Tells a hash state apart from other handles.
    std::uint64_t magic;
    EVP_MD_CTX *context;
    // The output length of hash_final/1 for an extendable-output digest; 0 for the others.
    unsigned xof_length;
};

static_assert(std::is_trivially_copyable_v<HashState>);

constexpr std::uint64_t HASH_MAGIC = 0x6372797074686173; // "crypthas"

// Free the digest context of a hash state whose last reference died.
void finalize(std::span<std::byte> bytes) noexcept {
    HashState state{};
    std::memcpy(&state, bytes.data(), sizeof(state));
    EVP_MD_CTX_free(state.context);
}

// The hash state a handle holds; badarg "Bad state" for anything else.
HashState state_of(ProcessContext &context, const Term &handle) {
    const auto buffer = detail::HandleAccess::buffer(handle);
    HashState state{};
    if (buffer && (*buffer)->size() == sizeof(HashState)) {
        std::memcpy(&state, (*buffer)->data(), sizeof(state));
    }
    if (state.magic != HASH_MAGIC) {
        reject(context, Fault::badarg, {0, "Bad state"});
    }
    return state;
}

// A new hash handle owning `digest_context`.
TermResult<Term> make_state(ProcessContext &context, EVP_MD_CTX *digest_context, unsigned xof_length) {
    const HashState state{HASH_MAGIC, digest_context, xof_length};
    std::array<std::byte, sizeof(HashState)> bytes{};
    std::memcpy(bytes.data(), &state, sizeof(state));
    auto handle = detail::HandleAccess::make(context.heap(), bytes, finalize);
    if (!handle) {
        EVP_MD_CTX_free(digest_context);
    }
    return handle;
}

// The digest `type` names and the libcrypto has; badarg or notsup as OTP raises them.
const Digest &digest_of(ProcessContext &context, const Term &type, std::string_view unsupported) {
    const auto *digest = type.is_atom() ? find_digest(type.atom_spelling_unchecked()) : nullptr;
    if (!digest) {
        reject(context, Fault::badarg, {0, "Bad digest type"});
    }
    if (!digest->md) {
        reject(context, Fault::notsup, {0, unsupported});
    }
    return *digest;
}

// A digest context started for `digest`, with OTP's default output length for SHAKE; null when OpenSSL fails.
EVP_MD_CTX *started(const Digest &digest) {
    auto *digest_context = EVP_MD_CTX_new();
    auto length = digest.xof_length;
    std::array params{OSSL_PARAM_construct_uint("xoflen", &length), OSSL_PARAM_construct_end()};
    if (digest_context &&
        EVP_DigestInit_ex2(digest_context, digest.md, digest.xof_length ? params.data() : nullptr) != 1) {
        EVP_MD_CTX_free(digest_context);
        return nullptr;
    }
    return digest_context;
}

// How long a finished digest is: the digest's own size, or a length of an extendable-output digest.
struct Output {
    bool xof;
    unsigned length;
};

// The digest of a context's input so far, none when OpenSSL fails; the context is finished, so callers pass a copy.
std::optional<std::vector<std::byte>> finish(EVP_MD_CTX *digest_context, Output output) {
    if (output.xof) {
        std::vector<std::byte> bytes(output.length);
        return EVP_DigestFinalXOF(digest_context, reinterpret_cast<unsigned char *>(bytes.data()), output.length) == 1
                   ? std::optional(std::move(bytes))
                   : std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(EVP_MD_CTX_get_size(digest_context)));
    unsigned size = 0;
    if (EVP_DigestFinal_ex(digest_context, reinterpret_cast<unsigned char *>(bytes.data()), &size) != 1) {
        return std::nullopt;
    }
    bytes.resize(size);
    return bytes;
}

// The bytes of iodata argument `argument`; badarg "Not iolist" otherwise.
std::vector<std::byte> data_of(ProcessContext &context, const Term &data, int argument) {
    auto bytes = iodata(data);
    if (!bytes) {
        reject(context, Fault::badarg, {argument, "Not iolist"});
    }
    return std::move(*bytes);
}

// hash(Type, Data): the digest of Data.
TermResult<Term> hash(ProcessContext &context, const Named<struct HashType> &type, const Named<struct HashData> &data) {
    const auto &digest = digest_of(context, type.term, "Digest type not supported in this cryptolib");
    const auto bytes = data_of(context, data.term, 1);
    auto *digest_context = started(digest);
    if (!digest_context) {
        reject(context, Fault::error, {-1, "EVP_DigestInit failed"});
    }
    const bool updated = EVP_DigestUpdate(digest_context, bytes.data(), bytes.size()) == 1;
    const auto output = updated ? finish(digest_context, {digest.xof_length != 0, digest.xof_length}) : std::nullopt;
    EVP_MD_CTX_free(digest_context);
    if (!output) {
        reject(context, Fault::error, {-1, "Low-level call failed"});
    }
    return TermFactory(context).binary(*output);
}

// hash_init(Type): a new hash state.
TermResult<Term> hash_init(ProcessContext &context, const Term &type) {
    const auto &digest = digest_of(context, type, "Unsupported digest type");
    auto *digest_context = started(digest);
    if (!digest_context) {
        reject(context, Fault::error, {-1, "Low-level call EVP_DigestInit failed"});
    }
    return make_state(context, digest_context, digest.xof_length);
}

// A copy of a state's digest context; null when OpenSSL fails.
EVP_MD_CTX *copied(const HashState &state) {
    auto *copy = EVP_MD_CTX_new();
    if (copy && EVP_MD_CTX_copy_ex(copy, state.context) != 1) {
        EVP_MD_CTX_free(copy);
        return nullptr;
    }
    return copy;
}

// hash_update(State, Data): a new state that has also seen Data.
TermResult<Term> hash_update(ProcessContext &context, const Named<struct HashHandle> &handle,
                             const Named<struct HashData> &data) {
    const auto state = state_of(context, handle.term);
    const auto bytes = data_of(context, data.term, 1);
    auto *copy = copied(state);
    if (!copy) {
        reject(context, Fault::error, {-1, "Low-level call EVP_MD_CTX_copy failed"});
    }
    if (EVP_DigestUpdate(copy, bytes.data(), bytes.size()) != 1) {
        EVP_MD_CTX_free(copy);
        reject(context, Fault::error, {-1, "Low-level call EVP_DigestUpdate failed"});
    }
    return make_state(context, copy, state.xof_length);
}

// The digest of a state's input; the state stays usable.
TermResult<Term> final_digest(ProcessContext &context, const HashState &state, Output length) {
    auto *copy = copied(state);
    if (!copy) {
        reject(context, Fault::error, {-1, "Low-level call EVP_MD_CTX_copy failed"});
    }
    const auto output = finish(copy, length);
    EVP_MD_CTX_free(copy);
    if (!output) {
        reject(context, Fault::error, {-1, "Low-level call EVP_DigestFinal failed"});
    }
    return TermFactory(context).binary(*output);
}

// hash_final(State): the digest of everything the state has seen.
TermResult<Term> hash_final(ProcessContext &context, const Term &handle) {
    const auto state = state_of(context, handle);
    return final_digest(context, state, {state.xof_length != 0, state.xof_length});
}

// hash_final_xof(State, Length): Length bits (whole bytes) of an extendable-output digest.
TermResult<Term> hash_final_xof(ProcessContext &context, const Named<struct HashHandle> &handle,
                                const Named<struct HashLength> &length) {
    const auto state = state_of(context, handle.term);
    const auto bits = small(length.term.word());
    if (!bits || *bits < 0 || *bits > 0xFFFFFFFF) {
        reject(context, Fault::badarg, {1, "Bad len"});
    }
    return final_digest(context, state, {true, static_cast<unsigned>(*bits >> 3)});
}

constexpr std::array HASH_BUILTINS{
    typed_entry<hash>("clause_crypto", "hash"),
    typed_entry<hash_init>("clause_crypto", "hash_init"),
    typed_entry<hash_update>("clause_crypto", "hash_update"),
    typed_entry<hash_final>("clause_crypto", "hash_final"),
    typed_entry<hash_final_xof>("clause_crypto", "hash_final_xof"),
};
} // namespace

std::span<const BuiltinEntry> hash_builtins() noexcept { return HASH_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
