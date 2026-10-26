#include "pkey.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <functional>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/objects.h>
#include <terms.hpp>

// Elliptic curves of crypto's public key builtins (plan 21 step LIB79C): OTP's curve definitions
// {{Field, {A, B, Seed}, Point, Order, CoFactor} | undefined, Name | undefined} read as ec.c reads them, with its retry
// on explicit parameters when OpenSSL does not know the name, elliptic curve keys, ec_generate_key, ecdh_compute_key,
// and ec_curve/1, the explicit parameters OTP's crypto_ec_curves holds, taken from OpenSSL's built-in curves.
namespace clause::runtime::builtins::crypto {
namespace {
// A private key on a curve: the curve definition {Curve, Name} and the key's binary.
struct CurveKey {
    Term curve;
    Term key;
};

// The two keys of a key agreement: own private key and the peer's public key.
struct KeyAgreement {
    EVP_PKEY *own;
    EVP_PKEY *peer;
};

// How a curve definition is read: by its name first, and whether the caller wants the order's byte size.
struct CurveRequest {
    bool use_name = true;
    bool want_order = false;
    std::size_t order_size = 0;
};

std::unexpected<KeyFailure> failure(Fault fault, Rejection rejection,
                                    std::source_location location = std::source_location::current()) {
    return std::unexpected(KeyFailure{.fault = fault, .rejection = rejection, .location = location});
}

using CurveResult = std::expected<void, KeyFailure>;

// An unsigned integer of a curve term, as enif_get_uint reads one.
std::optional<std::int64_t> unsigned_of(const Term &term) {
    const auto value = integer_of(term);
    if (!value || *value < 0 || *value > UINT_MAX) {
        return std::nullopt;
    }
    return *value;
}

// The exponents K of {tpbasis, K} or K1, K2, K3 of {ppbasis, K1, K2, K3}, with OTP's errors.
std::expected<std::vector<std::int64_t>, KeyFailure> basis_exponents(const Term &basis, bool tp) {
    if (!is_atom_named(basis.tuple_element_unchecked(0), tp ? "tpbasis" : "ppbasis")) {
        return failure(Fault::error, {1, "Bad atom"});
    }
    std::vector<std::int64_t> bits;
    for (std::size_t index = 1; index < basis.tuple_size_unchecked(); ++index) {
        const auto k = unsigned_of(basis.tuple_element_unchecked(index));
        if (!k) {
            return failure(Fault::error, {1, tp ? "uint expected (k1)" : "Expecting uint (k1,k2,k3)"});
        }
        bits.push_back(*k);
    }
    return bits;
}

// The reduction polynomial of {tpbasis, K} or {ppbasis, K1, K2, K3}; OTP raises these errors with class error.
CurveResult set_basis_bits(BIGNUM *polynomial, std::int64_t field_bits, const Term &basis) {
    const bool tp = basis.tuple_size_unchecked() == 2;
    auto exponents = basis_exponents(basis, tp);
    if (!exponents) {
        return std::unexpected(exponents.error());
    }
    auto &bits = *exponents;
    const bool ascending = std::ranges::adjacent_find(bits, std::greater_equal<>{}) == bits.end();
    if (bits.front() == 0 || field_bits <= bits.back() || !ascending || bits.back() > INT_MAX) {
        return failure(Fault::error,
                       {1, tp ? "bad values (field_bits or k1)" : "bad values (field_bits, k1, k2 or k3)"});
    }
    bits.push_back(field_bits);
    bits.push_back(0);
    for (const auto bit : bits) {
        BN_set_bit(polynomial, static_cast<int>(bit));
    }
    return {};
}

// {characteristic_two_field, M, Basis}: the field type and its reduction polynomial.
CurveResult add_binary_field(const Term &field, ParamList &params) {
    params.add_utf8("field-type", "characteristic-two-field");
    const auto bits = integer_of(field.tuple_element_unchecked(1));
    if (!bits || *bits > OPENSSL_ECC_MAX_FIELD_BITS) {
        return failure(Fault::error, {1, "Bad field-bits (M)"});
    }
    const auto basis = field.tuple_element_unchecked(2);
    if (is_atom_named(basis, "onbasis")) {
        return failure(Fault::notsup, {1, "'onbasis' not supported"});
    }
    if (!basis.is_tuple()) {
        return failure(Fault::error, {1, "Bad last field"});
    }
    if (basis.tuple_size_unchecked() != 2 && basis.tuple_size_unchecked() != 4) {
        return failure(Fault::error, {1, "Bad tuple"});
    }
    const Bignum polynomial(BN_new());
    if (const auto set = set_basis_bits(polynomial.get(), *bits, basis); !set) {
        return set;
    }
    params.add_bignum("p", polynomial.get());
    return {};
}

// The field of an explicit curve: {prime_field, P} or {characteristic_two_field, M, Basis}.
CurveResult add_field(const Term &field, ParamList &params) {
    if (!field.is_tuple()) {
        return failure(Fault::error, {1, "Bad Field"});
    }
    const auto arity = field.tuple_size_unchecked();
    if (arity == 2 && is_atom_named(field.tuple_element_unchecked(0), "prime_field")) {
        params.add_utf8("field-type", "prime-field");
        if (!params.add_number("p", field.tuple_element_unchecked(1))) {
            return failure(Fault::error, {1, "Bad p (Prime)"});
        }
        return {};
    }
    if (arity == 3 && is_atom_named(field.tuple_element_unchecked(0), "characteristic_two_field")) {
        return add_binary_field(field, params);
    }
    return failure(Fault::error, {1, "Bad field-type"});
}

// The explicit parameters of a curve tuple {Field, {A, B, Seed}, Point, Order, CoFactor}, in OTP's order.
CurveResult add_explicit_curve(const Term &curve, ParamList &params, CurveRequest &request) {
    if (!params.add_octets("generator", curve.tuple_element_unchecked(2))) {
        return failure(Fault::error, {1, "Bad Generator (Point)"});
    }
    const auto order = curve.tuple_element_unchecked(3);
    if (!params.add_number("order", order)) {
        return failure(Fault::error, {1, "Bad order"});
    }
    request.order_size = need(order.binary_bytes()).size();
    const auto cofactor = curve.tuple_element_unchecked(4);
    if (is_atom_named(cofactor, "none")) {
        return failure(Fault::error, {1, "Cofactor must be != none"});
    }
    if (!params.add_number("cofactor", cofactor)) {
        return failure(Fault::error, {1, "Bad cofactor"});
    }
    const auto prime = curve.tuple_element_unchecked(1);
    if (!prime.is_tuple() || prime.tuple_size_unchecked() < 3) {
        return failure(Fault::error, {1, "Bad Prime"});
    }
    if (!params.add_number("a", prime.tuple_element_unchecked(0))) {
        return failure(Fault::error, {1, "Bad a"});
    }
    if (!params.add_number("b", prime.tuple_element_unchecked(1))) {
        return failure(Fault::error, {1, "Bad b"});
    }
    if (prime.tuple_element_unchecked(2).is_binary()) {
        params.add_octets("seed", prime.tuple_element_unchecked(2));
    }
    return add_field(curve.tuple_element_unchecked(0), params);
}

// The curve name OTP's get_curve_definition uses: an atom of fewer than 20 Latin-1 characters.
std::optional<std::string_view> usable_name(const Term &name) {
    if (!name.is_atom() || is_atom_named(name, "undefined")) {
        return std::nullopt;
    }
    const auto spelling = name.atom_spelling_unchecked();
    if (spelling.size() >= 20) {
        return std::nullopt;
    }
    return spelling;
}

// A named curve: its name, and the byte size of its order when the request wants it.
CurveResult add_curve_name(const Term &curve, std::string_view name, ParamList &params, CurveRequest &request) {
    params.add_utf8("group", name);
    if (!request.want_order) {
        return {};
    }
    const auto order = curve.tuple_element_unchecked(3);
    if (!order.is_binary()) {
        return failure(Fault::error, {1, "Bad order"});
    }
    request.order_size = need(order.binary_bytes()).size();
    return {};
}

// Add a curve definition {Curve, Name} to `params`: its name while the request uses names, else its parameters.
CurveResult add_curve_definition(const Term &definition, ParamList &params, CurveRequest &request) {
    if (!definition.is_tuple() || definition.tuple_size_unchecked() != 2) {
        return failure(Fault::error, {-1, "Tuple arity 2 expected."});
    }
    const auto curve = definition.tuple_element_unchecked(0);
    if (!curve.is_tuple() || curve.tuple_size_unchecked() != 5) {
        return failure(Fault::error, {1, "Bad curve def. Expect 5-tuple."});
    }
    const auto name = request.use_name ? usable_name(definition.tuple_element_unchecked(1)) : std::nullopt;
    if (name) {
        return add_curve_name(curve, *name, params, request);
    }
    request.use_name = false;
    return add_explicit_curve(curve, params, request);
}

// An elliptic curve key from a key parameter and a curve definition, retried with explicit parameters when OpenSSL
// does not know the curve's name (OTP's retry_without_name).
KeyResult curve_key(const Term &curve, ParamList &params, int selection, CurveRequest &request) {
    for (;;) {
        if (const auto added = add_curve_definition(curve, params, request); !added) {
            return std::unexpected(added.error());
        }
        if (auto key = key_from_params("EC", selection, params)) {
            return key;
        }
        if (!request.use_name) {
            return failure(Fault::error, {-1, "Can't do fromdata"});
        }
        request.use_name = false;
        params.truncate(1);
    }
}

// OTP's get_ec_private_key_2: a private key on a curve definition.
KeyResult private_key_on(const CurveKey &curve_key_term, CurveRequest &request) {
    const auto &[curve, key] = curve_key_term;
    ParamList params;
    if (!params.add_number("priv", key)) {
        return failure(Fault::badarg, {0, "Bad private key"});
    }
    return curve_key(curve, params, EVP_PKEY_KEYPAIR, request);
}

// Whether a key term is {CurveDefinition, Binary}.
bool is_curve_key_term(const Term &key) {
    return key.is_tuple() && key.tuple_size_unchecked() == 2 && key.tuple_element_unchecked(0).is_tuple() &&
           key.tuple_element_unchecked(1).is_binary();
}

// An owned curve group.
struct GroupDeleter {
    void operator()(EC_GROUP *group) const noexcept { EC_GROUP_free(group); }
};

using Group = std::unique_ptr<EC_GROUP, GroupDeleter>;

// An owned curve point.
struct PointDeleter {
    void operator()(EC_POINT *point) const noexcept { EC_POINT_free(point); }
};

using Point = std::unique_ptr<EC_POINT, PointDeleter>;

// The uncompressed bytes of a point.
std::vector<std::byte> point_bytes(const EC_GROUP *group, const EC_POINT *point) {
    const auto size = EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, nullptr, 0, nullptr);
    std::vector<std::byte> bytes(size);
    EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, reinterpret_cast<unsigned char *>(bytes.data()),
                       size, nullptr);
    return bytes;
}

