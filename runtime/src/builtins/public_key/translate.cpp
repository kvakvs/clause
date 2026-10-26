#include "public_key.hpp"
#include <array>
#include <span>

// Added for public_key (plan 21 step LIB80B): pubkey_translation's walk between the records the generated ASN.1
// modules use and those public_key documents (SubjectPublicKeyInfo's algorithm, DSA parameters and signatures,
// attributes, OneAsymmetricKey and PrivateKeyInfo, extensions, the encryption algorithm of an encrypted key).
namespace clause::runtime::builtins::public_key {
using asn1::erlang_error;

namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// A tuple whose first element is `name`, of `size` elements.
bool record_is(const Term &term, const std::string_view name, const std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && atom_is(term.tuple_element_unchecked(0), name);
}

Term element(const Term &tuple, const std::size_t index) { return tuple.tuple_element_unchecked(index); }

// A record the walk only renames: its name and size, and the name it gets.
struct Rename {
    std::string_view from_;
    std::size_t size_;
    std::string_view to_;
};

constexpr std::array<Rename, 3> DECODE_RENAMES{{
    {"DSA-Sig-Value", 3, "Dss-Sig-Value"},
    {"OTPExtension", 4, "Extension"},
    {"EncryptionAlgorithmIdentifier", 3, "EncryptedPrivateKeyInfo_encryptionAlgorithm"},
}};

constexpr std::array<Rename, 3> ENCODE_RENAMES{{
    {"PrivateKeyInfo", 6, "OneAsymmetricKey"},
    {"Dss-Sig-Value", 3, "DSA-Sig-Value"},
    {"EncryptedPrivateKeyInfo_encryptionAlgorithm", 3, "EncryptionAlgorithmIdentifier"},
}};

// is_simple_tuple/1: tuples the walk leaves as they are (element/2 of an empty tuple fails with badarg).
bool simple(const Term &tuple) {
    if (record_is(tuple, "Extension", 4) && element(tuple, 3).is_binary()) {
        return true;
    }
    if (tuple.tuple_size_unchecked() == 0) {
        erlang_error(abi::v1::ErrorReason::badarg);
    }
    const auto first = element(tuple, 0);
    return first.is_integer() || atom_is(first, "asn1_OPENTYPE") || atom_is(first, "RSAPublicKey") ||
           atom_is(first, "utcTime");
}

class Walk {
  public:
    Walk(asn1::Builder &builder, const bool decoding) : builder_(builder), decoding_(decoding) {}

    Term term(const Term &value) {
        if (value.is_tuple()) {
            const auto special = decoding_ ? decoded(value) : encoded(value);
            return special ? *special : elements(value);
        }
        return value.is_list() ? items(value) : value;
    }

  private:
    Term rename(const Term &tuple, const std::string_view name) {
        std::vector<Term> fields{builder_.atom(name)};
        for (std::size_t i = 1; i < tuple.tuple_size_unchecked(); ++i) {
            fields.push_back(element(tuple, i));
        }
        return builder_.tuple(fields);
    }

    // {Name, P, Q, G} with the walk applied to none of them.
    Term triple(const std::string_view name, const Term &from, const std::size_t first) {
        return builder_.tuple(
            {builder_.atom(name), element(from, first), element(from, first + 1), element(from, first + 2)});
    }

    // The record renamed by the first row of `renames` that matches it.
    std::optional<Term> renamed(const Term &value, const std::span<const Rename> renames) {
        for (const auto &rename_row : renames) {
            if (record_is(value, rename_row.from_, rename_row.size_)) {
                return rename(value, rename_row.to_);
            }
        }
        return std::nullopt;
    }

    std::optional<Term> decoded(const Term &value);
    std::optional<Term> encoded(const Term &value);
    std::optional<Term> key_info(const Term &value, const Rename &names);

    Term elements(const Term &tuple) {
        if (simple(tuple)) {
            return tuple;
        }
        std::vector<Term> fields;
        fields.reserve(tuple.tuple_size_unchecked());
        for (std::size_t i = 0; i < tuple.tuple_size_unchecked(); ++i) {
            fields.push_back(term(element(tuple, i)));
        }
        return builder_.tuple(fields);
    }

    // [decode(E) || E <- List]: a list that is not proper fails as the comprehension does.
    Term items(const Term &list) {
        std::vector<Term> values;
        auto rest = list;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            values.push_back(term(rest.head_unchecked()));
        }
        if (!rest.is_nil()) {
            erlang_error(abi::v1::ErrorReason::bad_generator, &rest);
        }
        return builder_.list(values);
    }

    asn1::Builder &builder_;
    bool decoding_;
};

// SubjectPublicKeyInfo: its algorithm record renamed (the decoder's SubjectPublicKeyInfo_algorithm is documented as
// AlgorithmIdentifier), the algorithm and parameters walked, the key kept.
std::optional<Term> Walk::key_info(const Term &value, const Rename &names) {
    const auto algorithm = element(value, 1);
    if (!record_is(algorithm, names.from_, names.size_)) {
        erlang_error(abi::v1::ErrorReason::badmatch, &algorithm);
    }
    const auto renamed =
        builder_.tuple({builder_.atom(names.to_), term(element(algorithm, 1)), term(element(algorithm, 2))});
    return builder_.tuple({element(value, 0), renamed, element(value, 2)});
}

std::optional<Term> Walk::decoded(const Term &value) {
    if (record_is(value, "SubjectPublicKeyInfo", 3)) {
        return key_info(value, {"SubjectPublicKeyInfo_algorithm", 3, "AlgorithmIdentifier"});
    }
    if (record_is(value, "DSA-Params", 4)) {
        return builder_.tuple({builder_.atom("params"), triple("Dss-Parms", value, 1)});
    }
    if (record_is(value, "SingleAttribute", 3)) {
        return builder_.tuple({builder_.atom("AttributeTypeAndValue"), element(value, 1), term(element(value, 2))});
    }
    if (record_is(value, "OneAsymmetricKey", 6)) {
        return atom_is(element(value, 1), "v1") ? rename(value, "PrivateKeyInfo") : value;
    }
    return renamed(value, DECODE_RENAMES);
}

std::optional<Term> Walk::encoded(const Term &value) {
    if (record_is(value, "SubjectPublicKeyInfo", 3) && record_is(element(value, 1), "AlgorithmIdentifier", 3)) {
        return key_info(value, {"AlgorithmIdentifier", 3, "SubjectPublicKeyInfo_algorithm"});
    }
    if (record_is(value, "AttributeTypeAndValue", 3)) {
        return builder_.tuple({builder_.atom("SingleAttribute"), element(value, 1), term(element(value, 2))});
    }
    if (record_is(value, "SingleAttribute", 3) && record_is(element(value, 2), "correct", 2)) {
        return builder_.tuple({element(value, 0), element(value, 1), element(element(value, 2), 1)});
    }
    if (value.is_tuple() && value.tuple_size_unchecked() == 2 && atom_is(element(value, 0), "params") &&
        record_is(element(value, 1), "Dss-Parms", 4)) {
        return triple("DSA-Params", element(value, 1), 1);
    }
    return renamed(value, ENCODE_RENAMES);
}
} // namespace

// translate(decode | encode, Term): pubkey_translation:decode/1 or encode/1.
Term translate(ProcessContext &context, const Named<DirectionRole> &direction, const Named<ValueRole> &value) {
    asn1::Builder builder(context);
    Walk walk(builder, atom_is(direction.term, "decode"));
    return walk.term(value.term);
}
} // namespace clause::runtime::builtins::public_key
