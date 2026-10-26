%% clause_public_key: the builtins Clause's public_key module calls (plan 21 step LIB80B,
%% runtime/src/builtins/public_key/, docs/builtins.md#public-key): pubkey_translation's walk between the generated
%% modules' records and public_key's, public_key's tables of object identifiers, Windows' trust store, PEM text and
%% the PBE tables (LIB80E1), the otp form walk and extension types (LIB80E2). Each catalog builtin replaces its
%% clause_builtin_placeholder body.
-module(clause_public_key).
-export([translate/2, oid_name/2, name_oid/2, asn1_module/1, os_cacerts/0]).
-export([pem_decode/1, pem_entry/1, pbe_cipher/1, pbes2_cipher/1, pbes1/1, pbes1_oid/2, prf_digest/1]).
-export([otp_transform/2, extension_type/1]).

translate(_Direction, _Term) -> clause_builtin_placeholder.
oid_name(_Table, _Oid) -> clause_builtin_placeholder.
name_oid(_Table, _Name) -> clause_builtin_placeholder.
asn1_module(_Type) -> clause_builtin_placeholder.
os_cacerts() -> clause_builtin_placeholder.
pem_decode(_Text) -> clause_builtin_placeholder.
pem_entry(_Entry) -> clause_builtin_placeholder.
pbe_cipher(_Name) -> clause_builtin_placeholder.
pbes2_cipher(_Oid) -> clause_builtin_placeholder.
pbes1(_Oid) -> clause_builtin_placeholder.
pbes1_oid(_Name, _Digest) -> clause_builtin_placeholder.
prf_digest(_Oid) -> clause_builtin_placeholder.
otp_transform(_Direction, _Term) -> clause_builtin_placeholder.
extension_type(_Name) -> clause_builtin_placeholder.