// OTP's mk_pub_key_binary: the public point of a private key, computed on the key's named group.
std::vector<std::byte> public_point(ProcessContext &context, const EVP_PKEY *key) {
    std::array<char, 80> name{};
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, name.data(), name.size(), nullptr) != 1) {
        reject(context, Fault::error, {-1, "Couldn't get EC group name"});
    }
    const auto nid = OBJ_sn2nid(name.data());
    if (nid == NID_undef) {
        reject(context, Fault::error, {-1, "Couldn't get EC group nid"});
    }
    const Group group(EC_GROUP_new_by_curve_name(nid));
    const Point point(group ? EC_POINT_new(group.get()) : nullptr);
    if (!point) {
        reject(context, Fault::error, {-1, "Couldn't get EC_GROUP"});
    }
    BIGNUM *secret = nullptr;
    if (EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_PRIV_KEY, &secret) != 1) {
        reject(context, Fault::badarg, {1, "Couldn't get peer priv key bytes"});
    }
    const Bignum owned(secret);
    if (EC_POINT_mul(group.get(), point.get(), owned.get(), nullptr, nullptr, nullptr) != 1) {
        reject(context, Fault::error, {-1, "Couldn't multiply POINT"});
    }
    return point_bytes(group.get(), point.get());
}

// A big number padded on the left to `size` bytes (OTP's bn2term); badarg when it is longer.
std::vector<std::byte> padded(const std::vector<std::byte> &bytes, std::size_t size) {
    if (bytes.size() > size) {
        bad_argument();
    }
    std::vector<std::byte> result(size - bytes.size());
    result.insert(result.end(), bytes.begin(), bytes.end());
    return result;
}

