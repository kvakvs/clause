%% clause_asn1: the codec of the modules clau generates from ASN.1 specifications (plan 21 step LIB84,
%% runtime/src/builtins/asn1/, docs/asn1.md). The generated encode/2 and decode/2 pass their module's schema term;
%% each catalog builtin replaces its clause_builtin_placeholder body.
-module(clause_asn1).
-export([encode/3, decode/3]).

encode(_Schema, _Type, _Value) -> clause_builtin_placeholder.
decode(_Schema, _Type, _Bytes) -> clause_builtin_placeholder.
