-module(clause_json).
-export([number/2, string/2]).

% JSON scanners (plan 21 step LIB5E, docs/builtins.md#encodings): {Text, Next} for the string after a quote, and
% {integer | float, Token, Next} for a number, or `more` where the input ends first; runtime builtins that json's
% callback decoders call.
number(_, _) -> clause_builtin_placeholder.
string(_, _) -> clause_builtin_placeholder.