// One key generation attempt on a curve definition; null when OpenSSL could not generate it.
Pkey try_generate(ProcessContext &context, EVP_PKEY_CTX *generator, const Term &curve, CurveRequest &request) {
    ParamList params;
    if (!add_curve_definition(curve, params, request)) {
        reject(context, Fault::badarg, {0, "Couldn't get Curve definition"});
    }
    if (EVP_PKEY_keygen_init(generator) <= 0) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_keygen_init"});
    }
    if (EVP_PKEY_CTX_set_params(generator, params.ended()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_CTX_set_params"});
    }
    EVP_PKEY *key = nullptr;
    return Pkey(EVP_PKEY_generate(generator, &key) == 1 ? key : nullptr);
}

// A generated key pair on a curve definition: OTP's keygen with its retry on explicit parameters.
Pkey generated_on(ProcessContext &context, const Term &curve, CurveRequest &request) {
    const PkeyContext generator(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    if (!generator) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_CTX_new_from_name"});
    }
    for (;;) {
        if (auto key = try_generate(context, generator.get(), curve, request)) {
            return key;
        }
        if (!request.use_name) {
            reject(context, Fault::error, {-1, "Couldn't generate EC key"});
        }
        request.use_name = false;
    }
}

// The encoded public key of a generated pair.
std::vector<std::byte> encoded_public(ProcessContext &context, const EVP_PKEY *key) {
    std::size_t size = 0;
    if (EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, nullptr, 0, &size) != 1) {
        reject(context, Fault::error, {-1, "Can't get pub octet string size"});
    }
    std::vector<std::byte> bytes(size);
    if (EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY,
                                        reinterpret_cast<unsigned char *>(bytes.data()), size, &size) != 1) {
        reject(context, Fault::error, {-1, "Can't get pub octet string"});
    }
    bytes.resize(size);
    return bytes;
}

