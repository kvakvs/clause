#pragma once
// Added for public_key (plan 21 step LIB80B): the builtins of clause_public_key, which Clause's public_key module calls
// (docs/libraries/public_key.md): pubkey_translation's walk and public_key's object identifier tables.
#include "../asn1/asn1.hpp"
#include "../typed.hpp"
#include <initializer_list>

namespace clause::runtime::builtins::public_key {
// The roles of the builtins' arguments.
struct DirectionRole;
struct TableRole;
struct KeyRole;
struct ValueRole;
// Added for public_key's names (plan 21 step LIB80E3).
struct NameRole;
struct CandidateRole;
struct FlagRole;
// Added for public_key's policy tree (plan 21 step LIB80E4).
struct TreeRole;
struct CountRole;

// Whether a term is the OID of these arcs; the OID's tuple term.
bool same_oid(const Term &oid, const std::initializer_list<std::uint32_t> &arcs);
Term oid_term(asn1::Builder &builder, const std::initializer_list<std::uint32_t> &arcs);
// Whether a term is the character list of a text. Added for public_key's PBE tables.
bool is_string(const Term &term, std::string_view text);

// translate(decode | encode, Term).
Term translate(ProcessContext &context, const Named<DirectionRole> &direction, const Named<ValueRole> &value);
// oid_name(Table, Oid), name_oid(Table, Name), asn1_module(Type): the row's other column, or undefined.
Term oid_name(ProcessContext &context, const Named<TableRole> &table, const Named<KeyRole> &oid);
Term name_oid(ProcessContext &context, const Named<TableRole> &table, const Named<KeyRole> &name);
Term asn1_module(ProcessContext &context, const Named<KeyRole> &type);
// os_cacerts(): Windows' current user ROOT certificates as [{Encoding, Der}] (public_key trust stores).
Term os_cacerts(ProcessContext &context);

// Added for public_key's PEM text (plan 21 step LIB80E1): pem_decode(Text) is {Entries, done}, or {Entries,
// EncryptedDer, Rest} after an ENCRYPTED PRIVATE KEY entry; pem_entry(Entry) is one entry's PEM text.
Term pem_decode(ProcessContext &context, const Named<ValueRole> &text);
Term pem_entry(ProcessContext &context, const Named<ValueRole> &entry);
// Added for public_key's PBE tables: pbe_cipher(Name) is {CryptoCipher, BlockSize, KeyLength}, pbes2_cipher(Oid) the
// name of a PBES2 scheme, pbes1(Oid) {Name, Digest}, pbes1_oid(Name, Digest) the OID, prf_digest(Oid) a PBKDF2
// digest; function_clause for anything else.
Term pbe_cipher(ProcessContext &context, const Named<KeyRole> &name);
Term pbes2_cipher(ProcessContext &context, const Named<KeyRole> &oid);
Term pbes1(ProcessContext &context, const Named<KeyRole> &oid);
Term pbes1_oid(ProcessContext &context, const Named<KeyRole> &cipher, const Named<ValueRole> &digest);
Term prf_digest(ProcessContext &context, const Named<KeyRole> &oid);

// Added for public_key's otp form (plan 21 step LIB80E2): otp_transform(decode | encode, Term) is
// pubkey_cert_records:transform/2 below certificates; extension_type(Name) the {Module, Type} of an extension's value.
Term otp_transform(ProcessContext &context, const Named<DirectionRole> &direction, const Named<ValueRole> &value);
Term extension_type(ProcessContext &context, const Named<KeyRole> &name);

// Added for public_key's names (plan 21 step LIB80E3): pubkey_cert's normalize_general_name/1, is_issuer/2,
// is_dir_name/3 and match_name/3, the https wildcard match and pubkey_verify_hostname's to_lower_ascii/1; 0 when a
// called builtin failed.
Word normalize_name(ProcessContext &context, const Named<NameRole> &name);
Word is_issuer(ProcessContext &context, const Named<NameRole> &issuer, const Named<CandidateRole> &candidate);
Word is_dir_name(ProcessContext &context, const Named<NameRole> &first, const Named<CandidateRole> &second,
                 const Named<FlagRole> &exact);
Word match_name(ProcessContext &context, const Named<FlagRole> &type, const Named<NameRole> &name,
                const Named<CandidateRole> &permitted);
Word match_wildcard(ProcessContext &context, const Named<NameRole> &fqdn, const Named<CandidateRole> &name);
Word lower_ascii(ProcessContext &context, const Named<NameRole> &term);

// Added for public_key's policy tree (plan 21 step LIB80E4): pubkey_policy_tree's walks. policy_add, policy_map and
// policy_intersection return {Tree, Count}; policy_prune the pruned tree; policy_constrained the constrained node
// set; policy_result its nodes with their qualifiers collected.
Term policy_add(ProcessContext &context, const Named<TreeRole> &tree, const Named<CountRole> &count,
                const Named<ValueRole> &infos, const Named<FlagRole> &any_allowed);
Term policy_map(ProcessContext &context, const Named<TreeRole> &tree, const Named<CountRole> &count,
                const Named<ValueRole> &mapping, const Named<FlagRole> &inhibit, const Named<KeyRole> &any_qualifiers);
Term policy_intersection(ProcessContext &context, const Named<ValueRole> &user_set, const Named<TreeRole> &tree,
                         const Named<CountRole> &count);
Term policy_prune(ProcessContext &context, const Named<TreeRole> &tree);
Term policy_constrained(ProcessContext &context, const Named<TreeRole> &tree);
Term policy_result(ProcessContext &context, const Named<TreeRole> &tree);
} // namespace clause::runtime::builtins::public_key
