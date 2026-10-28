#include "ssl.hpp"
#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <span>
#include <terms.hpp>
#include <vector>

// Added for the ssl hello pause (plan 21 step LIB81C2): clause_ssl_algorithms:hello_extensions/2 decodes the
// extensions of a ClientHello or ServerHello as OTP's ssl_handshake decodes them for {handshake, hello}, with the
// top-level records replaced by their values (ssl_gen_statem's no_records). Certificate authorities stay DER; Erlang
// normalizes their names.
namespace clause::runtime::builtins::ssl {
namespace {
// A malformed hello, where OTP's decoding fails with badmatch or function_clause.
struct DecodeError {};

// A protocol version, ordered as OTP compares {Major, Minor} tuples.
struct Version {
    unsigned major_ = 0;
    unsigned minor_ = 0;
    auto operator<=>(const Version &) const = default;
};

constexpr Version TLS_1_2{3, 3};
constexpr Version TLS_1_3{3, 4};

enum class Message : std::uint8_t { client_hello, server_hello };

// A cursor over hello bytes; reading past the end is a DecodeError.
class Reader {
  public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    unsigned number(std::size_t size) {
        unsigned value = 0;
        for (const auto byte : take(size)) {
            value = (value << 8U) | std::to_integer<unsigned>(byte);
        }
        return value;
    }

    std::span<const std::byte> take(std::size_t size) {
        if (size > bytes_.size()) {
            throw DecodeError{};
        }
        const auto taken = bytes_.first(size);
        bytes_ = bytes_.subspan(size);
        return taken;
    }

    // The bytes of a vector whose length takes `size` bytes.
    std::span<const std::byte> vector(std::size_t size) { return take(number(size)); }

    std::span<const std::byte> rest() const { return bytes_; }

    bool empty() const { return bytes_.empty(); }

  private:
    // The bytes not read yet.
    std::span<const std::byte> bytes_;
};

// Builds the terms of decoded values.
class Builder {
  public:
    explicit Builder(ProcessContext &context) : context_(context) {}

    Term atom(std::string_view name) const { return builtins::atom(context_, AtomName(name)); }

    Term integer(std::int64_t value) const { return need(TermFactory(context_).integer(value)); }

    Term binary(std::span<const std::byte> bytes) const { return need(TermFactory(context_).binary(bytes)); }

    Term list(std::span<const Term> elements) const { return need(TermFactory(context_).list(elements)); }

    Term tuple(std::span<const Term> elements) const { return need(TermFactory(context_).tuple(elements)); }

    Term map(std::span<const std::pair<Term, Term>> entries) const { return need(TermFactory(context_).map(entries)); }

    Term version(Version version) const {
        const std::array elements{integer(version.major_), integer(version.minor_)};
        return tuple(elements);
    }