// The public and private keys of ec_generate_key: computed from a given private key, or generated.
std::pair<std::vector<std::byte>, Pkey> curve_pair(ProcessContext &context, const Term &curve, const Term &secret,
                                                   CurveRequest &request) {
    if (is_atom_named(secret, "undefined")) {
        auto key = generated_on(context, curve, request);
        auto public_key = encoded_public(context, key.get());
        return {std::move(public_key), std::move(key)};
    }
    auto key = private_key_on({curve, secret}, request);
    if (!key) {
        raise_failure(context, key.error());
    }
    auto public_key = public_point(context, key->get());
    return {std::move(public_key), std::move(*key)};
}

// ec_generate_key(Curve, PrivKey | undefined): {PublicKey, PrivateKey}, the private key padded to the order's size.
TermResult<Term> ec_generate_key(ProcessContext &context, const Named<struct EcCurve> &curve,
                                 const Named<struct EcPrivate> &secret) {
    CurveRequest request{.want_order = true};
    const auto [public_key, key] = curve_pair(context, curve.term, secret.term, request);
    auto private_key = key_number(key.get(), OSSL_PKEY_PARAM_PRIV_KEY);
    if (!private_key) {
        reject(context, Fault::badarg, {1, "Couldn't get priv key bytes"});
    }
    const auto size = request.order_size == 0 ? private_key->size() : request.order_size;
    TermFactory factory(context);
    const std::array parts{need(factory.binary(public_key)), need(factory.binary(padded(*private_key, size)))};
    return factory.tuple(parts);
}

