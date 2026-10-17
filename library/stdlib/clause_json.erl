-module(clause_json).
-export([encode/1, format/2, number/2, string/2]).

% JSON helpers (plan 21 steps LIB5E and LIB5F, docs/builtins.md#encodings): {Text, Next} for the string after a
% quote, and {integer | float, Token, Next} for a number, or `more` where the input ends first, for json's callback
% decoders; {ok, Iodata} of the default encoder and formatter, or `fallback` where json.erl encodes the term itself.
% Runtime builtins.
encode(_) -> clause_builtin_placeholder.
format(_, _) -> clause_builtin_placeholder.
number(_, _) -> clause_builtin_placeholder.
string(_, _) -> clause_builtin_placeholder.
