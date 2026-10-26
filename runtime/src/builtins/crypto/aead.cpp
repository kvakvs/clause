#include "crypto.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <initializer_list>
#include <mutex>
#include <openssl/evp.h>
#include <terms.hpp>

// AEAD ciphers of the crypto builtins (plan 21 step LIB79B): crypto_one_time_aead/6,7 and the
// crypto_one_time_aead_init/4 states that crypto_one_time_aead/4 encrypts or decrypts with, as OTP's aead.c drives
// EVP_CIPHER_CTX for GCM, CCM and ChaCha20-Poly1305: its checks in its order and its errors. Decryption with a wrong
// tag returns the atom error.
namespace clause::runtime::builtins::crypto {
namespace {
// A crypto_one_time_aead_init/4 state: OTP's aead_cipher_ctx.
struct AeadState {
    // Serializes the processes that share the context, as OTP's mutex does.
    std::mutex lock;
    const Cipher *cipher = nullptr;
    CipherContext context;
    std::vector<std::byte> key;
    // 1 encrypts, 0 decrypts.
    int encrypt = 1;
    unsigned tag_length = 0;
};

constexpr std::uint64_t AEAD_MAGIC = 0x6372797074616561; // "cryptaea"

// One AEAD operation on a context that has its cipher and direction.
struct AeadCall {
    const Cipher &cipher;
    EVP_CIPHER_CTX *context;
    std::span<const std::byte> key;
    std::span<const std::byte> iv;
    std::span<const std::byte> text;
    std::span<const std::byte> aad;
    // 0 decrypts; 1 (or OTP's compatibility -1) encrypts.
    int encrypt;
    unsigned tag_length;
    // The tag a decryption checks (empty when encrypting).
    std::vector<std::byte> tag;
    // The state form: the tag follows the encrypted text instead of coming apart.
    bool appended;
};

// A non-negative integer that fits OTP's unsigned int; none for anything else (enif_get_uint).
std::optional<unsigned> unsigned_of(const Term &term) {
    const auto value = small(term.word());
    if (!value || *value < 0 || *value > UINT_MAX) {
        return std::nullopt;
    }
    return static_cast<unsigned>(*value);
}

// The encrypt flag of the one-time form: true, false or OTP's compatibility value undefined.
int encrypt_flag(ProcessContext &context, const Term &flag, int argument, bool compatible) {
    const auto name = flag.is_atom() ? flag.atom_spelling_unchecked() : std::string_view{};
    if (name == "true") {
        return 1;
    }
    if (name == "false") {
        return 0;
    }
    if (compatible && name == "undefined") {
        return -1;
    }
    reject(context, Fault::badarg, {argument, "Bad enc flag"});
}

// The AEAD cipher an atom names for a key length, checked as OTP's aead.c checks it.
const Cipher &aead_cipher_of(ProcessContext &context, const Term &type, std::size_t key_length) {
    const auto *cipher = find_cipher(type.atom_spelling_unchecked(), key_length);
    if (!cipher) {
        reject(context, Fault::badarg, {0, "Unknown cipher or invalid key size"});
    }
    if (cipher->kind == CipherKind::plain) {
        reject(context, Fault::badarg, {0, "Not aead cipher"});
    }
    if (!cipher->cipher) {
        reject(context, Fault::notsup, {0, "The cipher is not supported in this libcrypto version"});
    }
    return *cipher;
}

// A context of an AEAD cipher for the direction `encrypt`, sized for the key; its key and IV come per operation.
CipherContext aead_context(ProcessContext &context, const Cipher &cipher, std::span<const std::byte> key, int encrypt) {
    CipherContext cipher_context(EVP_CIPHER_CTX_new());
    if (!cipher_context) {
        reject(context, Fault::error, {-1, "Can't allocate ctx"});
    }
    if (EVP_CipherInit_ex(cipher_context.get(), cipher.cipher, nullptr, nullptr, nullptr, encrypt) != 1) {
        reject(context, Fault::error, {-1, "CipherInit failed"});
    }
    if (EVP_CIPHER_CTX_set_key_length(cipher_context.get(), static_cast<int>(key.size())) != 1) {
        reject(context, Fault::badarg, {1, "Bad Key length"});
    }
    return cipher_context;
}

// Give a CCM context its tag (length), key, IV and text length, which CCM takes before the text.
void prepare_ccm(ProcessContext &context, AeadCall &call) {
    if (EVP_CIPHER_CTX_ctrl(call.context, EVP_CTRL_AEAD_SET_TAG, static_cast<int>(call.tag_length),
                            call.tag.empty() ? nullptr : call.tag.data()) != 1) {
        reject(context, Fault::badarg, {5, "Can't set tag"});
    }
    if (EVP_CipherInit_ex(call.context, nullptr, nullptr, bytes_address(call.key), bytes_address(call.iv), -1) != 1) {
        reject(context, Fault::error, {-1, "Can't set key or iv"});
    }
    int length = 0;
    if (EVP_CipherUpdate(call.context, nullptr, &length, nullptr, static_cast<int>(call.text.size())) != 1) {
        reject(context, Fault::error, {-1, "Can't set text size"});
    }
}

// Give the context its IV length, key, IV and AAD.
void prepare(ProcessContext &context, AeadCall &call) {
    if (EVP_CIPHER_CTX_ctrl(call.context, EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(call.iv.size()), nullptr) != 1) {
        reject(context, Fault::badarg, {2, "Bad IV length"});
    }
    int length = 0;
    if (call.cipher.kind == CipherKind::ccm) {
        prepare_ccm(context, call);
    } else if (EVP_CipherInit_ex(call.context, nullptr, nullptr, bytes_address(call.key), bytes_address(call.iv), -1) !=
               1) {
        reject(context, Fault::error, {-1, "Can't set key and iv"});
    }
    if (EVP_CipherUpdate(call.context, nullptr, &length, bytes_address(call.aad), static_cast<int>(call.aad.size())) !=
        1) {
        reject(context, Fault::badarg, {4, "Can't set AAD"});
    }
}

// Finish an encryption: {CipherText, Tag}, or the text with the tag after it for the state form.
TermResult<Term> encrypted(ProcessContext &context, const AeadCall &call, std::vector<std::byte> output) {
    std::array<unsigned char, EVP_MAX_BLOCK_LENGTH> rest{};
    int length = 0;
    if (EVP_CipherFinal_ex(call.context, rest.data(), &length) != 1) {
        reject(context, Fault::error, {-1, "Encrypt error"});
    }
    std::vector<std::byte> tag(call.tag_length);
    auto *tag_address = call.appended ? output.data() + call.text.size() : tag.data();
    if (EVP_CIPHER_CTX_ctrl(call.context, EVP_CTRL_AEAD_GET_TAG, static_cast<int>(call.tag_length), tag_address) != 1) {
        reject(context, Fault::error, {-1, "Can't get Tag"});
    }
    output.resize(call.text.size() + (call.appended ? call.tag_length : 0));
    TermFactory factory(context);
    if (call.appended) {
        return factory.binary(output);
    }
    const std::array parts{need(factory.binary(output)), need(factory.binary(tag))};
    return factory.tuple(parts);
}

// Finish a decryption: the plain text, or error when the tag does not match (CCM checked it in the update).
TermResult<Term> decrypted(ProcessContext &context, AeadCall &call, std::vector<std::byte> output, int length) {
    if (call.cipher.kind != CipherKind::ccm) {
        auto *tag_address = call.tag.empty() ? nullptr : call.tag.data();
        if (EVP_CIPHER_CTX_ctrl(call.context, EVP_CTRL_AEAD_SET_TAG, static_cast<int>(call.tag_length), tag_address) !=
                1 ||
            EVP_DecryptFinal_ex(call.context, reinterpret_cast<unsigned char *>(output.data()) + length, &length) !=
                1) {
            return atom(context, "error"_atom);
        }
    }
    output.resize(call.text.size());
    return TermFactory(context).binary(output);
}

// Encrypt or decrypt the text of a prepared call (the common part of OTP's aead_cipher_nif).
TermResult<Term> run(ProcessContext &context, AeadCall &call) {
    prepare(context, call);
    const auto size = call.text.size() + (call.encrypt != 0 && call.appended ? call.tag_length : 0);
    // One byte more, so that OpenSSL never sees a null output for an empty text.
    std::vector<std::byte> output(size + EVP_MAX_BLOCK_LENGTH + 1);
    int length = 0;
    if (EVP_CipherUpdate(call.context, reinterpret_cast<unsigned char *>(output.data()), &length,
                         bytes_address(call.text), static_cast<int>(call.text.size())) != 1) {
        if (call.encrypt != 0) {
            reject(context, Fault::badarg, {3, "Can't set in-text"});
        }
        return atom(context, "error"_atom);
    }
    if (call.encrypt != 0) {
        return encrypted(context, call, std::move(output));
    }
    return decrypted(context, call, std::move(output), length);
}

// The tag argument of the one-time form: the tag length when encrypting, the tag itself when decrypting.
struct TagArgument {
    unsigned length = 0;
    std::vector<std::byte> tag;
};

TagArgument tag_argument(ProcessContext &context, const Term &term, int encrypt) {
    if (encrypt != 0) {
        const auto length = unsigned_of(term);
        if (!length) {
            reject(context, Fault::badarg, {5, "Bad Tag length"});
        }
        return {*length, {}};
    }
    auto tag = iodata_argument(context, term, {5, "non-binary Tag"});
    const auto length = static_cast<unsigned>(tag.size());
    return {length, std::move(tag)};
}

// Whether any part of a one-time call is past OTP's int sizes.
bool too_long(const TagArgument &tag, std::initializer_list<std::size_t> sizes) {
    return tag.length > INT_MAX || std::ranges::any_of(sizes, [](std::size_t size) { return size > INT_MAX; });
}

// aead_cipher(Type, Key, IV, Text, AAD, TagOrTagLength, EncFlag): crypto_one_time_aead/6,7.
TermResult<Term> aead_cipher(ProcessContext &context, const Named<struct AeadType> &type,
                             const Named<struct AeadKey> &key_data, const Named<struct AeadIv> &iv_data,
                             const Named<struct AeadText> &text_data, const Named<struct AeadAad> &aad_data,
                             const Named<struct AeadTag> &tag, const Named<struct AeadFlag> &flag) {
    const auto encrypt = encrypt_flag(context, flag.term, 6, true);
    if (!type.term.is_atom()) {
        reject(context, Fault::badarg, {0, "non-atom cipher type"});
    }
    const auto key = iodata_argument(context, key_data.term, {1, "non-binary key"});
    const auto iv = iodata_argument(context, iv_data.term, {2, "non-binary iv"});
    const auto text = iodata_argument(context, text_data.term, {3, "non-binary text"});
    const auto aad = iodata_argument(context, aad_data.term, {4, "non-binary AAD"});
    auto tag_data = tag_argument(context, tag.term, encrypt);
    if (too_long(tag_data, {key.size(), iv.size(), text.size(), aad.size()})) {
        reject(context, Fault::badarg, {5, "binary too long"});
    }
    const auto &cipher = aead_cipher_of(context, type.term, key.size());
    const auto cipher_context = aead_context(context, cipher, key, encrypt);
    AeadCall call{cipher,          cipher_context.get(),    key,  iv, text, aad, encrypt,
                  tag_data.length, std::move(tag_data.tag), false};
    return run(context, call);
}

// aead_cipher_init(Type, Key, TagLength, EncFlag): crypto_one_time_aead_init/4's state.
TermResult<Term> aead_cipher_init(ProcessContext &context, const Named<struct AeadType> &type,
                                  const Named<struct AeadKey> &key, const Named<struct AeadTag> &tag_length,
                                  const Named<struct AeadFlag> &flag) {
    auto state = std::make_unique<AeadState>();
    state->encrypt = encrypt_flag(context, flag.term, 3, false);
    if (!type.term.is_atom()) {
        reject(context, Fault::badarg, {0, "non-atom cipher type"});
    }
    auto bytes = key.term.is_binary() ? key.term.binary_bytes() : TermResult<std::vector<std::byte>>{};
    if (!key.term.is_binary() || !bytes) {
        reject(context, Fault::badarg, {1, "non-binary key"});
    }
    state->key = std::move(*bytes);
    const auto length = unsigned_of(tag_length.term);
    if (!length) {
        reject(context, Fault::badarg, {2, "Bad Tag length"});
    }
    state->tag_length = *length;
    if (state->tag_length > INT_MAX || state->key.size() > INT_MAX) {
        reject(context, Fault::badarg, {1, "key or tag too long"});
    }
    state->cipher = &aead_cipher_of(context, type.term, state->key.size());
    state->context = aead_context(context, *state->cipher, state->key, state->encrypt);
    return owned_handle(context, AEAD_MAGIC, std::move(state));
}

// aead_cipher(State, IV, Text, AAD): crypto_one_time_aead/4; a decrypted Text ends with its tag.
TermResult<Term> aead_cipher_with(ProcessContext &context, const Named<struct AeadHandle> &handle,
                                  const Named<struct AeadIv> &iv_data, const Named<struct AeadText> &text_data,
                                  const Named<struct AeadAad> &aad_data) {
    auto *state = owned_object<AeadState>(handle.term, AEAD_MAGIC);
    if (!state) {
        reject(context, Fault::badarg, {0, "Bad State"});
    }
    std::scoped_lock lock(state->lock);
    const auto iv = iodata_argument(context, iv_data.term, {1, "non-binary iv"});
    const auto text = iodata_argument(context, text_data.term, {2, "non-binary text"});
    const auto aad = iodata_argument(context, aad_data.term, {3, "non-binary AAD"});
    std::span<const std::byte> input(text);
    std::vector<std::byte> tag;
    if (state->encrypt == 0) {
        if (input.size() < state->tag_length) {
            reject(context, Fault::error, {-1, "Bad in data"});
        }
        const auto tag_bytes = input.last(state->tag_length);
        tag.assign(tag_bytes.begin(), tag_bytes.end());
        input = input.first(input.size() - state->tag_length);
    }
    AeadCall call{*state->cipher, state->context.get(), state->key,     iv,  input, aad,
                  state->encrypt, state->tag_length,    std::move(tag), true};
    return run(context, call);
}

constexpr std::array AEAD_BUILTINS{
    typed_entry<aead_cipher>("clause_crypto", "aead_cipher"),
    typed_entry<aead_cipher_init>("clause_crypto", "aead_cipher_init"),
    typed_entry<aead_cipher_with>("clause_crypto", "aead_cipher"),
};
} // namespace

std::span<const BuiltinEntry> aead_builtins() noexcept { return AEAD_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