// The shared secret of a private key and a peer's public key (EVP_PKEY_derive), with OTP's ecdh.c errors.
std::vector<std::byte> derive_shared(ProcessContext &context, const KeyAgreement &agreement) {
    const auto [own, peer] = agreement;
    const PkeyContext deriver(EVP_PKEY_CTX_new(own, nullptr));
    if (!deriver) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_CTX_init"});
    }
    if (EVP_PKEY_derive_init(deriver.get()) <= 0) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_derive_init"});
    }
    if (EVP_PKEY_derive_set_peer_ex(deriver.get(), peer, 0) <= 0) {
        reject(context, Fault::error, {-1, "Can't derive secret or set peer"});
    }
    std::size_t size = 0;
    if (EVP_PKEY_derive(deriver.get(), nullptr, &size) <= 0) {
        reject(context, Fault::error, {-1, "Can't get result size"});
    }
    std::vector<std::byte> secret(size);
    if (EVP_PKEY_derive(deriver.get(), reinterpret_cast<unsigned char *>(secret.data()), &size) <= 0) {
        reject(context, Fault::error, {-1, "Can't get result"});
    }
    secret.resize(size);
    return secret;
}

// ecdh_compute_key(OthersPublicKey, Curve, MyPrivateKey): the ECDH shared secret.
TermResult<Term> ecdh_compute_key(ProcessContext &context, const Named<struct EcdhOthers> &others,
                                  const Named<struct EcdhCurve> &curve, const Named<struct EcdhMine> &mine) {
    ParamList params;
    if (!params.add_octets("pub", others.term)) {
        reject(context, Fault::badarg, {0, "Bad peer public key; binary expected"});
    }
    CurveRequest request;
    auto peer = curve_key(curve.term, params, EVP_PKEY_PUBLIC_KEY, request);
    if (!peer) {
        raise_failure(context, peer.error());
    }
    if (!params.replace_number(0, "priv", mine.term)) {
        reject(context, Fault::badarg, {0, "Bad peer public key; binary expected"});
    }
    const auto own = key_from_params("EC", EVP_PKEY_KEYPAIR, params);
    if (!own) {
        reject(context, Fault::error, {-1, "Can't do fromdata"});
    }
    return TermFactory(context).binary(derive_shared(context, {own.get(), peer->get()}));
}

// The minimal unsigned bytes of a curve number, zero as one byte, as crypto_ec_curves writes them.
TermResult<Term> number_term(TermFactory &factory, const BIGNUM *number) {
    auto bytes = bytes_of(number);
    if (bytes.empty()) {
        bytes.push_back(std::byte{0});
    }
    return factory.binary(bytes);
}

// The basis of a binary field from its reduction polynomial: {tpbasis, K} or {ppbasis, K1, K2, K3}.
TermResult<Term> basis_term(ProcessContext &context, const BIGNUM *polynomial, int degree) {
    TermFactory factory(context);
    std::vector<Term> parts;
    for (int bit = 1; bit < degree; ++bit) {
        if (BN_is_bit_set(polynomial, bit) != 0) {
            parts.push_back(need(factory.integer(bit)));
        }
    }
    parts.insert(parts.begin(), atom(context, parts.size() == 1 ? "tpbasis"_atom : "ppbasis"_atom));
    return factory.tuple(parts);
}