  private:
    // The process that owns the built terms.
    ProcessContext &context_;
};

// The hello being decoded: its message, the version its extensions are read for and whether the current extension is
// the last one.
struct Hello {
    Message message_ = Message::client_hello;
    Version version_;
    bool last_ = false;
};

// A signature scheme: its code, OTP's name and the {Hash, Sign} it is in a TLS 1.2 hello (no sign: left out).
struct Scheme {
    std::uint16_t code_;
    std::string_view name_;
    std::string_view hash_;
    std::string_view sign_;
};

constexpr std::array<Scheme, 34> SCHEMES{{
    {0x0401, "rsa_pkcs1_sha256", "sha256", "rsa"},
    {0x0501, "rsa_pkcs1_sha384", "sha384", "rsa"},
    {0x0601, "rsa_pkcs1_sha512", "sha512", "rsa"},
    {0x0403, "ecdsa_secp256r1_sha256", "sha256", "ecdsa"},
    {0x0503, "ecdsa_secp384r1_sha384", "sha384", "ecdsa"},
    {0x0603, "ecdsa_secp521r1_sha512", "sha512", "ecdsa"},
    {0x081A, "ecdsa_brainpoolP256r1tls13_sha256", "sha256", "ecdsa"},
    {0x081B, "ecdsa_brainpoolP384r1tls13_sha384", "sha384", "ecdsa"},
    {0x081C, "ecdsa_brainpoolP512r1tls13_sha512", "sha512", "ecdsa"},
    {0x0804, "rsa_pss_rsae_sha256", "sha256", "rsa_pss_rsae"},
    {0x0805, "rsa_pss_rsae_sha384", "sha384", "rsa_pss_rsae"},
    {0x0806, "rsa_pss_rsae_sha512", "sha512", "rsa_pss_rsae"},
    {0x0807, "eddsa_ed25519", "", ""},
    {0x0808, "eddsa_ed448", "", ""},
    {0x0809, "rsa_pss_pss_sha256", "sha256", "rsa_pss_pss"},
    {0x080A, "rsa_pss_pss_sha384", "sha384", "rsa_pss_pss"},
    {0x080B, "rsa_pss_pss_sha512", "sha512", "rsa_pss_pss"},
    {0x0201, "rsa_pkcs1_sha1", "sha", "rsa"},
    {0x0203, "ecdsa_sha1", "sha", "ecdsa"},
    {0x0904, "mldsa44", "", ""},
    {0x0905, "mldsa65", "", ""},
    {0x0906, "mldsa87", "", ""},
    {0x0911, "slh_dsa_sha2_128s", "", ""},
    {0x0912, "slh_dsa_sha2_128f", "", ""},
    {0x0913, "slh_dsa_sha2_192s", "", ""},
    {0x0914, "slh_dsa_sha2_192f", "", ""},
    {0x0915, "slh_dsa_sha2_256s", "", ""},
    {0x0916, "slh_dsa_sha2_256f", "", ""},
    {0x0917, "slh_dsa_shake_128s", "", ""},
    {0x0918, "slh_dsa_shake_128f", "", ""},
    {0x0919, "slh_dsa_shake_192s", "", ""},
    {0x091A, "slh_dsa_shake_192f", "", ""},
    {0x091B, "slh_dsa_shake_256s", "", ""},
    {0x091C, "slh_dsa_shake_256f", "", ""},
}};

constexpr std::array<std::string_view, 7> HASHES{"null", "md5", "sha", "sha224", "sha256", "sha384", "sha512"};
constexpr std::array<std::string_view, 4> SIGNS{"anon", "rsa", "dsa", "ecdsa"};
// Codes from here on are private use and kept as integers; codes below and past the named ones are unassigned.
constexpr unsigned PRIVATE_USE = 224;

// A named group of TLS 1.3 and its code.
struct Group {
    std::uint16_t code_;
    std::string_view name_;
};

constexpr std::array<Group, 19> GROUPS{{
    {0x0017, "secp256r1"},
    {0x0018, "secp384r1"},
    {0x0019, "secp521r1"},
    {0x001D, "x25519"},
    {0x001E, "x448"},
    {0x001F, "brainpoolP256r1tls13"},
    {0x0020, "brainpoolP384r1tls13"},
    {0x0021, "brainpoolP512r1tls13"},
    {0x0200, "mlkem512"},
    {0x0201, "mlkem768"},
    {0x0202, "mlkem1024"},
    {0x11EC, "x25519mlkem768"},
    {0x11EB, "secp256r1mlkem768"},
    {0x11ED, "secp384r1mlkem1024"},
    {0x0100, "ffdhe2048"},
    {0x0101, "ffdhe3072"},
    {0x0102, "ffdhe4096"},
    {0x0103, "ffdhe6144"},
    {0x0104, "ffdhe8192"},
}};

// The OIDs of the elliptic curves of TLS 1.2 hellos, codes 1 to 30 (tls_v1:enum_to_oid/1).
constexpr std::array<std::string_view, 30> CURVES{
    "1.3.132.0.1",          "1.3.132.0.2",           "1.3.132.0.15",          "1.3.132.0.24",        "1.3.132.0.25",
    "1.3.132.0.26",         "1.3.132.0.27",          "1.3.132.0.3",           "1.3.132.0.16",        "1.3.132.0.17",
    "1.3.132.0.36",         "1.3.132.0.37",          "1.3.132.0.38",          "1.3.132.0.39",        "1.3.132.0.9",
    "1.3.132.0.8",          "1.3.132.0.30",          "1.3.132.0.31",          "1.2.840.10045.3.1.1", "1.3.132.0.32",
    "1.3.132.0.33",         "1.3.132.0.10",          "1.2.840.10045.3.1.7",   "1.3.132.0.34",        "1.3.132.0.35",
    "1.3.36.3.3.2.8.1.1.7", "1.3.36.3.3.2.8.1.1.11", "1.3.36.3.3.2.8.1.1.13", "1.3.101.110",         "1.3.101.111",
};

// The split of a hybrid key share: the sizes of its two parts, by group and direction.
struct HybridShare {
    std::string_view group_;
    std::size_t client_first_;
    std::size_t client_second_;
    std::size_t server_first_;
    std::size_t server_second_;
};

constexpr std::array<HybridShare, 3> HYBRID_SHARES{{
    {"x25519mlkem768", 1184, 32, 1088, 32},
    {"secp256r1mlkem768", 65, 1184, 65, 1088},
    {"secp384r1mlkem1024", 97, 1568, 97, 1568},
}};

// A hash or sign algorithm code: its name, the integer of a private one, or unassigned.
Term algorithm(const Builder &build, unsigned code, std::span<const std::string_view> names) {
    if (code < names.size()) {
        return build.atom(names[code]);
    }
    return code >= PRIVATE_USE ? build.integer(code) : build.atom("unassigned");
}

bool assigned(unsigned code, std::span<const std::string_view> names) {
    return code < names.size() || code >= PRIVATE_USE;
}

// {Hash, Sign} of a code outside the scheme table, none when either part is unassigned.
std::optional<Term> legacy_pair(const Builder &build, unsigned code) {
    const auto hash = code >> 8U;
    const auto sign = code & 0xFFU;
    if (!assigned(hash, HASHES) || !assigned(sign, SIGNS)) {
        return std::nullopt;
    }
    const std::array elements{algorithm(build, hash, HASHES), algorithm(build, sign, SIGNS)};
    return build.tuple(elements);
}

const Scheme *scheme_of(unsigned code) {
    const auto found = std::ranges::find(SCHEMES, code, &Scheme::code_);
    return found == SCHEMES.end() ? nullptr : &*found;
}

// A scheme of a TLS 1.3 list: its name, else its {Hash, Sign} (ssl_cipher:signature_scheme/1).
std::optional<Term> scheme_13(const Builder &build, unsigned code) {
    const auto *scheme = scheme_of(code);
    return scheme != nullptr ? std::optional(build.atom(scheme->name_)) : legacy_pair(build, code);
}

// A scheme of a TLS 1.2 list: {Hash, Sign} of a named scheme with a TLS 1.2 form, else its {Hash, Sign}.
std::optional<Term> scheme_12(const Builder &build, unsigned code) {
    const auto *scheme = scheme_of(code);
    if (scheme == nullptr) {
        return legacy_pair(build, code);
    }
    if (scheme->sign_.empty()) {
        return std::nullopt;
    }
    const std::array elements{build.atom(scheme->hash_), build.atom(scheme->sign_)};
    return build.tuple(elements);
}

// The terms of each 16-bit code of `bytes` that `decode` keeps; a trailing odd byte is ignored.
template <typename Decode> Term code_list(const Builder &build, std::span<const std::byte> bytes, Decode decode) {
    std::vector<Term> terms;
    Reader reader(bytes);
    while (reader.rest().size() >= 2) {
        if (auto term = decode(build, reader.number(2))) {
            terms.push_back(*term);
        }
    }
    return build.list(terms);
}

std::optional<Term> group_atom(const Builder &build, unsigned code) {
    const auto found = std::ranges::find(GROUPS, code, &Group::code_);
    return found == GROUPS.end() ? std::nullopt : std::optional(build.atom(found->name_));
}

// The OID tuple of a TLS 1.2 curve code.
std::optional<Term> curve_oid(const Builder &build, unsigned code) {
    if (code == 0 || code > CURVES.size()) {
        return std::nullopt;
    }
    std::vector<Term> arcs;
    std::int64_t arc = 0;
    for (const auto digit : CURVES.at(code - 1)) {
        if (digit == '.') {
            arcs.push_back(build.integer(arc));
            arc = 0;
        } else {
            arc = arc * 10 + (digit - '0');
        }
    }
    arcs.push_back(build.integer(arc));
    return build.tuple(arcs);
}

// A decoded extension: its key and value, none for an extension decoded as unknown (left out).
struct Decoded {
    std::string_view key_;
    std::optional<Term> value_;
};

using Decoding = std::optional<Decoded>;

bool from_client(const Hello &hello) { return hello.message_ == Message::client_hello; }

// The bytes of a whole vector filling `data`, else a DecodeError (OTP's exact matches).
std::span<const std::byte> whole_vector(std::span<const std::byte> data, std::size_t size) {
    Reader reader(data);
    const auto vector = reader.vector(size);
    if (!reader.empty()) {
        throw DecodeError{};
    }
    return vector;
}

Decoding alpn(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    Reader reader(data);
    if (data.size() < 2 || reader.number(2) + 2 != data.size()) {
        return std::nullopt;
    }
    return Decoded{"alpn", build.binary(reader.rest())};
}

Decoding next_protocol(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    return Decoded{"next_protocol_negotiation", build.binary(data)};
}

Decoding renegotiation(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    if (data.size() == 1) {
        return Decoded{"renegotiation_info", build.binary(data)};
    }
    return Decoded{"renegotiation_info", build.binary(whole_vector(data, 1))};
}

Decoding srp(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    Reader reader(data);
    if (!from_client(hello) || data.empty() || reader.number(1) + 1 != data.size()) {
        return std::nullopt;
    }
    return Decoded{"srp", build.binary(reader.rest())};
}

Decoding signature_algs(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    if (!from_client(hello) || hello.version_ > TLS_1_3) {
        return std::nullopt;
    }
    const auto list = whole_vector(data, 2);
    if (hello.version_ == TLS_1_3) {
        return Decoded{"signature_algs", code_list(build, list, scheme_13)};
    }
    if (hello.version_ == TLS_1_2) {
        return Decoded{"signature_algs", code_list(build, list, scheme_12)};
    }
    // Before TLS 1.2 each pair is kept with its unassigned parts.
    std::vector<Term> pairs;
    for (Reader reader(list); reader.rest().size() >= 2;) {
        const std::array pair{algorithm(build, reader.number(1), HASHES), algorithm(build, reader.number(1), SIGNS)};
        pairs.push_back(build.tuple(pair));
    }
    return Decoded{"signature_algs", build.list(pairs)};
}

Decoding signature_algs_cert(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    if (!from_client(hello)) {
        return std::nullopt;
    }
    return Decoded{"signature_algs_cert", code_list(build, whole_vector(data, 2), scheme_13)};
}

Decoding use_srtp(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    Reader reader(data);
    const auto profiles = reader.vector(2);
    const auto mki = reader.vector(1);
    if (!reader.empty()) {
        throw DecodeError{};
    }
    std::vector<Term> list;
    for (Reader each(profiles); each.rest().size() >= 2;) {
        list.push_back(build.binary(each.take(2)));
    }
    const std::array<std::pair<Term, Term>, 2> entries{{
        {build.atom("protection_profiles"), build.list(list)},
        {build.atom("mki"), build.binary(mki)},
    }};
    return Decoded{"use_srtp", build.map(entries)};
}

Decoding elliptic_curves(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    Reader reader(data);
    if (hello.version_ < TLS_1_3) {
        reader.take(2);
        return Decoded{"elliptic_curves", code_list(build, reader.rest(), curve_oid)};
    }
    if (hello.version_ != TLS_1_3 || !from_client(hello)) {
        return std::nullopt;
    }
    reader.take(2);
    return Decoded{"elliptic_curves", code_list(build, reader.rest(), group_atom)};
}

Decoding ec_point_formats(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    Reader reader(data);
    reader.take(1);
    std::vector<Term> formats;
    for (const auto byte : reader.rest()) {
        formats.push_back(build.integer(std::to_integer<unsigned>(byte)));
    }
    return Decoded{"ec_point_formats", build.list(formats)};
}

// The first host name of a server_name list, else undefined (also for a malformed entry, as OTP's dec_sni/1).
std::optional<Term> host_name(const Builder &build, Reader names) {
    while (names.rest().size() >= 3) {
        const auto type = names.number(1);
        const auto length = names.number(2);
        if (length > names.rest().size()) {
            return std::nullopt;
        }
        const auto name = names.take(length);
        if (type == 0) {
            std::vector<Term> text;
            for (const auto byte : name) {
                text.push_back(build.integer(std::to_integer<unsigned>(byte)));
            }
            return build.list(text);
        }
    }
    return std::nullopt;
}

Decoding sni(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    if (data.empty() && !from_client(hello)) {
        return Decoded{"sni", build.list({})};
    }
    Reader reader(data);
    reader.take(2);
    return Decoded{"sni", host_name(build, reader)};
}

Decoding max_fragment_length(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    if (data.size() != 1) {
        return std::nullopt;
    }
    return Decoded{"max_frag_enum", build.integer(std::to_integer<unsigned>(data.front()))};
}

// The versions of a supported_versions list (an odd byte fails, as OTP's decode_versions/2).
std::vector<Version> versions_of(std::span<const std::byte> bytes) {
    if (bytes.size() % 2 != 0) {
        throw DecodeError{};
    }
    std::vector<Version> versions;
    for (Reader reader(bytes); !reader.empty();) {
        const auto major = reader.number(1);
        versions.push_back({major, reader.number(1)});
    }
    return versions;
}

Decoding supported_versions(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    if (data.size() == 2) {
        return Decoded{"server_hello_selected_version",
                       build.version({std::to_integer<unsigned>(data[0]), std::to_integer<unsigned>(data[1])})};
    }
    if (data.size() < 2) {
        return std::nullopt;
    }
    std::vector<Term> versions;
    for (const auto version : versions_of(data.subspan(1))) {
        versions.push_back(build.version(version));
    }
    return Decoded{"client_hello_versions", build.list(versions)};
}

// A key share's exchange: a hybrid group's two parts as a pair, else the bytes.
Term key_exchange(const Builder &build, std::string_view group, std::span<const std::byte> bytes, bool client) {
    const auto found = std::ranges::find(HYBRID_SHARES, group, &HybridShare::group_);
    if (found != HYBRID_SHARES.end()) {
        const auto first = client ? found->client_first_ : found->server_first_;
        const auto second = client ? found->client_second_ : found->server_second_;
        if (bytes.size() == first + second) {
            const std::array parts{build.binary(bytes.first(first)), build.binary(bytes.subspan(first))};
            return build.tuple(parts);
        }
    }
    return build.binary(bytes);
}

Term share_entry(const Builder &build, unsigned code, std::span<const std::byte> bytes, bool client) {
    const auto group = group_atom(build, code);
    const auto name = group ? group->atom_spelling_unchecked() : std::string_view{};
    const std::array elements{build.atom("key_share_entry"), group.value_or(build.atom("undefined")),
                              key_exchange(build, name, bytes, client)};
    return build.tuple(elements);
}

Decoding key_share(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    Reader reader(data);
    if (!from_client(hello)) {
        const auto code = reader.number(2);
        const auto entry = share_entry(build, code, whole_vector(reader.rest(), 2), false);
        return Decoded{"key_share", entry};
    }
    std::vector<Term> entries;
    reader.take(2);
    while (!reader.empty()) {
        const auto code = reader.number(2);
        const auto bytes = reader.vector(2);
        if (group_atom(build, code)) {
            entries.push_back(share_entry(build, code, bytes, true));
        }
    }
    return Decoded{"key_share", build.list(entries)};
}

Decoding psk_modes(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    if (!from_client(hello)) {
        return std::nullopt;
    }
    constexpr std::array<std::string_view, 2> MODES{"psk_ke", "psk_dhe_ke"};
    std::vector<Term> modes;
    for (const auto byte : whole_vector(data, 1)) {
        const auto mode = std::to_integer<unsigned>(byte);
        if (mode < MODES.size()) {
            modes.push_back(build.atom(MODES.at(mode)));
        }
    }
    return Decoded{"psk_key_exchange_modes", build.list(modes)};
}

// {offered_psks, [{psk_identity, Identity, Age}], [Binder]} of a ClientHello's pre_shared_key.
Term offered_psks(const Builder &build, Reader reader) {
    std::vector<Term> identities;
    for (Reader each(reader.vector(2)); !each.empty();) {
        const auto identity = build.binary(each.vector(2));
        const std::array fields{build.atom("psk_identity"), identity, build.integer(each.number(4))};
        identities.push_back(build.tuple(fields));
    }
    std::vector<Term> binders;
    for (Reader each(reader.vector(2)); !each.empty();) {
        binders.push_back(build.binary(each.vector(1)));
    }
    if (!reader.empty()) {
        throw DecodeError{};
    }
    const std::array fields{build.atom("offered_psks"), build.list(identities), build.list(binders)};
    return build.tuple(fields);
}

Decoding pre_shared_key(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    if (!from_client(hello)) {
        if (data.size() != 2) {
            throw DecodeError{};
        }
        return Decoded{"pre_shared_key", build.integer(Reader(data).number(2))};
    }
    const auto psks = offered_psks(build, Reader(data));
    // It must be the last extension (illegal_parameter in OTP).
    if (!hello.last_) {
        throw DecodeError{};
    }
    return Decoded{"pre_shared_key", psks};
}

Decoding cookie(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    Reader reader(data);
    if (!from_client(hello) || data.size() < 2 || reader.number(2) + 2 != data.size()) {
        return std::nullopt;
    }
    return Decoded{"cookie", build.binary(reader.rest())};
}

Decoding status_request(const Builder &build, std::span<const std::byte> data, const Hello & /*hello*/) {
    if (data.empty()) {
        return Decoded{"status_request", std::nullopt};
    }
    Reader reader(data);
    if (data.size() < 4 || reader.number(1) != 1 || reader.number(3) + 4 != data.size()) {
        return std::nullopt;
    }
    const std::array fields{build.atom("certificate_status"), build.atom("undefined"), build.binary(reader.rest())};
    return Decoded{"status_request", build.tuple(fields)};
}

Decoding early_data(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    if (!data.empty() || !from_client(hello)) {
        return std::nullopt;
    }
    return Decoded{"early_data", build.list({})};
}

Decoding certificate_authorities(const Builder &build, std::span<const std::byte> data, const Hello &hello) {
    if (!from_client(hello)) {
        return std::nullopt;
    }
    std::vector<Term> names;
    for (Reader reader(whole_vector(data, 2)); !reader.empty();) {
        names.push_back(build.binary(reader.vector(2)));
    }
    return Decoded{"certificate_authorities", build.list(names)};
}

// The extensions OTP decodes, by code.
struct Extension {
    std::uint16_t code_;
    Decoding (*decode_)(const Builder &, std::span<const std::byte>, const Hello &);
};

constexpr std::array<Extension, 19> EXTENSIONS{{
    {16, alpn},
    {13172, next_protocol},
    {0xFF01, renegotiation},
    {12, srp},
    {13, signature_algs},
    {50, signature_algs_cert},
    {14, use_srtp},
    {10, elliptic_curves},
    {11, ec_point_formats},
    {0, sni},
    {1, max_fragment_length},
    {43, supported_versions},
    {51, key_share},
    {45, psk_modes},
    {41, pre_shared_key},
    {44, cookie},
    {5, status_request},
    {42, early_data},
    {47, certificate_authorities},
}};

// The keys OTP's empty_extensions/2 starts a hello's map with.
std::vector<std::string_view> empty_keys(Version version, Message message) {
    if (message == Message::server_hello) {
        if (version == TLS_1_3) {
            return {"key_share", "pre_shared_key", "server_hello_selected_version"};
        }
        return {"renegotiation_info", "alpn", "next_protocol_negotiation", "ec_point_formats"};
    }
    if (version == TLS_1_3) {
        return {"sni",
                "max_frag_enum",
                "status_request",
                "elliptic_curves",
                "signature_algs",
                "use_srtp",
                "alpn",
                "key_share",
                "pre_shared_key",
                "psk_key_exchange_modes",
                "early_data",
                "cookie",
                "client_hello_versions",
                "certificate_authorities",
                "signature_algs_cert"};
    }
    std::vector<std::string_view> keys{
        "renegotiation_info", "alpn", "next_protocol_negotiation", "srp", "ec_point_formats", "elliptic_curves", "sni"};
    if (version == TLS_1_2) {
        keys.emplace_back("signature_algs");
    }
    return keys;
}

// The decoded map: each key with its value, none for undefined.
class Extensions {
  public:
    explicit Extensions(const std::vector<std::string_view> &keys) {
        for (const auto key : keys) {
            entries_.emplace_back(key, std::nullopt);
        }
    }

