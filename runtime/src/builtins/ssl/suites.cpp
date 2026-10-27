#include "../text.hpp"
#include "ssl.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

// Added for ssl (plan 21 step LIB81A): OTP's cipher suites and the lists ssl:cipher_suites/2,3 return.
namespace clause::runtime::builtins::ssl {
namespace {
// One suite: its two-byte code and the spellings of its map's parts.
struct SuiteRow {
    std::uint16_t code_;
    std::string_view key_exchange_;
    std::string_view cipher_;
    std::string_view mac_;
    std::string_view prf_;
};

// One list of a version and description: its suites' indices in SUITE_LIST_INDICES.
struct SuiteList {
    std::string_view version_;
    std::string_view description_;
    std::uint16_t offset_;
    std::uint16_t count_;
};

#include "suites.inc"

// The parts ssl_cipher_format's maps use that no listed suite has.
constexpr std::array<std::string_view, 4> UNLISTED_PARTS{"null", "dh_dss", "dh_rsa", "psk_dhe"};

constexpr std::array<std::string_view, 6> VERSIONS{"tlsv1.3", "tlsv1.2", "tlsv1.1", "tlsv1", "dtlsv1.2", "dtlsv1"};

// The spelling of an atom among `names`; function_clause for any other term.
std::string_view one_of(const Term &term, std::span<const std::string_view> names) {
    if (term.is_atom()) {
        const auto spelling = term.atom_spelling_unchecked();
        if (std::ranges::find(names, spelling) != names.end()) {
            return spelling;
        }
    }
    function_clause();
}

// The suites of a description's list for a version, checked in OTP's order: the version first.
std::span<const std::uint8_t> listed(const Named<DescriptionRole> &description_name,
                                     const Named<VersionRole> &version_name) {
    const auto &description = description_name.term;
    const auto version = one_of(version_name.term, VERSIONS);
    if (description.is_atom()) {
        for (const auto &list : SUITE_LISTS) {
            if (list.version_ == version && list.description_ == description.atom_spelling_unchecked()) {
                return std::span{SUITE_LIST_INDICES}.subspan(list.offset_, list.count_);
            }
        }
    }
    function_clause();
}

Term suite_map(ProcessContext &context, const SuiteRow &row) {
    const std::array<std::pair<Term, Term>, 4> entries{{
        {atom(context, "key_exchange"_atom), atom(context, AtomName(row.key_exchange_))},
        {atom(context, "cipher"_atom), atom(context, AtomName(row.cipher_))},
        {atom(context, "mac"_atom), atom(context, AtomName(row.mac_))},
        {atom(context, "prf"_atom), atom(context, AtomName(row.prf_))},
    }};
    return need(TermFactory(context).map(entries));
}

SuitePart part(std::string_view spelling) { return {.present_ = true, .atom_ = code_points(spelling)}; }

SuiteParts row_parts(const SuiteRow &row) {
    return {part(row.key_exchange_), part(row.cipher_), part(row.mac_), part(row.prf_)};
}
} // namespace

bool known_suite_part(std::string_view spelling) {
    const auto in_row = [spelling](const SuiteRow &row) {
        return row.key_exchange_ == spelling || row.cipher_ == spelling || row.mac_ == spelling || row.prf_ == spelling;
    };
    return std::ranges::any_of(SUITES, in_row) || std::ranges::find(UNLISTED_PARTS, spelling) != UNLISTED_PARTS.end();
}

Term string_term(ProcessContext &context, std::u32string_view text) {
    TermFactory factory(context);
    std::vector<Term> characters;
    characters.reserve(text.size());
    for (const auto c : text) {
        characters.push_back(need(factory.integer(c)));
    }
    return need(factory.list(characters));
}

void function_clause() { throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause}; }

Term cipher_suites(ProcessContext &context, const Named<DescriptionRole> &description,
                   const Named<VersionRole> &version) {
    std::vector<Term> suites;
    for (const auto index : listed(description, version)) {
        suites.push_back(suite_map(context, SUITES.at(index)));
    }
    return need(TermFactory(context).list(suites));
}

Term cipher_suite_names(ProcessContext &context, const Named<DescriptionRole> &description,
                        const Named<VersionRole> &version, const Named<StringTypeRole> &type) {
    one_of(version.term, VERSIONS);
    constexpr std::array<std::string_view, 2> TYPES{"rfc", "openssl"};
    const bool rfc = one_of(type.term, TYPES) == "rfc";
    std::vector<Term> names;
    for (const auto index : listed(description, version)) {
        const auto parts = row_parts(SUITES.at(index));
        names.push_back(string_term(context, rfc ? rfc_name(parts) : openssl_name(parts)));
    }
    return need(TermFactory(context).list(names));
}
} // namespace clause::runtime::builtins::ssl
