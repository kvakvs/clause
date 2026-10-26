#include "public_key.hpp"
#include <array>
#include <initializer_list>

// Added for public_key's otp form (plan 21 step LIB80E2): pubkey_cert_records:transform/2 between OTP-PKIX's records
// and the otp form public_key documents: names with their attribute values unwrapped, signature algorithms, DSA
// parameters, name constraints, distribution points and access descriptions. Certificates and their TBS parts stay
// for Erlang, which decodes their public keys and extensions with the generated modules.
namespace clause::runtime::builtins::public_key {
using asn1::erlang_error;
using Reason = abi::v1::ErrorReason;

namespace {
bool atom_is(const Term &term, const std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

bool record_is(const Term &term, const std::string_view name, const std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && atom_is(term.tuple_element_unchecked(0), name);
}

Term element(const Term &tuple, const std::size_t index) { return tuple.tuple_element_unchecked(index); }

// {Tag, Value} with an atom tag.
bool tagged(const Term &term, const std::string_view tag) {
    return term.is_tuple() && term.tuple_size_unchecked() == 2 && atom_is(element(term, 0), tag);
}

// The attribute types whose values the otp form unwraps from their string choice.
const std::array<std::initializer_list<std::uint32_t>, 3> UNWRAPPED_ATTRIBUTES{{
    {2, 5, 4, 6},
    {0, 9, 2342, 19200300, 100, 1, 25},
    {1, 2, 840, 113549, 1, 9, 1},
}};

bool unwrapped_attribute(const Term &type) {
    for (const auto &oid : UNWRAPPED_ATTRIBUTES) {
        if (same_oid(type, oid)) {
            return true;
        }
    }
    return false;
}

class OtpWalk {
  public:
    OtpWalk(asn1::Builder &builder, const bool decoding) : builder_(builder), decoding_(decoding) {}

    Term term(const Term &value) { return decoding_ ? decoded(value) : encoded(value); }

  private:
    Term decoded(const Term &value);
    Term encoded(const Term &value);
    std::optional<Term> decoded_record(const Term &value);
    std::optional<Term> named(const Term &value);

    // The tuple with one element replaced.
    Term with_element(const Term &tuple, const std::size_t index, const Term &replacement) {
        std::vector<Term> fields;
        fields.reserve(tuple.tuple_size_unchecked());
        for (std::size_t i = 0; i < tuple.tuple_size_unchecked(); ++i) {
            fields.push_back(i == index ? replacement : element(tuple, i));
        }
        return builder_.tuple(fields);
    }

    Term rename(const Term &tuple, const std::string_view name) {
        return builder_.tuple({builder_.atom(name), element(tuple, 1), term(element(tuple, 2))});
    }

    // [F(E) || E <- List] with the comprehension's bad_generator for an improper list.
    template <typename Each> Term each(const Term &list, Each &&each_element) {
        std::vector<Term> values;
        auto rest = list;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            values.push_back(each_element(rest.head_unchecked()));
        }
        if (!rest.is_nil()) {
            erlang_error(Reason::bad_generator, &rest);
        }
        return builder_.list(values);
    }

    // lists:map/2: function_clause for anything but a proper list.
    template <typename Each> Term map(const Term &list, Each &&each_element) {
        std::vector<Term> values;
        auto rest = list;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            values.push_back(each_element(rest.head_unchecked()));
        }
        if (!rest.is_nil()) {
            erlang_error(Reason::function_clause);
        }
        return builder_.list(values);
    }

    Term rdn_sequence(const Term &sequence) {
        const auto rdns = map(sequence, [this](const Term &rdn) {
            return map(rdn, [this](const Term &attribute) { return term(attribute); });
        });
        return builder_.tuple({builder_.atom("rdnSequence"), rdns});
    }

    // Name constraint subtrees: GeneralSubtree records with their base names transformed, others skipped.
    Term subtrees(const Term &trees) {
        if (atom_is(trees, "asn1_NOVALUE")) {
            return trees;
        }
        std::vector<Term> values;
        auto rest = trees;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto tree = rest.head_unchecked();
            if (record_is(tree, "GeneralSubtree", 4)) {
                values.push_back(with_element(tree, 1, term(element(tree, 1))));
            }
        }
        if (!rest.is_nil()) {
            erlang_error(Reason::bad_generator, &rest);
        }
        return builder_.list(values);
    }

    Term name_constraints(const Term &value) {
        return builder_.tuple({element(value, 0), subtrees(element(value, 1)), subtrees(element(value, 2))});
    }

