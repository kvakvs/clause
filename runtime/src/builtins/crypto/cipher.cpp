#include "../io_lib/pretty.hpp"
#include "../text.hpp"
#include "crypto.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <string>
#include <terms.hpp>

// Ciphers of the crypto builtins (plan 21 step LIB79B): cipher_info/1, the crypto_init/4 states that
// crypto_update/2, crypto_final/1 and crypto_get_data/1 work on, and crypto_one_time/5, as OTP's api_ng.c drives
// EVP_CIPHER_CTX: its checks in its order, its padding and its errors. A state is a handle reference owning the
// context, which updates advance in place.
namespace clause::runtime::builtins::crypto {
namespace {
// OTP's padding options: undefined leaves a partial last block out, none requires whole blocks, zero and random
// fill the last block, pkcs_padding is OpenSSL's own.
enum class Padding : std::uint8_t { undefined, none, zero, random, pkcs_padding };

constexpr std::array<std::pair<std::string_view, Padding>, 5> PADDINGS{{
    {"undefined", Padding::undefined},
    {"none", Padding::none},
    {"zero", Padding::zero},
    {"random", Padding::random},
    {"pkcs_padding", Padding::pkcs_padding},
}};

// A crypto_init/4 state: OTP's evp_cipher_ctx.
struct CipherState {
    // Serializes the processes that share the state.
    std::mutex lock;
    CipherContext context;
    Padding padding = Padding::undefined;
    // The padding crypto_final/1 added; -1 before it ran.
    int padded_size = -1;
    // 1 encrypts, 0 decrypts; -1 is OTP's compatibility value undefined, which finishes as encryption.
    int encrypt = 1;
    // The bytes the updates took, counted as OTP's unsigned int.
    unsigned size = 0;
};

constexpr std::uint64_t CIPHER_MAGIC = 0x6372797074636970; // "cryptcip"

// The encrypt flag and padding of crypto_init's FlagOrOptions.
struct Options {
    int encrypt = 1;
    Padding padding = Padding::undefined;
    bool encrypt_seen = false;
    bool padding_seen = false;
};

bool is_atom_named(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The text OTP's enif_snprintf("%T") gives an unknown option tag (printable lists as strings, on one line), cut to
// its 64-byte buffer.
std::string bad_tag_message(ProcessContext &context, const Term &tag) {
    io_lib::Deep deep;
    const auto printed = io_lib::pretty_print(context, deep, tag, {.line_length = 0});
    auto text = "Bad tag in option: " + utf8(deep.flat(printed));
    text.resize(std::min<std::size_t>(text.size(), 63));
    return text;
}

// Apply one {encrypt, Flag} option.
void encrypt_option(ProcessContext &context, const Term &value, int argument, Options &options) {
    if (options.encrypt_seen) {
        reject(context, Fault::badarg, {argument, "'encrypt' option is present more than once!"});
    }
    options.encrypt_seen = true;
    if (is_atom_named(value, "true")) {
        options.encrypt = 1;
    } else if (is_atom_named(value, "false")) {
        options.encrypt = 0;
    } else if (is_atom_named(value, "undefined")) {
        options.encrypt = -1;
    } else {
        reject(context, Fault::badarg, {argument, "The 'encrypt' option must be a boolean!"});
    }
}

// Apply one {padding, Padding} option.
void padding_option(ProcessContext &context, const Term &value, int argument, Options &options) {
    if (options.padding_seen) {
        reject(context, Fault::badarg, {argument, "The 'padding' option is present more than once!"});
    }
    options.padding_seen = true;
    const auto name = value.is_atom() ? value.atom_spelling_unchecked() : std::string_view{};
    const auto found = std::ranges::find(PADDINGS, name, &std::pair<std::string_view, Padding>::first);
    if (name.empty() || found == PADDINGS.end()) {
        reject(context, Fault::badarg, {argument, "Bad 'padding' option value"});
    }
    options.padding = found->second;
}

// Apply one element of a property list of options.
void apply_option(ProcessContext &context, const Term &element, int argument, Options &options) {
    if (!element.is_tuple() || element.tuple_size_unchecked() != 2) {
        reject(context, Fault::badarg, {argument, "Options must be a property list!"});
    }
    const auto tag = element.tuple_element_unchecked(0);
    const auto value = element.tuple_element_unchecked(1);
    if (is_atom_named(tag, "encrypt")) {
        encrypt_option(context, value, argument, options);
    } else if (is_atom_named(tag, "padding")) {
        padding_option(context, value, argument, options);
    } else {
        const auto message = bad_tag_message(context, tag);
        reject(context, Fault::badarg, {argument, message});
    }
}

// FlagOrOptions: true, false, undefined or a proper list of options (OTP's get_opts).
Options options_of(ProcessContext &context, const Term &term, int argument) {
    Options options;
    if (is_atom_named(term, "true") || is_atom_named(term, "false") || is_atom_named(term, "undefined")) {
        options.encrypt = is_atom_named(term, "true") ? 1 : is_atom_named(term, "false") ? 0 : -1;
        return options;
    }
    if (!term.is_list() || !need(term.is_proper_list())) {
        reject(context, Fault::badarg, {argument, "Options are not a boolean or a proper list"});
    }
    for (auto list = term; list.is_cons(); list = list.tail_unchecked()) {
        apply_option(context, list.head_unchecked(), argument, options);
    }
    return options;
}

// The streaming cipher an atom names for a key length, checked as OTP's get_init_args checks it.
const Cipher &plain_cipher(ProcessContext &context, const Term &type, std::size_t key_length) {
    if (!type.is_atom()) {
        reject(context, Fault::badarg, {0, "Cipher id is not an atom"});
    }
    const auto name = type.atom_spelling_unchecked();
    const auto *cipher = find_cipher(name, key_length);
    if (!cipher) {
        reject(context, Fault::badarg,
               find_cipher_named(name) ? Rejection{1, "Bad key size"} : Rejection{0, "Unknown cipher"});
    }
    if (cipher->kind != CipherKind::plain) {
        reject(context, Fault::badarg, {0, "Missing arguments for this cipher"});
    }
    if (forbidden_in_fips(*cipher)) {
        reject(context, Fault::notsup, {0, "Forbidden in FIPS"});
    }
    if (!active(*cipher)) {
        reject(context, Fault::notsup, {0, "Cipher not supported in this libcrypto version"});
    }
    return *cipher;
}

// The IV argument of a cipher: none for a cipher without one, else iodata of the cipher's IV length.
std::vector<std::byte> iv_of(ProcessContext &context, const Cipher &cipher, const Term &iv) {
    if (is_atom_named(iv, "undefined")) {
        reject(context, Fault::notsup, {-1, "Dynamic IV is not supported since OTP 27.0"});
    }
    const auto length = static_cast<std::size_t>(EVP_CIPHER_get_iv_length(active(cipher)));
    if (length == 0) {
        return {};
    }
    auto bytes = iodata_argument(context, iv, {2, "Bad iv type"});
    if (bytes.size() != length) {
        reject(context, Fault::badarg, {2, "Bad iv size"});
    }
    return bytes;
}

// Set RC2's effective key bits to the key's length, as OTP does for rc2_cbc.
void set_rc2_key_bits(ProcessContext &context, EVP_CIPHER_CTX *cipher_context, std::size_t key_length) {
    if (key_length > INT_MAX / 8) {
        reject(context, Fault::badarg, {1, "To large rc2_cbc key"});
    }
    if (EVP_CIPHER_CTX_ctrl(cipher_context, EVP_CTRL_SET_RC2_KEY_BITS, static_cast<int>(key_length) * 8, nullptr) !=
        1) {
        reject(context, Fault::badarg, {1, "ctrl rc2_cbc key"});
    }
}

// The key and IV a context is keyed with; the IV is empty for a cipher without one.
struct Keying {
    std::span<const std::byte> key;
    std::span<const std::byte> iv;
};

// A context of `cipher` keyed for the direction `encrypt`.
CipherContext started(ProcessContext &context, const Cipher &cipher, const Keying &keying, int encrypt) {
    const auto key = keying.key;
    const auto iv = keying.iv;
    CipherContext cipher_context(EVP_CIPHER_CTX_new());
    if (!cipher_context) {
        reject(context, Fault::error, {-1, "Can't allocate context"});
    }
    if (EVP_CipherInit_ex(cipher_context.get(), active(cipher), nullptr, nullptr, nullptr, encrypt) != 1) {
        reject(context, Fault::error, {-1, "Can't initialize context, step 1"});
    }
    if (EVP_CIPHER_CTX_set_key_length(cipher_context.get(), static_cast<int>(key.size())) != 1) {
        reject(context, Fault::error, {1, "Can't initialize context, key_length"});
    }
    if (EVP_CIPHER_get_type(active(cipher)) == NID_rc2_cbc) {
        set_rc2_key_bits(context, cipher_context.get(), key.size());
    }
    const auto *iv_address = iv.empty() ? nullptr : bytes_address(iv);
    if (EVP_CipherInit_ex(cipher_context.get(), nullptr, nullptr, bytes_address(key), iv_address, -1) != 1) {
        reject(context, iv.empty() ? Fault::badarg : Fault::error,
               iv.empty() ? Rejection{1, "Can't initialize key"} : Rejection{-1, "Can't initialize key or iv"});
    }
    return cipher_context;
}

// The arguments of a cipher initialization and the argument number of its options.
struct InitArguments {
    const Term &type;
    const Term &key;
    const Term &iv;
    const Term &options;
    int options_argument;
};

// Check crypto_init's arguments in OTP's order and set `state` up (OTP's get_init_args).
void initialize(ProcessContext &context, CipherState &state, const InitArguments &arguments) {
    const auto options = options_of(context, arguments.options, arguments.options_argument);
    state.encrypt = options.encrypt;
    state.padding = options.padding;
    const auto key = iodata_argument(context, arguments.key, {1, "Bad key"});
    const auto &cipher = plain_cipher(context, arguments.type, key.size());
    const auto iv = iv_of(context, cipher, arguments.iv);
    state.context = started(context, cipher, {key, iv}, state.encrypt);
    if (state.padding != Padding::pkcs_padding) {
        EVP_CIPHER_CTX_set_padding(state.context.get(), 0);
    }
}

// Run `data` through a state's context (OTP's get_update_args).
std::vector<std::byte> update(ProcessContext &context, CipherState &state, std::span<const std::byte> data) {
    state.size += static_cast<unsigned>(data.size());
    const auto block_size = static_cast<std::size_t>(EVP_CIPHER_CTX_get_block_size(state.context.get()));
    std::vector<std::byte> output(data.size() + block_size);
    int length = 0;
    if (EVP_CipherUpdate(state.context.get(), reinterpret_cast<unsigned char *>(output.data()), &length,
                         bytes_address(data), static_cast<int>(data.size())) != 1) {
        reject(context, Fault::error, {-1, "Can't update"});
    }
    output.resize(static_cast<std::size_t>(length));
    return output;
}

// Finish a context into `output` from `offset` on; false when OpenSSL rejects the last block.
bool finished(CipherState &state, std::vector<std::byte> &output, std::size_t offset) {
    int length = 0;
    if (EVP_CipherFinal_ex(state.context.get(), reinterpret_cast<unsigned char *>(output.data() + offset), &length) !=
        1) {
        return false;
    }
    output.resize(offset + static_cast<std::size_t>(length));
    return true;
}

// Fill the last block with zeros or random bytes through the context; the bytes it gives back.
std::size_t fill_last_block(ProcessContext &context, CipherState &state, std::vector<std::byte> &output, int pad_size) {
    if (pad_size == 0) {
        return 0;
    }
    std::array<unsigned char, EVP_MAX_BLOCK_LENGTH> padding{};
    if (state.padding == Padding::random) {
        RAND_bytes(padding.data(), pad_size);
    }
    int length = 0;
    if (EVP_CipherUpdate(state.context.get(), reinterpret_cast<unsigned char *>(output.data()), &length, padding.data(),
                         pad_size) != 1) {
        reject(context, Fault::error, {-1, "Can't pad"});
    }
    return static_cast<std::size_t>(length);
}

// The last bytes of an encryption, padded as the state's option says.
std::vector<std::byte> final_encryption(ProcessContext &context, CipherState &state, int block_size, int pad_size) {
    state.padded_size = state.padding == Padding::pkcs_padding && pad_size == 0 ? block_size : pad_size;
    if (state.padding == Padding::undefined) {
        return {};
    }
    std::vector<std::byte> output(2 * static_cast<std::size_t>(block_size));
    std::size_t offset = 0;
    std::string_view failure = "Padding failed";
    if (state.padding == Padding::none) {
        failure = "Padding 'none' but unfilled last block";
    } else if (state.padding == Padding::pkcs_padding) {
        failure = "Can't finalize";
    } else {
        offset = fill_last_block(context, state, output, pad_size);
    }
    if (!finished(state, output, offset)) {
        reject(context, Fault::error, {-1, failure});
    }
    return output;
}

// The last bytes of a state (OTP's get_final_args).
std::vector<std::byte> finish(ProcessContext &context, CipherState &state) {
    const auto block_size = EVP_CIPHER_CTX_get_block_size(state.context.get());
    auto pad_size = static_cast<int>(state.size % static_cast<unsigned>(block_size));
    if (pad_size != 0) {
        pad_size = block_size - pad_size;
    }
    if (state.encrypt != 0) {
        return final_encryption(context, state, block_size, pad_size);
    }
    if (state.padding == Padding::undefined) {
        return {};
    }
    std::vector<std::byte> output(static_cast<std::size_t>(block_size));
    if (!finished(state, output, 0)) {
        reject(context, Fault::error, {-1, "Can't finalize"});
    }
    return output;
}

// The state a handle holds; badarg "Bad State" for anything else.
CipherState &state_of(ProcessContext &context, const Term &handle) {
    auto *state = owned_object<CipherState>(handle, CIPHER_MAGIC);
    if (!state) {
        reject(context, Fault::badarg, {0, "Bad State"});
    }
    return *state;
}

// The data of an update or one-time call: iodata of at most INT_MAX bytes.
std::vector<std::byte> data_of(ProcessContext &context, const Term &data, Rejection rejection) {
    auto bytes = iodata_argument(context, data, rejection);
    if (bytes.size() > INT_MAX) {
        reject(context, Fault::badarg, {rejection.argument, "too long data"});
    }
    return bytes;
}

// The mode atoms of cipher_info/1 by OpenSSL's cipher modes; other modes are undefined.
constexpr std::array<std::pair<unsigned long, std::string_view>, 11> MODES{{
    {EVP_CIPH_ECB_MODE, "ecb_mode"},
    {EVP_CIPH_CBC_MODE, "cbc_mode"},
    {EVP_CIPH_CFB_MODE, "cfb_mode"},
    {EVP_CIPH_OFB_MODE, "ofb_mode"},
    {EVP_CIPH_CTR_MODE, "ctr_mode"},
    {EVP_CIPH_GCM_MODE, "gcm_mode"},
    {EVP_CIPH_CCM_MODE, "ccm_mode"},
    {EVP_CIPH_XTS_MODE, "xts_mode"},
    {EVP_CIPH_WRAP_MODE, "wrap_mode"},
    {EVP_CIPH_OCB_MODE, "ocb_mode"},
    {EVP_CIPH_STREAM_CIPHER, "stream_cipher"},
}};

std::string_view mode_name(const EVP_CIPHER *cipher) {
    const auto found =
        std::ranges::find(MODES, EVP_CIPHER_get_mode(cipher), &std::pair<unsigned long, std::string_view>::first);
    return found == MODES.end() ? std::string_view{"undefined"} : found->second;
}

// cipher_info(Type): the cipher's lengths, AEAD property and mode; badarg for an unknown name, notsup for a
// missing cipher (crypto.erl tries the aliases).
TermResult<Term> cipher_info(ProcessContext &context, const Term &type) {
    const auto *found = type.is_atom() ? find_cipher_named(type.atom_spelling_unchecked()) : nullptr;
    if (!found) {
        bad_argument();
    }
    const auto *cipher = active(*found);
    if (!cipher) {
        raise_notsup(context);
    }
    TermFactory factory(context);
    const auto nid = EVP_CIPHER_get_type(cipher);
    const bool aead = (EVP_CIPHER_get_flags(cipher) & EVP_CIPH_FLAG_AEAD_CIPHER) != 0;
    const std::array<std::pair<AtomName, Term>, 6> entries{
        std::pair<AtomName, Term>{"type"_atom,
                                  nid == NID_undef ? atom(context, "undefined"_atom) : need(factory.integer(nid))},
        std::pair<AtomName, Term>{"key_length"_atom, need(factory.integer(EVP_CIPHER_get_key_length(cipher)))},
        std::pair<AtomName, Term>{"iv_length"_atom, need(factory.integer(EVP_CIPHER_get_iv_length(cipher)))},
        std::pair<AtomName, Term>{"block_size"_atom, need(factory.integer(EVP_CIPHER_get_block_size(cipher)))},
        std::pair<AtomName, Term>{"prop_aead"_atom, atom(context, aead ? "true"_atom : "false"_atom)},
        std::pair<AtomName, Term>{"mode"_atom, atom(context, mode_name(cipher))}};
    return atom_map(context, entries);
}

// Change a state's direction (crypto_init/4 given a state): FlagOrOptions must be a boolean.
Term reinitialize(ProcessContext &context, const Term &handle, CipherState &state, const Term &flag) {
    if (!is_atom_named(flag, "true") && !is_atom_named(flag, "false")) {
        reject(context, Fault::badarg, {3, "Expected true or false"});
    }
    std::scoped_lock lock(state.lock);
    state.encrypt = is_atom_named(flag, "true") ? 1 : 0;
    if (EVP_CipherInit_ex(state.context.get(), nullptr, nullptr, nullptr, nullptr, state.encrypt) != 1) {
        reject(context, Fault::error, {-1, "Can't initialize encflag"});
    }
    return handle;
}

// crypto_init(Cipher, Key, IV, FlagOrOptions): a new state; given a state for Cipher, the same state redirected.
TermResult<Term> crypto_init(ProcessContext &context, const Named<struct CipherType> &type,
                             const Named<struct CipherKey> &key, const Named<struct CipherIv> &iv,
                             const Named<struct CipherOptions> &options) {
    if (type.term.is_atom()) {
        auto state = std::make_unique<CipherState>();
        initialize(context, *state, {type.term, key.term, iv.term, options.term, 3});
        return owned_handle(context, CIPHER_MAGIC, std::move(state));
    }
    auto *state = owned_object<CipherState>(type.term, CIPHER_MAGIC);
    if (!state) {
        reject(context, Fault::badarg, {0, "Expected cipher name atom"});
    }
    return reinitialize(context, type.term, *state, options.term);
}

// crypto_update(State, Data): the bytes Data gives, the state advanced.
TermResult<Term> crypto_update(ProcessContext &context, const Named<struct CipherHandle> &handle,
                               const Named<struct CipherData> &data) {
    const auto bytes = data_of(context, data.term, {1, "expected binary"});
    auto &state = state_of(context, handle.term);
    std::scoped_lock lock(state.lock);
    return TermFactory(context).binary(update(context, state, bytes));
}

// crypto_final(State): the last bytes, padded as the state's options say.
TermResult<Term> crypto_final(ProcessContext &context, const Term &handle) {
    auto &state = state_of(context, handle);
    std::scoped_lock lock(state.lock);
    return TermFactory(context).binary(finish(context, state));
}

// crypto_get_data(State): #{size, padding_size, padding_type, encrypt}.
TermResult<Term> crypto_get_data(ProcessContext &context, const Term &handle) {
    auto &state = state_of(context, handle);
    std::scoped_lock lock(state.lock);
    TermFactory factory(context);
    const auto padding = std::ranges::find(PADDINGS, state.padding, &std::pair<std::string_view, Padding>::second);
    const std::array<std::pair<AtomName, Term>, 4> entries{
        std::pair<AtomName, Term>{"size"_atom, need(factory.integer(static_cast<int>(state.size)))},
        std::pair<AtomName, Term>{"padding_size"_atom, need(factory.integer(state.padded_size))},
        std::pair<AtomName, Term>{"padding_type"_atom, atom(context, padding->first)},
        std::pair<AtomName, Term>{"encrypt"_atom, atom(context, state.encrypt != 0 ? "true"_atom : "false"_atom)}};
    return atom_map(context, entries);
}

// crypto_one_time(Cipher, Key, IV, Data, FlagOrOptions): the whole text through a temporary state.
TermResult<Term> crypto_one_time(ProcessContext &context, const Named<struct CipherType> &type,
                                 const Named<struct CipherKey> &key, const Named<struct CipherIv> &iv,
                                 const Named<struct CipherData> &data, const Named<struct CipherOptions> &options) {
    const auto bytes = data_of(context, data.term, {3, "expected binary as data"});
    CipherState state;
    initialize(context, state, {type.term, key.term, iv.term, options.term, 4});
    auto output = update(context, state, bytes);
    const auto last = finish(context, state);
    output.insert(output.end(), last.begin(), last.end());
    return TermFactory(context).binary(output);
}

constexpr std::array CIPHER_BUILTINS{
    typed_entry<cipher_info>("clause_crypto", "cipher_info"),
    typed_entry<crypto_init>("clause_crypto", "crypto_init"),
    typed_entry<crypto_update>("clause_crypto", "crypto_update"),
    typed_entry<crypto_final>("clause_crypto", "crypto_final"),
    typed_entry<crypto_get_data>("clause_crypto", "crypto_get_data"),
    typed_entry<crypto_one_time>("clause_crypto", "crypto_one_time"),
};
} // namespace

std::span<const BuiltinEntry> cipher_builtins() noexcept { return CIPHER_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