    // Add a decoded extension; a second one with a value is a DecodeError (OTP's duplicate_extension alert).
    void add(const Decoded &decoded) {
        const auto found = std::ranges::find(entries_, decoded.key_, &Entry::first);
        if (found == entries_.end()) {
            entries_.emplace_back(decoded.key_, decoded.value_);
            return;
        }
        if (found->second) {
            throw DecodeError{};
        }
        found->second = decoded.value_;
    }

    Term term(const Builder &build) const {
        std::vector<std::pair<Term, Term>> pairs;
        pairs.reserve(entries_.size());
        for (const auto &[key, value] : entries_) {
            pairs.emplace_back(build.atom(key), value.value_or(build.atom("undefined")));
        }
        return build.map(pairs);
    }

  private:
    using Entry = std::pair<std::string_view, std::optional<Term>>;
    // The keys in their first order.
    std::vector<Entry> entries_;
};

// The version extensions are read for: the first supported version (a ServerHello's selected one), else the hello's
// version, and never above the local one (process_supported_versions_extension/3).
Version read_version(std::span<const std::byte> extensions, Version local, Version legacy) {
    for (Reader reader(extensions); reader.rest().size() >= 4;) {
        const auto code = reader.number(2);
        const auto length = reader.number(2);
        if (length > reader.rest().size()) {
            break;
        }
        const auto data = reader.take(length);
        if (code == 43 && data.size() >= 2) {
            const auto selected = data.size() == 2 ? versions_of(data).front() : versions_of(data.subspan(1)).front();
            return std::min(selected, local);
        }
    }
    return std::min(legacy, local);
}

// Decode the extension list; a malformed tail ends it as OTP's last decode_extensions/4 clause does.
Term decode_extensions(const Builder &build, std::span<const std::byte> bytes, Hello hello) {
    Extensions extensions(empty_keys(hello.version_, hello.message_));
    Reader reader(bytes);
    while (reader.rest().size() >= 4) {
        const auto code = reader.number(2);
        const auto length = reader.number(2);
        if (length > reader.rest().size()) {
            break;
        }
        const auto data = reader.take(length);
        hello.last_ = reader.empty();
        const auto found = std::ranges::find(EXTENSIONS, code, &Extension::code_);
        if (found == EXTENSIONS.end()) {
            continue;
        }
        if (const auto decoded = found->decode_(build, data, hello)) {
            extensions.add(*decoded);
        }
    }
    return extensions.term(build);
}

// The extensions of a hello body after its version: a ClientHello's vector, else a ServerHello's optional one.
Term hello_body(const Builder &build, Reader body, Message message, Version local) {
    const Version legacy{body.number(1), body.number(1)};
    body.take(32);
    body.vector(1);
    if (message == Message::client_hello) {
        body.vector(2);
        body.vector(1);
    } else {
        body.take(2);
        // Only the null compression method (illegal_parameter in OTP).
        if (body.number(1) != 0) {
            throw DecodeError{};
        }
    }
    if (body.empty()) {
        const auto version = message == Message::client_hello ? std::min(legacy, local) : local;
        return Extensions(empty_keys(version, message)).term(build);
    }
    const auto extensions = whole_vector(body.rest(), 2);
    return decode_extensions(build, extensions, {message, read_version(extensions, local, legacy), false});
}
} // namespace

Term hello_extensions(ProcessContext &context, const BinaryArgument &message, const TupleArgument &local) {
    const auto major = local.elements.size() == 2 ? small(local.elements[0].word()) : std::nullopt;
    const auto minor = local.elements.size() == 2 ? small(local.elements[1].word()) : std::nullopt;
    if (!major || !minor) {
        bad_argument();
    }
    const Builder build(context);
    try {
        Reader reader(message.bytes);
        const auto type = reader.number(1);
        const auto body = whole_vector(reader.rest(), 3);
        if (type != 1 && type != 2) {
            throw DecodeError{};
        }
        const auto kind = type == 1 ? Message::client_hello : Message::server_hello;
        const Version version{static_cast<unsigned>(*major), static_cast<unsigned>(*minor)};
        return hello_body(build, Reader(body), kind, version);
    } catch (const DecodeError &) {
        return build.atom("error");
    }
}
} // namespace clause::runtime::builtins::ssl
