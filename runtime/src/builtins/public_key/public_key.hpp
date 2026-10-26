#pragma once
// Added for public_key (plan 21 step LIB80B): the builtins of clause_public_key, which Clause's public_key module calls
// (docs/libraries/public_key.md): pubkey_translation's walk and public_key's object identifier tables.
#include "../asn1/asn1.hpp"
#include "../typed.hpp"

namespace clause::runtime::builtins::public_key {
// The roles of the builtins' arguments.
struct DirectionRole;
struct TableRole;
struct KeyRole;
struct ValueRole;

// translate(decode | encode, Term).
Term translate(ProcessContext &context, const Named<DirectionRole> &direction, const Named<ValueRole> &value);
// oid_name(Table, Oid), name_oid(Table, Name), asn1_module(Type): the row's other column, or undefined.
Term oid_name(ProcessContext &context, const Named<TableRole> &table, const Named<KeyRole> &oid);
Term name_oid(ProcessContext &context, const Named<TableRole> &table, const Named<KeyRole> &name);
Term asn1_module(ProcessContext &context, const Named<KeyRole> &type);
// os_cacerts(): Windows' current user ROOT certificates as [{Encoding, Der}] (public_key trust stores).
Term os_cacerts(ProcessContext &context);
} // namespace clause::runtime::builtins::public_key
