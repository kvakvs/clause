%% erl_scan's scanner (plan 21 step LIB30, docs/builtins.md#erl-scan): a runtime builtin over a character list, called
%% by library/stdlib/erl_scan.erl.
-module(clause_erl_scan).
-export([scan/6]).

%% scan(Chars, Eof, Line, Column, {WhiteSpace, Comments, Text, Reserved}, Whole): the tokens of one form (Whole false)
%% or of the whole input, {more, ...} where more characters are needed, or {error, ErrorInfo, EndLocation, Rest}.
%% Reserved is the list of feature keywords, or custom for a reserved_word_fun, which leaves unquoted atoms as
%% {'$unquoted', Anno, Atom}.
scan(_, _, _, _, _, _) -> clause_builtin_placeholder.