    // dec_transform_dist_point/1: a point of {Type, Name} with its name and CRL issuers transformed.
    Term distribution_point(const Term &point) {
        if (!record_is(point, "DistributionPoint", 4) || !element(point, 1).is_tuple() ||
            element(point, 1).tuple_size_unchecked() != 2) {
            return point;
        }
        const auto name = element(point, 1);
        const auto renamed = builder_.tuple({element(name, 0), term(element(name, 1))});
        return builder_.tuple({element(point, 0), renamed, element(point, 2), term(element(point, 3))});
    }

    // The attribute of a name in the other form, its value unwrapped from {correct, V} or a string choice.
    Term attribute(const Term &value) {
        const auto type = element(value, 1);
        auto inner = element(value, 2);
        if (decoding_ && unwrapped_attribute(type) && inner.is_tuple() && inner.tuple_size_unchecked() == 2) {
            inner = element(inner, 1);
        } else if (!decoding_ && unwrapped_attribute(type)) {
            inner = builder_.tuple({builder_.atom("correct"), inner});
        }
        return builder_.tuple({builder_.atom(decoding_ ? "AttributeTypeAndValue" : "SingleAttribute"), type, inner});
    }

    asn1::Builder &builder_;
    bool decoding_;
};

// The clauses both directions share: attribute and key identifier records, directory names and RDN sequences, name
// constraints.
std::optional<Term> OtpWalk::named(const Term &value) {
    if (record_is(value, decoding_ ? "SingleAttribute" : "AttributeTypeAndValue", 3)) {
        return attribute(value);
    }
    if (record_is(value, "AuthorityKeyIdentifier", 4)) {
        return with_element(value, 2, term(element(value, 2)));
    }
    if (tagged(value, "directoryName")) {
        return builder_.tuple({element(value, 0), term(element(value, 1))});
    }
    if (tagged(value, "rdnSequence") && element(value, 1).is_list()) {
        return rdn_sequence(element(value, 1));
    }
    if (record_is(value, "NameConstraints", 3)) {
        return name_constraints(value);
    }
    return std::nullopt;
}

std::optional<Term> OtpWalk::decoded_record(const Term &value) {
    if (record_is(value, "OTPCertificate_signatureAlgorithm", 3) ||
        record_is(value, "OTPTBSCertificate_signature", 3)) {
        return rename(value, "SignatureAlgorithm");
    }
    if (tagged(value, "present") && record_is(element(value, 1), "DSA-Params", 4)) {
        const auto params = element(value, 1);
        return builder_.tuple({builder_.atom("params"), with_element(params, 0, builder_.atom("Dss-Parms"))});
    }
    if (tagged(value, "absent") && atom_is(element(value, 1), "NULL")) {
        return element(value, 1);
    }
    if (auto name = named(value)) {
        return name;
    }
    if (record_is(value, "AccessDescription", 3)) {
        return with_element(value, 2, term(element(value, 2)));
    }
    return std::nullopt;
}

// dec_transform/1 without its certificate and TBS certificate clauses.
Term OtpWalk::decoded(const Term &value) {
    if (value.is_cons() && record_is(value.head_unchecked(), "DistributionPoint", 4)) {
        return each(value, [this](const Term &point) { return distribution_point(point); });
    }
    if (value.is_list()) {
        return each(value, [this](const Term &item) { return term(item); });
    }
    return decoded_record(value).value_or(value);
}

// enc_transform/1 without its certificate and TBS certificate clauses.
Term OtpWalk::encoded(const Term &value) {
    if (record_is(value, "SignatureAlgorithm", 3)) {
        return rename(value, "OTPTBSCertificate_signature");
    }
    if (tagged(value, "params") && record_is(element(value, 1), "Dss-Parms", 4)) {
        const auto params = element(value, 1);
        return builder_.tuple({builder_.atom("present"), with_element(params, 0, builder_.atom("DSA-Params"))});
    }
    // [{directoryName, _}]: one directory name in a list.
    if (value.is_cons() && value.tail_unchecked().is_nil() && tagged(value.head_unchecked(), "directoryName")) {
        return each(value, [this](const Term &name) { return term(name); });
    }
    return named(value).value_or(value);
}
} // namespace

// otp_transform(decode | encode, Term): pubkey_cert_records:transform/2 below the certificate records.
Term otp_transform(ProcessContext &context, const Named<DirectionRole> &direction, const Named<ValueRole> &value) {
    asn1::Builder builder(context);
    OtpWalk walk(builder, atom_is(direction.term, "decode"));
    return walk.term(value.term);
}
} // namespace clause::runtime::builtins::public_key
