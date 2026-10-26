%% clause_public_key: the builtins Clause's public_key module calls (plan 21 step LIB80B,
%% runtime/src/builtins/public_key/, docs/builtins.md#public-key): pubkey_translation's walk between the generated
%% modules' records and public_key's, and public_key's tables of object identifiers. Each catalog builtin replaces its
%% clause_builtin_placeholder body.
-module(clause_public_key).
-export([translate/2, oid_name/2, name_oid/2, asn1_module/1]).

translate(_Direction, _Term) -> clause_builtin_placeholder.
oid_name(_Table, _Oid) -> clause_builtin_placeholder.
name_oid(_Table, _Name) -> clause_builtin_placeholder.
asn1_module(_Type) -> clause_builtin_placeholder.
