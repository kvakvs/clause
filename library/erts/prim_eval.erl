%% Clause's replacement for OTP's prim_eval (docs/library.md#runtime-module-replacements), generated from
%% prim_eval.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(prim_eval).
-export([
    'receive'/2
]).

'receive'(_, _) -> erlang:error({not_implemented, 'receive', 2}).
