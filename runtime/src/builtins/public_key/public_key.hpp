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
// Added for public_key's policy tree (plan 21 step LIB80E4).
struct FlagRole;
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

// Added for public_key's PBKDF1 loop (plan 21 step LIB80E5): pbkdf1_rounds(First, Count, Digest) is the PBKDF1 output
// after the first round; 0 when a called builtin failed.
Word pbkdf1_rounds(ProcessContext &context, const Named<ValueRole> &first, const Named<KeyRole> &count,
                   const Named<TableRole> &digest);
} // namespace clause::runtime::builtins::public_key
