#include "public_key.hpp"
#include <array>
#include <initializer_list>

// Added for public_key (plan 21 step LIB80B): public_key's tables of object identifiers (public key algorithms, curve
// field types, named curves, the ML-DSA, ML-KEM and SLH-DSA parameter sets, certificate extensions) and of the
// generated module that defines each type der_decode/2 takes.
namespace clause::runtime::builtins::public_key {
namespace {
// An object identifier of at most 10 arcs and the name a table gives it.
struct OidName {
    std::initializer_list<std::uint32_t> oid_;
    std::string_view name_;
};

struct TypeModule {
    std::string_view type_;
    std::string_view module_;
};

#include "tables.inc"

// The rows of a table by its name.
std::span<const OidName> rows(const Term &table) {
    static const std::array<std::pair<std::string_view, std::span<const OidName>>, 7> TABLES{{
        {"public_key_algorithm", PUBLIC_KEY_ALGORITHMS},
        {"curve_type", CURVE_TYPES},
        {"named_curve", NAMED_CURVES},
        {"ml_dsa", ML_DSA_ALGORITHMS},
        {"ml_kem", ML_KEM_ALGORITHMS},
        {"slh_dsa", SLH_DSA_ALGORITHMS},
        {"extension", EXTENSIONS},
    }};
    if (table.is_atom()) {
        for (const auto &[name, entries] : TABLES) {
            if (table.atom_spelling_unchecked() == name) {
                return entries;
            }
        }
    }
    bad_argument();
}
} // namespace

bool same_oid(const Term &oid, const std::initializer_list<std::uint32_t> &arcs) {
    if (!oid.is_tuple() || oid.tuple_size_unchecked() != arcs.size()) {
        return false;
    }
    std::size_t i = 0;
    for (const auto arc : arcs) {
        const auto element = oid.tuple_element_unchecked(i++);
        if (!element.is_integer() || small(element.word()) != static_cast<std::int64_t>(arc)) {
            return false;
        }
    }
    return true;
}

Term oid_term(asn1::Builder &builder, const std::initializer_list<std::uint32_t> &arcs) {
    std::vector<Term> elements;
    for (const auto arc : arcs) {
        elements.push_back(builder.integer(arc));
    }
    return builder.tuple(elements);
}

bool is_string(const Term &term, const std::string_view text) {
    auto rest = term;
    for (const auto c : text) {
        if (!rest.is_cons() || small(rest.head_unchecked().word()) != static_cast<unsigned char>(c)) {
            return false;
        }
        rest = rest.tail_unchecked();
    }
    return rest.is_nil();
}

Term oid_name(ProcessContext &context, const Named<TableRole> &table, const Named<KeyRole> &oid) {
    asn1::Builder builder(context);
    for (const auto &row : rows(table.term)) {
        if (same_oid(oid.term, row.oid_)) {
            return builder.atom(row.name_);
        }
    }
    return builder.atom("undefined");
}

Term name_oid(ProcessContext &context, const Named<TableRole> &table, const Named<KeyRole> &name) {
    asn1::Builder builder(context);
    for (const auto &row : rows(table.term)) {
        if (name.term.is_atom() && name.term.atom_spelling_unchecked() == row.name_) {
            return oid_term(builder, row.oid_);
        }
    }
    return builder.atom("undefined");
}

Term asn1_module(ProcessContext &context, const Named<KeyRole> &type) {
    asn1::Builder builder(context);
    for (const auto &row : TYPE_MODULES) {
        if (type.term.is_atom() && type.term.atom_spelling_unchecked() == row.type_) {
            return builder.atom(row.module_);
        }
    }
    return builder.atom("undefined");
}
} // namespace clause::runtime::builtins::public_key