// The field term of a group whose curve is y^2 = ... over p.
TermResult<Term> field_term(ProcessContext &context, const EC_GROUP *group, const BIGNUM *p) {
    TermFactory factory(context);
    if (EC_GROUP_get_field_type(group) == NID_X9_62_prime_field) {
        const std::array parts{atom(context, "prime_field"_atom), need(number_term(factory, p))};
        return factory.tuple(parts);
    }
    const auto degree = EC_GROUP_get_degree(group);
    const std::array parts{atom(context, "characteristic_two_field"_atom), need(factory.integer(degree)),
                           need(basis_term(context, p, degree))};
    return factory.tuple(parts);
}

// The seed of a group, or none.
TermResult<Term> seed_term(ProcessContext &context, const EC_GROUP *group) {
    const auto *seed = EC_GROUP_get0_seed(group);
    if (!seed) {
        return atom(context, "none"_atom);
    }
    return TermFactory(context).binary(std::as_bytes(std::span(seed, EC_GROUP_get_seed_len(group))));
}

// The explicit curve term of a group: {Field, {A, B, Seed}, Point, Order, CoFactor}.
TermResult<Term> curve_term(ProcessContext &context, const EC_GROUP *group) {
    const Bignum p(BN_new());
    const Bignum a(BN_new());
    const Bignum b(BN_new());
    if (EC_GROUP_get_curve(group, p.get(), a.get(), b.get(), nullptr) != 1) {
        return atom(context, "undefined"_atom);
    }
    TermFactory factory(context);
    const std::array prime{need(number_term(factory, a.get())), need(number_term(factory, b.get())),
                           need(seed_term(context, group))};
    const std::array parts{need(field_term(context, group, p.get())), need(factory.tuple(prime)),
                           need(factory.binary(point_bytes(group, EC_GROUP_get0_generator(group)))),
                           need(number_term(factory, EC_GROUP_get0_order(group))),
                           need(number_term(factory, EC_GROUP_get0_cofactor(group)))};
    return factory.tuple(parts);
}

// ec_curve(Name): the explicit parameters of an OTP named curve, undefined for a name OTP does not know or a curve
// the linked libcrypto lacks.
TermResult<Term> ec_curve(ProcessContext &context, const Named<struct CurveName> &name) {
    const auto nid = name.term.is_atom() ? curve_nid(name.term.atom_spelling_unchecked()) : NID_undef;
    const Group group(nid == NID_undef ? nullptr : EC_GROUP_new_by_curve_name(nid));
    if (!group) {
        return atom(context, "undefined"_atom);
    }
    return curve_term(context, group.get());
}

constexpr std::array CURVE_BUILTINS{
    typed_entry<ec_generate_key>("clause_crypto", "ec_generate_key"),
    typed_entry<ecdh_compute_key>("clause_crypto", "ecdh_compute_key"),
    typed_entry<ec_curve>("clause_crypto", "ec_curve"),
};
} // namespace

KeyResult ec_private_key(const Term &key) {
    if (!is_curve_key_term(key)) {
        return failure(Fault::badarg, {0, "Bad private key format"});
    }
    CurveRequest request;
    return private_key_on({key.tuple_element_unchecked(0), key.tuple_element_unchecked(1)}, request);
}

KeyResult ec_public_key(const Term &key) {
    if (!is_curve_key_term(key)) {
        return failure(Fault::badarg, {0, "Bad public key format"});
    }
    ParamList params;
    params.add_octets("pub", key.tuple_element_unchecked(1));
    CurveRequest request;
    return curve_key(key.tuple_element_unchecked(0), params, EVP_PKEY_PUBLIC_KEY, request);
}

std::span<const BuiltinEntry> curve_builtins() noexcept { return CURVE_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
