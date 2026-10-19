%% erl_parse's grammar and term conversion (plan 21 step LIB31, docs/builtins.md#erl-parse): runtime builtins called
%% by library/stdlib/erl_parse.erl.
-module(clause_erl_parse).
-export([parse/1, abstract/3]).

%% parse(Tokens): {ok, Form} or {error, ErrorInfo} from the grammar and its actions; {syntax_error, Token},
%% syntax_error_at_end, {syntax_error_at_end, LastToken} where the tokens ran out, or {format, Location, Format, Args}
%% for a message io_lib:format/2 builds.
parse(_) -> clause_builtin_placeholder.

%% abstract(Term, Anno, Encoding): erl_parse:abstract/3 for the encodings latin1, unicode, utf8 and none.
abstract(_, _, _) -> clause_builtin_placeholder.
