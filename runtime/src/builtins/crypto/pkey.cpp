#include "pkey.hpp"
#include <array>
#include <climits>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <terms.hpp>

// Signatures and RSA encryption of crypto's public key builtins (plan 21 step LIB79C): pkey_sign/5, pkey_verify/6,
// pkey_crypt/6 and privkey_to_pubkey/2 read OTP's key terms, digest types and options as pkey.c does, in its order
// and with its errors. Engine keys (maps) are refused as by an OTP without engine support.
namespace clause::runtime::builtins::crypto {
namespace {
// OTP's pkey_types: the post-quantum signature algorithms, whose keys are raw octet strings.
constexpr std::array PKEY_TYPES{
    PkeyType{"mldsa44", "ML-DSA-44", true},
    PkeyType{"mldsa65", "ML-DSA-65", true},
    PkeyType{"mldsa87", "ML-DSA-87", true},
    PkeyType{"slh_dsa_shake_128s", "SLH-DSA-SHAKE-128s", false},
    PkeyType{"slh_dsa_shake_128f", "SLH-DSA-SHAKE-128f", false},
    PkeyType{"slh_dsa_sha2_128s", "SLH-DSA-SHA2-128s", false},
    PkeyType{"slh_dsa_sha2_128f", "SLH-DSA-SHA2-128f", false},
    PkeyType{"slh_dsa_shake_192s", "SLH-DSA-SHAKE-192s", false},
    PkeyType{"slh_dsa_shake_192f", "SLH-DSA-SHAKE-192f", false},
    PkeyType{"slh_dsa_sha2_192s", "SLH-DSA-SHA2-192s", false},
    PkeyType{"slh_dsa_sha2_192f", "SLH-DSA-SHA2-192f", false},
    PkeyType{"slh_dsa_shake_256s", "SLH-DSA-SHAKE-256s", false},
    PkeyType{"slh_dsa_shake_256f", "SLH-DSA-SHAKE-256f", false},
    PkeyType{"slh_dsa_sha2_256s", "SLH-DSA-SHA2-256s", false},
    PkeyType{"slh_dsa_sha2_256f", "SLH-DSA-SHA2-256f", false},
};

// The four classic signature algorithms pkey_crypt and privkey_to_pubkey accept.
bool classic_algorithm(const Term &algorithm) {
    return is_atom_named(algorithm, "rsa") || is_atom_named(algorithm, "dss") || is_atom_named(algorithm, "ecdsa") ||
           is_atom_named(algorithm, "eddsa");
}

// OTP's check_pkey_algorithm_type: eddsa is not supported in FIPS mode; without allow_unknown only the classic ones.
void check_algorithm(ProcessContext &context, const Term &algorithm, int argument, bool allow_unknown) {
    if (fips_enabled() && is_atom_named(algorithm, "eddsa")) {
        reject(context, Fault::notsup, {argument, "Unsupported algorithm in FIPS mode"});
    }
    if (!allow_unknown && !classic_algorithm(algorithm)) {
        reject(context, Fault::badarg, {argument, "Bad algorithm"});
    }
}

// OTP's get_pkey_digest_type: the digest a type names, null for none (RSA and the post-quantum types) and eddsa.
const EVP_MD *digest_type(ProcessContext &context, const Term &algorithm, const PkeyType *pkey_type, const Term &type,
                          int argument) {
    if (is_atom_named(type, "none") &&
        (is_atom_named(algorithm, "rsa") || (pkey_type && is_atom_named(algorithm, pkey_type->name)))) {
        return nullptr;
    }
    if (is_atom_named(algorithm, "eddsa")) {
        return nullptr;
    }
    const auto *digest = type.is_atom() ? find_digest(type.atom_spelling_unchecked()) : nullptr;
    if (!digest) {
        reject(context, Fault::badarg, {argument, "Bad digest type"});
    }
    if (forbidden_in_fips(*digest)) {
        reject(context, Fault::badarg, {argument, "Digest type forbidden in FIPS"});
    }
    if (!active(*digest)) {
        reject(context, Fault::badarg, {argument, "Digest type not supported"});
    }
    return active(*digest);
}

// What a signature covers: the digest type and the bytes to be signed (a digest, or the text when there is none).
struct Signed {
    const EVP_MD *md = nullptr;
    std::vector<std::byte> bytes;
};

// {digest, Digest}: a digest already computed, of the digest type's size.
std::vector<std::byte> given_digest(ProcessContext &context, const Term &data, const EVP_MD *md) {
    if (data.tuple_size_unchecked() != 2) {
        reject(context, Fault::badarg, {2, "Bad list"});
    }
    if (!is_atom_named(data.tuple_element_unchecked(0), "digest")) {
        reject(context, Fault::badarg, {2, "Expected 'digest' as head"});
    }
    auto bytes = iodata_argument(context, data.tuple_element_unchecked(1), {2, "Bad 2nd element in list"});
    if (bytes.size() > INT_MAX) {
        reject(context, Fault::badarg, {2, "Too large binary"});
    }
    if (md && static_cast<int>(bytes.size()) != EVP_MD_get_size(md)) {
        reject(context, Fault::badarg, {2, "Bad binary size for the algorithm"});
    }
    return bytes;
}

// The arguments a signature covers: Algorithm, Type and Data, and the algorithm's post-quantum key type.
struct SignArguments {
    Term algorithm;
    Term type;
    Term data;
    const PkeyType *pkey_type;
};

// OTP's get_pkey_sign_digest over the arguments (Algorithm, Type, Data, ...).
Signed signed_bytes(ProcessContext &context, const SignArguments &arguments) {
    check_algorithm(context, arguments.algorithm, 0, true);
    Signed result{.md = digest_type(context, arguments.algorithm, arguments.pkey_type, arguments.type, 1), .bytes = {}};
    if (arguments.data.is_tuple()) {
        result.bytes = given_digest(context, arguments.data, result.md);
        return result;
    }
    const auto text = iodata_argument(context, arguments.data, {2, "Expected a binary or a list"});
    if (!result.md) {
        result.bytes = text;
        return result;
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned length = 0;
    if (EVP_Digest(bytes_address(text), text.size(), digest.data(), &length, result.md, nullptr) != 1) {
        reject(context, Fault::error, {-1, "Can't create EVP_DigestFinal_ex"});
    }
    result.bytes.assign(reinterpret_cast<const std::byte *>(digest.data()),
                        reinterpret_cast<const std::byte *>(digest.data()) + length);
    return result;
}

// The RSA options of a signature (OTP's PKeySignOptions).
struct SignOptions {
    const EVP_MD *mgf1_md = nullptr;
    int padding = 0;
    int pss_saltlen = 0;
};

// One {rsa_padding, Padding} option of a signature.
void sign_padding_option(ProcessContext &context, const Term &value, int argument, const EVP_MD *md,
                         SignOptions &options) {
    if (is_atom_named(value, "rsa_pkcs1_padding")) {
        options.padding = RSA_PKCS1_PADDING;
    } else if (is_atom_named(value, "rsa_pkcs1_pss_padding")) {
        options.padding = RSA_PKCS1_PSS_PADDING;
        options.mgf1_md = options.mgf1_md ? options.mgf1_md : md;
    } else if (is_atom_named(value, "rsa_x931_padding")) {
        options.padding = RSA_X931_PADDING;
    } else if (is_atom_named(value, "rsa_no_padding")) {
        options.padding = RSA_NO_PADDING;
    } else {
        reject(context, Fault::badarg, {argument, "Bad value in option rsa_padding"});
    }
}

// An option list argument, its argument number, and the algorithm it is read for.
struct OptionList {
    Term algorithm;
    Term list;
    int argument;
};

// The digest of an atom-valued option, or OTP's error for another value.
const EVP_MD *digest_option(ProcessContext &context, const OptionList &list, const Term &value, Rejection not_atom,
                            Fault fault = Fault::badarg) {
    if (!value.is_atom()) {
        reject(context, fault, not_atom);
    }
    return digest_type(context, list.algorithm, nullptr, value, list.argument);
}

// The {rsa_pss_saltlen, Length} option: -2 and up.
int saltlen_option(ProcessContext &context, const Term &value, int argument) {
    const auto saltlen = integer_of(value);
    if (!saltlen || *saltlen < -2 || *saltlen > INT_MAX) {
        reject(context, Fault::badarg, {argument, "Bad value in option rsa_pss_saltlen"});
    }
    return static_cast<int>(*saltlen);
}

// One option of a signature's option list.
void sign_option(ProcessContext &context, const OptionList &list, const Term &option, const Signed &input,
                 SignOptions &options) {
    const auto argument = list.argument;
    if (!option.is_tuple() || option.tuple_size_unchecked() != 2) {
        reject(context, Fault::badarg, {argument, "Expects only two-tuples in the list"});
    }
    const auto tag = option.tuple_element_unchecked(0);
    const auto value = option.tuple_element_unchecked(1);
    if (is_atom_named(tag, "rsa_mgf1_md")) {
        options.mgf1_md =
            digest_option(context, list, value, {argument, "Atom expected as argument to option rsa_mgf1_md"});
    } else if (is_atom_named(tag, "rsa_padding")) {
        sign_padding_option(context, value, argument, input.md, options);
    } else if (is_atom_named(tag, "rsa_pss_saltlen")) {
        options.pss_saltlen = saltlen_option(context, value, argument);
    } else {
        reject(context, Fault::badarg, {argument, "Bad option"});
    }
}

// OTP's get_pkey_sign_options: only RSA takes options; its defaults are PKCS #1 padding and OpenSSL's salt length.
SignOptions sign_options(ProcessContext &context, const OptionList &list, const Signed &input) {
    if (!list.list.is_list()) {
        reject(context, Fault::badarg, {list.argument, "Expected a list"});
    }
    SignOptions options;
    if (is_atom_named(list.algorithm, "rsa")) {
        options.padding = RSA_PKCS1_PADDING;
        options.pss_saltlen = -2;
    }
    if (list.list.is_nil()) {
        return options;
    }
    if (!is_atom_named(list.algorithm, "rsa")) {
        reject(context, Fault::badarg, {list.argument, "Only RSA supports Options"});
    }
    for (auto rest = list.list; rest.is_cons(); rest = rest.tail_unchecked()) {
        sign_option(context, list, rest.head_unchecked(), input, options);
    }
    return options;
}

// A key argument as OTP's key readers see it: the algorithm, the key, its argument number and post-quantum type.
struct KeyArgument {
    Term algorithm;
    Term key;
    int argument;
    const PkeyType *pkey_type;
};

// The private key of a key type that is neither RSA, DSA, ECDSA nor EdDSA: raw, {seed, Seed} or {expandedkey, Key}.
Pkey raw_private_key(ProcessContext &context, const KeyArgument &argument_key) {
    const auto &[algorithm, key, argument, pkey_type] = argument_key;
    auto format = OctetFormat::private_key;
    auto bytes = key;
    if (key.is_tuple() && key.tuple_size_unchecked() == 2) {
        const auto tag = key.tuple_element_unchecked(0);
        if (is_atom_named(tag, "seed")) {
            if (!pkey_type || !pkey_type->allow_seed) {
                reject(context, Fault::badarg, {argument, "Seed key not allowed"});
            }
            format = OctetFormat::seed;
        } else if (!is_atom_named(tag, "expandedkey")) {
            reject(context, Fault::badarg, {argument, "Invalid key tuple format"});
        }
        bytes = key.tuple_element_unchecked(1);
    }
    return octet_key(context, {algorithm, bytes, format, pkey_type});
}

// The EdDSA key of a private or public key term; notsup in FIPS mode.
Pkey checked_eddsa_key(ProcessContext &context, const Term &key, int argument, bool is_public) {
    if (fips_enabled()) {
        reject(context, Fault::notsup, {0, "EDDSA not supported in FIPS mode"});
    }
    auto pkey = eddsa_key(key, is_public);
    if (!pkey) {
        reject(context, Fault::badarg,
               {argument, is_public ? "Couldn't get EDDSA public key" : "Couldn't get EDDSA private key"});
    }
    return pkey;
}

// The classic key reader of an algorithm and its error, or none for the other algorithms.
struct ClassicReader {
    std::string_view algorithm;
    Pkey (*read)(const Term &key, bool is_public);
    std::string_view private_error;
    std::string_view public_error;
};

Pkey read_rsa(const Term &key, bool is_public) { return is_public ? rsa_public_key(key) : rsa_private_key(key); }

Pkey read_ecdsa(const Term &key, bool is_public) {
    auto result = is_public ? ec_public_key(key) : ec_private_key(key);
    return result ? std::move(*result) : nullptr;
}

constexpr std::array CLASSIC_READERS{
    ClassicReader{"rsa", read_rsa, "Couldn't get RSA private key", "Couldn't get RSA public key"},
    ClassicReader{"ecdsa", read_ecdsa, "Couldn't get ECDSA private key", "Couldn't get ECDSA public key"},
    ClassicReader{"dss", dss_key, "Couldn't get DSA private key", "Couldn't get DSA public key"},
};

// A classic key with its reader's error when the term is not such a key.
Pkey classic_key(ProcessContext &context, const ClassicReader &reader, const KeyArgument &argument, bool is_public) {
    auto pkey = reader.read(argument.key, is_public);
    if (!pkey) {
        reject(context, Fault::badarg, {argument.argument, is_public ? reader.public_error : reader.private_error});
    }
    return pkey;
}

// OTP's get_pkey_private_key and get_pkey_public_key: the key argument of an algorithm.
Pkey key_argument(ProcessContext &context, const KeyArgument &argument, bool is_public) {
    if (argument.key.is_map()) {
        reject(context, Fault::badarg, {argument.argument, "No engine support"});
    }
    const auto reader = std::ranges::find_if(CLASSIC_READERS, [&](const ClassicReader &candidate) {
        return is_atom_named(argument.algorithm, candidate.algorithm);
    });
    if (reader != CLASSIC_READERS.end()) {
        return classic_key(context, *reader, argument, is_public);
    }
    if (is_atom_named(argument.algorithm, "eddsa")) {
        return checked_eddsa_key(context, argument.key, argument.argument, is_public);
    }
    if (is_public) {
        return octet_key(context, {argument.algorithm, argument.key, OctetFormat::public_key, argument.pkey_type});
    }
    return raw_private_key(context, argument);
}

// A one-shot signature or verification over a message context (EdDSA and the post-quantum types).
struct MessageContextDeleter {
    void operator()(EVP_MD_CTX *context) const noexcept { EVP_MD_CTX_free(context); }
};

using MessageContext = std::unique_ptr<EVP_MD_CTX, MessageContextDeleter>;

// Sign the whole message in one go (EVP_DigestSign), with the init and sign errors of the caller's OTP path.
std::vector<std::byte> sign_message(ProcessContext &context, EVP_PKEY *key, const std::vector<std::byte> &message,
                                    std::array<std::string_view, 2> errors) {
    const MessageContext signer(EVP_MD_CTX_new());
    if (!signer || EVP_DigestSignInit_ex(signer.get(), nullptr, nullptr, nullptr, nullptr, key, nullptr) != 1) {
        reject(context, Fault::error, {-1, errors[0]});
    }
    std::size_t size = 0;
    if (EVP_DigestSign(signer.get(), nullptr, &size, bytes_address(message), message.size()) != 1) {
        reject(context, Fault::error, {-1, errors[1]});
    }
    std::vector<std::byte> signature(size);
    if (EVP_DigestSign(signer.get(), reinterpret_cast<unsigned char *>(signature.data()), &size, bytes_address(message),
                       message.size()) != 1) {
        reject(context, Fault::error, {-1, errors[1]});
    }
    signature.resize(size);
    return signature;
}

// Verify a whole message in one go (EVP_DigestVerify); OTP's result is true only for 1.
bool verify_message(ProcessContext &context, EVP_PKEY *key, const std::vector<std::byte> &message,
                    const std::vector<std::byte> &signature, std::string_view init_error) {
    const MessageContext verifier(EVP_MD_CTX_new());
    if (!verifier || EVP_DigestVerifyInit_ex(verifier.get(), nullptr, nullptr, nullptr, nullptr, key, nullptr) != 1) {
        reject(context, Fault::error, {-1, init_error});
    }
    return EVP_DigestVerify(verifier.get(), bytes_address(signature), signature.size(), bytes_address(message),
                            message.size()) == 1;
}

// Set the digest and RSA options of a signing or verifying context, with OTP's errors; `argument` is the options'.
void set_sign_options(ProcessContext &context, EVP_PKEY_CTX *pkey_context, const Term &algorithm, const Signed &input,
                      const SignOptions &options, int argument) {
    if (input.md && EVP_PKEY_CTX_set_signature_md(pkey_context, input.md) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_CTX_set_signature_md"});
    }
    if (!is_atom_named(algorithm, "rsa")) {
        return;
    }
    if (EVP_PKEY_CTX_set_rsa_padding(pkey_context, options.padding) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_CTX_set_rsa_padding"});
    }
    if (options.padding != RSA_PKCS1_PSS_PADDING) {
        return;
    }
    if (options.mgf1_md && EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_context, options.mgf1_md) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_CTX_set_rsa_mgf1_md"});
    }
    if (options.pss_saltlen > -2 && EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_context, options.pss_saltlen) != 1) {
        reject(context, Fault::badarg, {argument, "Bad rsa_pss_saltlen"});
    }
}

// Sign a digest (or, without a digest type, the text) with a classic key: EVP_PKEY_sign.
std::vector<std::byte> sign_digest(ProcessContext &context, EVP_PKEY *key, const Term &algorithm, const Signed &input,
                                   const SignOptions &options) {
    const PkeyContext signer(EVP_PKEY_CTX_new(key, nullptr));
    if (!signer) {
        reject(context, Fault::error, {-1, "Can't allocate new EVP_PKEY_CTX"});
    }
    if (EVP_PKEY_sign_init(signer.get()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_sign_init"});
    }
    set_sign_options(context, signer.get(), algorithm, input, options, 4);
    const auto *bytes = bytes_address(input.bytes);
    std::size_t size = 0;
    if (EVP_PKEY_sign(signer.get(), nullptr, &size, bytes, input.bytes.size()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_sign"});
    }
    std::vector<std::byte> signature(size);
    if (EVP_PKEY_sign(signer.get(), reinterpret_cast<unsigned char *>(signature.data()), &size, bytes,
                      input.bytes.size()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_sign"});
    }
    signature.resize(size);
    return signature;
}

// pkey_sign(Algorithm, Type, Data | {digest, Digest}, Key, Options): a signature.
TermResult<Term> pkey_sign(ProcessContext &context, const Named<struct SignAlgorithm> &algorithm,
                           const Named<struct SignType> &type, const Named<struct SignData> &data,
                           const Named<struct SignKey> &key, const Named<struct SignOptions> &options) {
    if (key.term.is_map()) {
        reject(context, Fault::badarg, {3, "No engine support"});
    }
    const auto *pkey_type = find_pkey_type(algorithm.term);
    const auto input = signed_bytes(context, {algorithm.term, type.term, data.term, pkey_type});
    const auto sign_opts = sign_options(context, {algorithm.term, options.term, 4}, input);
    const auto pkey = key_argument(context, {algorithm.term, key.term, 3, pkey_type}, false);
    std::vector<std::byte> signature;
    if (is_atom_named(algorithm.term, "eddsa")) {
        signature =
            sign_message(context, pkey.get(), input.bytes, {"Can't EVP_DigestSignInit", "Can't EVP_DigestSign"});
    } else if (pkey_type) {
        signature =
            sign_message(context, pkey.get(), input.bytes, {"Can't EVP_PKEY_sign_message_init", "Can't EVP_PKEY_sign"});
    } else {
        signature = sign_digest(context, pkey.get(), algorithm.term, input, sign_opts);
    }
    return TermFactory(context).binary(signature);
}

// Verify a digest (or text) with a classic key: EVP_PKEY_verify.
bool verify_digest(ProcessContext &context, EVP_PKEY *key, const Term &algorithm, const Signed &input,
                   const SignOptions &options, const std::vector<std::byte> &signature) {
    const PkeyContext verifier(EVP_PKEY_CTX_new(key, nullptr));
    if (!verifier) {
        reject(context, Fault::error, {-1, "Can't allocate new EVP_PKEY_CTX"});
    }
    if (EVP_PKEY_verify_init(verifier.get()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_verify_init"});
    }
    set_sign_options(context, verifier.get(), algorithm, input, options, 5);
    return EVP_PKEY_verify(verifier.get(), bytes_address(signature), signature.size(), bytes_address(input.bytes),
                           input.bytes.size()) == 1;
}

// pkey_verify(Algorithm, Type, Data | {digest, Digest}, Signature, Key, Options): whether the signature holds.
// OTP's engine check looks at the signature argument.
TermResult<Term> pkey_verify(ProcessContext &context, const Named<struct VerifyAlgorithm> &algorithm,
                             const Named<struct VerifyType> &type, const Named<struct VerifyData> &data,
                             const Named<struct VerifySignature> &signature, const Named<struct VerifyKey> &key,
                             const Named<struct VerifyOptions> &options) {
    if (signature.term.is_map()) {
        reject(context, Fault::badarg, {3, "No engine support"});
    }
    const auto *pkey_type = find_pkey_type(algorithm.term);
    const auto input = signed_bytes(context, {algorithm.term, type.term, data.term, pkey_type});
    const auto sign_opts = sign_options(context, {algorithm.term, options.term, 5}, input);
    if (!signature.term.is_binary()) {
        reject(context, Fault::badarg, {3, "Expected a binary"});
    }
    const auto signature_bytes = need(signature.term.binary_bytes());
    const auto pkey = key_argument(context, {algorithm.term, key.term, 4, pkey_type}, true);
    bool valid = false;
    if (is_atom_named(algorithm.term, "eddsa")) {
        valid = verify_message(context, pkey.get(), input.bytes, signature_bytes, "Can't EVP_DigestVerifyInit");
    } else if (pkey_type) {
        valid = verify_message(context, pkey.get(), input.bytes, signature_bytes, "Can't EVP_PKEY_verify_message_init");
    } else {
        valid = verify_digest(context, pkey.get(), algorithm.term, input, sign_opts, signature_bytes);
    }
    return atom(context, valid ? "true"_atom : "false"_atom);
}

// The RSA options of pkey_crypt (OTP's PKeyCryptOptions).
struct CryptOptions {
    const EVP_MD *mgf1_md = nullptr;
    std::vector<std::byte> oaep_label;
    const EVP_MD *oaep_md = nullptr;
    int padding = 0;
    const EVP_MD *signature_md = nullptr;
};

// One {rsa_padding | rsa_pad, Padding} option of pkey_crypt.
void crypt_padding_option(ProcessContext &context, const Term &value, CryptOptions &options) {
    constexpr std::array<std::pair<std::string_view, int>, 4> PADDINGS{
        {{"rsa_pkcs1_padding", RSA_PKCS1_PADDING},
         {"rsa_pkcs1_oaep_padding", RSA_PKCS1_OAEP_PADDING},
         {"rsa_x931_padding", RSA_X931_PADDING},
         {"rsa_no_padding", RSA_NO_PADDING}}};
    for (const auto &[name, padding] : PADDINGS) {
        if (is_atom_named(value, name)) {
            options.padding = padding;
            return;
        }
    }
    reject(context, Fault::badarg, {3, "Bad padding type in option rsa_padding"});
}

// The {rsa_oaep_label, Label} option: a binary.
std::vector<std::byte> oaep_label_option(ProcessContext &context, const Term &value) {
    if (!value.is_binary()) {
        reject(context, Fault::badarg, {3, "Binary expected for option rsa_oaep_label"});
    }
    return need(value.binary_bytes());
}

// One option of pkey_crypt's option list.
void crypt_option(ProcessContext &context, const OptionList &list, const Term &option, CryptOptions &options) {
    if (!option.is_tuple() || option.tuple_size_unchecked() != 2) {
        reject(context, Fault::badarg, {3, "Expect only two-tuples in the list"});
    }
    const auto tag = option.tuple_element_unchecked(0);
    const auto value = option.tuple_element_unchecked(1);
    if (is_atom_named(tag, "rsa_padding") || is_atom_named(tag, "rsa_pad")) {
        crypt_padding_option(context, value, options);
    } else if (is_atom_named(tag, "signature_md")) {
        options.signature_md =
            digest_option(context, list, value, {3, "Atom expected as argument to option signature_md"});
    } else if (is_atom_named(tag, "rsa_mgf1_md")) {
        options.mgf1_md = digest_option(context, list, value, {3, "Atom expected as argument to option rsa_mgf1_md"});
    } else if (is_atom_named(tag, "rsa_oaep_label")) {
        options.oaep_label = oaep_label_option(context, value);
    } else if (is_atom_named(tag, "rsa_oaep_md")) {
        options.oaep_md =
            digest_option(context, list, value, {3, "Atom expected as argument to option rsa_oaep_md"}, Fault::notsup);
    } else {
        reject(context, Fault::badarg, {3, "Unknown option"});
    }
}

// OTP's get_pkey_crypt_options: RSA's options, PKCS #1 padding by default.
CryptOptions crypt_options(ProcessContext &context, const OptionList &list) {
    if (!list.list.is_list()) {
        reject(context, Fault::badarg, {3, "Expected a list"});
    }
    CryptOptions options;
    if (is_atom_named(list.algorithm, "rsa")) {
        options.padding = RSA_PKCS1_PADDING;
    }
    if (list.list.is_nil()) {
        return options;
    }
    if (!is_atom_named(list.algorithm, "rsa")) {
        reject(context, Fault::badarg, {3, "Only RSA supports Options"});
    }
    for (auto rest = list.list; rest.is_cons(); rest = rest.tail_unchecked()) {
        crypt_option(context, list, rest.head_unchecked(), options);
    }
    return options;
}

// The direction of pkey_crypt: a private key signs (encrypt) or decrypts, a public key encrypts or recovers.
struct CryptMode {
    bool is_private;
    bool is_encrypt;
};

// Start a pkey_crypt context in its direction.
int crypt_init(EVP_PKEY_CTX *pkey_context, CryptMode mode) {
    if (mode.is_private) {
        return mode.is_encrypt ? EVP_PKEY_sign_init(pkey_context) : EVP_PKEY_decrypt_init(pkey_context);
    }
    return mode.is_encrypt ? EVP_PKEY_encrypt_init(pkey_context) : EVP_PKEY_verify_recover_init(pkey_context);
}

// Run a pkey_crypt context on `in` (to `out`, or only its size when out is null).
int crypt_run(EVP_PKEY_CTX *pkey_context, CryptMode mode, unsigned char *out, std::size_t *size,
              const std::vector<std::byte> &in) {
    const auto *bytes = bytes_address(in);
    if (mode.is_private) {
        return mode.is_encrypt ? EVP_PKEY_sign(pkey_context, out, size, bytes, in.size())
                               : EVP_PKEY_decrypt(pkey_context, out, size, bytes, in.size());
    }
    return mode.is_encrypt ? EVP_PKEY_encrypt(pkey_context, out, size, bytes, in.size())
                           : EVP_PKEY_verify_recover(pkey_context, out, size, bytes, in.size());
}

// The OAEP digests and label of an RSA context.
void set_oaep_options(ProcessContext &context, EVP_PKEY_CTX *pkey_context, const CryptOptions &options) {
    if (options.oaep_md && EVP_PKEY_CTX_set_rsa_oaep_md(pkey_context, options.oaep_md) != 1) {
        reject(context, Fault::error, {-1, "Couldn't EVP_PKEY_CTX_set_rsa_oaep_md"});
    }
    if (options.mgf1_md && EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_context, options.mgf1_md) != 1) {
        reject(context, Fault::error, {-1, "Couldn't EVP_PKEY_CTX_set_rsa_mgf1_md"});
    }
    if (options.oaep_label.empty()) {
        return;
    }
    if (options.oaep_label.size() > INT_MAX) {
        reject(context, Fault::badarg, {3, "RSA oep label too large"});
    }
    // The context owns the copy once set.
    auto *label = static_cast<unsigned char *>(OPENSSL_memdup(options.oaep_label.data(), options.oaep_label.size()));
    if (EVP_PKEY_CTX_set0_rsa_oaep_label(pkey_context, label, static_cast<int>(options.oaep_label.size())) != 1) {
        OPENSSL_free(label);
        reject(context, Fault::error, {-1, "Couldn't set RSA oaep label"});
    }
}

// Set the RSA options of a pkey_crypt context.
void set_crypt_options(ProcessContext &context, EVP_PKEY_CTX *pkey_context, const CryptOptions &options) {
    if (options.signature_md && EVP_PKEY_CTX_set_signature_md(pkey_context, options.signature_md) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_CTX_set_signature_md"});
    }
    if (EVP_PKEY_CTX_set_rsa_padding(pkey_context, options.padding) != 1) {
        reject(context, Fault::error, {-1, "Couldn't set rsa padding"});
    }
    if (options.padding == RSA_PKCS1_OAEP_PADDING) {
        set_oaep_options(context, pkey_context, options);
    }
}

// pkey_crypt(Algorithm, Text, Key, Options, IsPrivate, IsEncrypt): RSA encryption and decryption.
TermResult<Term> pkey_crypt(ProcessContext &context, const Named<struct CryptAlgorithm> &algorithm,
                            const Named<struct CryptText> &text, const Named<struct CryptKey> &key,
                            const Named<struct CryptOptionList> &options, const Named<struct CryptPrivate> &is_private,
                            const Named<struct CryptEncrypt> &is_encrypt) {
    const CryptMode mode{is_atom_named(is_private.term, "true"), is_atom_named(is_encrypt.term, "true")};
    check_algorithm(context, algorithm.term, 0, false);
    if (!text.term.is_binary()) {
        reject(context, Fault::badarg, {1, "Binary expected"});
    }
    const auto in = need(text.term.binary_bytes());
    const auto pkey = key_argument(context, {algorithm.term, key.term, 2, nullptr}, !mode.is_private);
    const auto crypt_opts = crypt_options(context, {algorithm.term, options.term, 3});
    const PkeyContext crypter(EVP_PKEY_CTX_new(pkey.get(), nullptr));
    if (!crypter) {
        reject(context, Fault::error, {-1, "Can't allocate new EVP_PKEY_CTX"});
    }
    if (crypt_init(crypter.get(), mode) != 1) {
        reject(context, Fault::notsup, {-1, "Can't initiate encrypt/decrypt"});
    }
    if (is_atom_named(algorithm.term, "rsa")) {
        set_crypt_options(context, crypter.get(), crypt_opts);
    }
    std::size_t size = 0;
    if (crypt_run(crypter.get(), mode, nullptr, &size, in) != 1) {
        reject(context, Fault::error, {-1, "Couldn't get size of result"});
    }
    std::vector<std::byte> out(size);
    if (crypt_run(crypter.get(), mode, reinterpret_cast<unsigned char *>(out.data()), &size, in) != 1) {
        reject(context, Fault::error, {-1, "Couldn't get the result"});
    }
    out.resize(size);
    return TermFactory(context).binary(out);
}

// A list of binaries of a key's big number parameters; none when the key lacks one.
std::optional<std::vector<Term>> key_numbers(ProcessContext &context, const EVP_PKEY *key,
                                             std::span<const char *const> names) {
    TermFactory factory(context);
    std::vector<Term> numbers;
    for (const auto *name : names) {
        const auto bytes = key_number(key, name);
        if (!bytes) {
            return std::nullopt;
        }
        numbers.push_back(need(factory.binary(*bytes)));
    }
    return numbers;
}

// privkey_to_pubkey(Algorithm, PrivateKey): [E, N] of an RSA key, [P, Q, G, Y] of a DSA key.
TermResult<Term> privkey_to_pubkey(ProcessContext &context, const Named<struct PubkeyAlgorithm> &algorithm,
                                   const Named<struct PubkeyPrivate> &key) {
    check_algorithm(context, algorithm.term, 0, false);
    const auto pkey = key_argument(context, {algorithm.term, key.term, 1, nullptr}, false);
    static constexpr std::array<const char *, 2> RSA_NUMBERS{"e", "n"};
    static constexpr std::array<const char *, 4> DSA_NUMBERS{"p", "q", "g", "pub"};
    if (is_atom_named(algorithm.term, "ecdsa")) {
        reject(context, Fault::notsup, {0, "ECDSA not implemented"});
    }
    const bool rsa = is_atom_named(algorithm.term, "rsa");
    if (!rsa && !is_atom_named(algorithm.term, "dss")) {
        reject(context, Fault::badarg, {0, "Bad algorithm"});
    }
    const auto numbers =
        key_numbers(context, pkey.get(),
                    rsa ? std::span<const char *const>(RSA_NUMBERS) : std::span<const char *const>(DSA_NUMBERS));
    if (!numbers) {
        reject(
            context, Fault::badarg,
            {1, rsa ? "Couldn't get RSA public key from private key" : "Couldn't get DSA public key from private key"});
    }
    return TermFactory(context).list(*numbers);
}

constexpr std::array PKEY_BUILTINS{
    typed_entry<pkey_sign>("clause_crypto", "pkey_sign"),
    typed_entry<pkey_verify>("clause_crypto", "pkey_verify"),
    typed_entry<pkey_crypt>("clause_crypto", "pkey_crypt"),
    typed_entry<privkey_to_pubkey>("clause_crypto", "privkey_to_pubkey"),
};

// The OpenSSL key type of a raw key: its post-quantum type's, else the algorithm atom's spelling (OTP reads at most
// 39 characters).
std::string octet_type_name(ProcessContext &context, const Term &algorithm, const PkeyType *type) {
    if (type) {
        return std::string(type->fetch);
    }
    if (!algorithm.is_atom() || algorithm.atom_spelling_unchecked().size() >= 40) {
        reject(context, Fault::error, {0, "Invalid key type"});
    }
    return std::string(algorithm.atom_spelling_unchecked());
}

// The key parameter a raw key is given as.
const char *octet_param_name(OctetFormat format) {
    switch (format) {
    case OctetFormat::seed:
        return "seed";
    case OctetFormat::public_key:
        return "pub";
    case OctetFormat::private_key:
        break;
    }
    return "priv";
}

// Read binaries from a list into params under `names`; false when the list is shorter or holds a non-binary.
bool numbers_from_list(ParamList &params, Term &list, std::span<const char *const> names) {
    for (const auto *name : names) {
        if (!params.add_number_from_list(name, list)) {
            return false;
        }
    }
    return true;
}
} // namespace

const PkeyType *find_pkey_type(const Term &algorithm) {
    for (const auto &type : PKEY_TYPES) {
        if (is_atom_named(algorithm, type.name)) {
            return &type;
        }
    }
    return nullptr;
}

Pkey rsa_private_key(const Term &key) {
    static constexpr std::array<const char *, 3> KEY{"e", "n", "d"};
    static constexpr std::array<const char *, 5> FACTORS{"rsa-factor1", "rsa-factor2", "rsa-exponent1", "rsa-exponent2",
                                                         "rsa-coefficient1"};
    ParamList params;
    auto rest = key;
    if (!numbers_from_list(params, rest, KEY)) {
        return nullptr;
    }
    if (!rest.is_nil() && (!numbers_from_list(params, rest, FACTORS) || !rest.is_nil())) {
        return nullptr;
    }
    return key_from_params("RSA", EVP_PKEY_KEYPAIR, params);
}

Pkey rsa_public_key(const Term &key) {
    static constexpr std::array<const char *, 2> KEY{"e", "n"};
    ParamList params;
    auto rest = key;
    if (!numbers_from_list(params, rest, KEY) || !rest.is_nil()) {
        return nullptr;
    }
    return key_from_params("RSA", EVP_PKEY_PUBLIC_KEY, params);
}

Pkey dss_key(const Term &key, bool is_public) {
    static constexpr std::array<const char *, 4> PRIVATE{"p", "q", "g", "priv"};
    static constexpr std::array<const char *, 4> PUBLIC{"p", "q", "g", "pub"};
    ParamList params;
    auto rest = key;
    if (!numbers_from_list(params, rest, is_public ? PUBLIC : PRIVATE) || !rest.is_nil()) {
        return nullptr;
    }
    return key_from_params("DSA", is_public ? EVP_PKEY_PUBLIC_KEY : EVP_PKEY_KEYPAIR, params);
}

Pkey eddsa_key(const Term &key, bool is_public) {
    if (!key.is_cons() || !key.head_unchecked().is_binary() || !key.tail_unchecked().is_cons() ||
        !key.tail_unchecked().tail_unchecked().is_nil()) {
        return nullptr;
    }
    const auto curve = key.tail_unchecked().head_unchecked();
    const char *type = is_atom_named(curve, "ed25519") ? "ED25519" : is_atom_named(curve, "ed448") ? "ED448" : nullptr;
    if (!type) {
        return nullptr;
    }
    const auto bytes = need(key.head_unchecked().binary_bytes());
    const auto *raw = bytes_address(bytes);
    return Pkey(is_public ? EVP_PKEY_new_raw_public_key_ex(nullptr, type, nullptr, raw, bytes.size())
                          : EVP_PKEY_new_raw_private_key_ex(nullptr, type, nullptr, raw, bytes.size()));
}

Pkey octet_key(ProcessContext &context, const OctetKey &raw) {
    const auto &[algorithm, key, format, type] = raw;
    const auto name = octet_type_name(context, algorithm, type);
    const PkeyContext probe(EVP_PKEY_CTX_new_from_name(nullptr, name.c_str(), nullptr));
    if (!probe) {
        reject(context, Fault::error, {-1, type ? "Can't create PKEY_CTX from id" : "Can't create PKEY_CTX from name"});
    }
    ParamList params;
    if (!params.add_octets(octet_param_name(format), key)) {
        reject(context, Fault::error, {1, "Invalid public key"});
    }
    auto pkey = key_from_params(name.c_str(),
                                format == OctetFormat::public_key ? EVP_PKEY_PUBLIC_KEY : EVP_PKEY_KEYPAIR, params);
    if (!pkey) {
        reject(context, Fault::error, {-1, "Can't create PKEY fromdata"});
    }
    return pkey;
}

std::span<const BuiltinEntry> pkey_builtins() noexcept { return PKEY_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
