%% clause_public_key: the builtins Clause's public_key module calls (plan 21 step LIB80B,
%% runtime/src/builtins/public_key/, docs/builtins.md#public-key): pubkey_translation's walk between the generated
%% modules' records and public_key's, public_key's tables of object identifiers, Windows' trust store, PEM text and
%% the PBE tables (LIB80E1), the otp form walk and extension types (LIB80E2), names (LIB80E3), the policy tree
%% (LIB80E4). Each catalog builtin replaces its clause_builtin_placeholder body.
-module(clause_public_key).
-export([translate/2, oid_name/2, name_oid/2, asn1_module/1, os_cacerts/0]).
-export([pem_decode/1, pem_entry/1, pbe_cipher/1, pbes2_cipher/1, pbes1/1, pbes1_oid/2, prf_digest/1]).
-export([otp_transform/2, extension_type/1]).
-export([normalize_name/1, is_issuer/2, is_dir_name/3, match_name/3, match_wildcard/2, lower_ascii/1]).
-export([policy_add/4, policy_map/5, policy_intersection/3, policy_prune/1, policy_constrained/1, policy_result/1]).

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
normalize_name(_Name) -> clause_builtin_placeholder.
is_issuer(_Issuer, _Candidate) -> clause_builtin_placeholder.
is_dir_name(_First, _Second, _Exact) -> clause_builtin_placeholder.
match_name(_Type, _Name, _Permitted) -> clause_builtin_placeholder.
match_wildcard(_Fqdn, _Name) -> clause_builtin_placeholder.
lower_ascii(_Term) -> clause_builtin_placeholder.
policy_add(_Tree, _Count, _Infos, _AnyAllowed) -> clause_builtin_placeholder.
policy_map(_Tree, _Count, _Mapping, _Inhibit, _AnyQualifiers) -> clause_builtin_placeholder.
policy_intersection(_UserPolicySet, _Tree, _Count) -> clause_builtin_placeholder.
policy_prune(_Tree) -> clause_builtin_placeholder.
policy_constrained(_Tree) -> clause_builtin_placeholder.
policy_result(_Tree) -> clause_builtin_placeholder.
