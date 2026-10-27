%% Clause's epp_dodger (plan 21 LIB77E4c, docs/libraries/syntax_tools.md#epp_dodger): Erlang source parsed without
%% the preprocessor. Reading forms, parsing them and fixing failed -define forms are Erlang here; the token rewriting
%% of directives and macros and the rewriting of parsed forms back into attributes and macros are runtime builtins of
%% clause_epp_dodger (runtime/src/builtins/epp_dodger/).
-module(epp_dodger).
-export([
    format_error/1,
    parse/1,
    parse/2,
    parse/3,
    parse_file/1,
    parse_file/2,
    parse_form/2,
    parse_form/3,
    quick_parse/1,
    quick_parse/2,
    quick_parse/3,
    quick_parse_file/1,
    quick_parse_file/2,
    quick_parse_form/2,
    quick_parse_form/3,
    tokens_to_string/1
]).
-compile(nowarn_deprecated_catch).

% The name of the function a directive becomes (clause_epp_dodger's PP_FORM).
-define(PP_FORM, '?preprocessor declaration?').

%% Files and devices.

parse_file(File) -> parse_file(File, []).

parse_file(File, Options) -> parse_file(File, fun parse/3, Options).

quick_parse_file(File) -> quick_parse_file(File, []).

quick_parse_file(File, Options) -> parse_file(File, fun quick_parse/3, Options ++ [no_fail]).

% A file parsed as UTF-8, again as Latin-1 when that fails and the file does not declare UTF-8.
parse_file(File, Parser, Options) ->
    case parse_device(utf8, File, Parser, Options) of
        {ok, Forms} = Parsed ->
            case invalid_unicode(Forms) of
                false ->
                    Parsed;
                true ->
                    case epp:read_encoding(File) of
                        utf8 -> Parsed;
                        _ -> parse_device(latin1, File, Parser, Options)
                    end
            end;
        Failed ->
            Failed
    end.

parse_device(Encoding, File, Parser, Options) ->
    case file:open(File, [read]) of
        {ok, Device} ->
            _ = epp:set_encoding(Device, Encoding),
            try
                Parser(Device, 1, Options)
            after
                ok = file:close(Device)
            end;
        {error, Reason} ->
            {error, {0, file, Reason}}
    end.

invalid_unicode(Forms) -> lists:any(fun is_invalid_unicode/1, Forms).

is_invalid_unicode({error, {_, file_io_server, invalid_unicode}}) -> true;
is_invalid_unicode(_) -> false.

parse(Device) -> parse(Device, 1).

parse(Device, Location) -> parse(Device, Location, []).

parse(Device, Location, Options) -> parse_forms(Device, Location, fun parse_form/3, Options, []).

quick_parse(Device) -> quick_parse(Device, 1).

quick_parse(Device, Location) -> quick_parse(Device, Location, []).

quick_parse(Device, Location, Options) -> parse_forms(Device, Location, fun quick_parse_form/3, Options, []).

% The forms of a device up to its end; read errors are kept as {error, ErrorInfo} forms.
parse_forms(Device, Location, Parser, Options, Forms) ->
    case Parser(Device, Location, Options) of
        {ok, none, Next} -> parse_forms(Device, Next, Parser, Options, Forms);
        {ok, Form, Next} -> parse_forms(Device, Next, Parser, Options, [Form | Forms]);
        {error, ErrorInfo, Next} -> parse_forms(Device, Next, Parser, Options, [{error, ErrorInfo} | Forms]);
        {eof, _} -> {ok, lists:reverse(Forms)}
    end.

%% One form.

parse_form(Device, Location) -> parse_form(Device, Location, []).

parse_form(Device, Location, Options) -> parse_form(Device, Location, fun normal_parser/2, Options).

quick_parse_form(Device, Location) -> quick_parse_form(Device, Location, []).

quick_parse_form(Device, Location, Options) -> parse_form(Device, Location, fun quick_parser/2, Options).

parse_form(Device, Location, Parser, Options) ->
    NoFail = proplists:get_bool(no_fail, Options),
    Clever = proplists:get_bool(clever, Options),
    {ok, {_, ReservedWord}} = erl_features:init_parse_state(Options, fun reserved_word/1),
    case io:scan_erl_form(Device, "", Location, [{reserved_word_fun, ReservedWord}]) of
        {ok, Tokens, Next} ->
            case catch {ok, Parser(Tokens, Clever)} of
                {'EXIT', Reason} ->
                    {error, {Next, ?MODULE, {unknown, Reason}}, Next};
                {error, Reason} ->
                    {error, {Next, ?MODULE, Reason}, Next};
                {parse_error, _} when NoFail ->
                    Text = erl_syntax:text(tokens_to_string(Tokens)),
                    {ok, erl_syntax:set_pos(Text, erl_anno:new(start_location(Tokens, Next))), Next};
                {parse_error, ErrorInfo} ->
                    {error, ErrorInfo, Next};
                {ok, Form} ->
                    {ok, Form, Next}
            end;
        {error, _, _} = Error ->
            Error;
        {error, _} ->
            {eof, Location};
        {eof, _} = Eof ->
            Eof
    end.

start_location([Token | _], _) -> erl_anno:location(element(2, Token));
start_location([], Location) -> Location.

reserved_word('else') -> true;
reserved_word('maybe') -> true;
reserved_word(Word) -> erl_scan:f_reserved_word(Word).

%% Parsers of one form's tokens.

normal_parser(Tokens, Clever) ->
    case scan_form(Tokens, Clever) of
        Scanned when is_list(Scanned) -> clause_epp_dodger:rewrite_form(parse_tokens(Scanned, fun fix_form/1));
        Marker -> Marker
    end.

quick_parser(Tokens, _Clever) ->
    case parse_tokens(clause_epp_dodger:quickscan(Tokens), fun fix_form/1) of
        {function, _, ?PP_FORM, _, [{clause, _, [], [], [{atom, _, kill}]}]} -> none;
        Form -> Form
    end.

% -error and -warning forms become error markers; other forms have their directives and macros rewritten.
scan_form([{'-', _}, {atom, Anno, error} | Tokens], _) -> marker(Anno, error, "-error", Tokens);
scan_form([{'-', _}, {atom, Anno, warning} | Tokens], _) -> marker(Anno, warning, "-warning", Tokens);
scan_form(Tokens, Clever) -> clause_epp_dodger:scan(Tokens, Clever).

marker(Anno, Kind, Prefix, Tokens) ->
    Text = lists:droplast(tokens_to_string(lists:droplast(Tokens))),
    erl_syntax:error_marker({erl_anno:location(Anno), ?MODULE, {Kind, Prefix ++ " " ++ Text ++ "."}}).

% The form of the tokens, retrying with the fixes a failed parse allows; throws {parse_error, ErrorInfo}.
parse_tokens(Tokens, Fix) ->
    case erl_parse:parse_form(Tokens) of
        {ok, Form} ->
            Form;
        {error, ErrorInfo} ->
            case Fix(Tokens) of
                {form, Form} -> Form;
                {retry, Tokens1, Fix1} -> parse_tokens(Tokens1, Fix1);
                error -> throw({parse_error, ErrorInfo})
            end
    end.

% A -define whose body does not parse: retried as a definition whose body is text.
fix_form([{atom, _, ?PP_FORM}, {'(', _}, {')', _}, {'->', _}, {atom, _, define}, {'(', _} | _] = Tokens) ->
    case lists:reverse(Tokens) of
        [{dot, _}, {')', _} | _] -> {retry, Tokens, fun fix_define/1};
        [{dot, Anno} | Rest] -> {retry, lists:reverse([{dot, Anno}, {')', Anno} | Rest]), fun fix_define/1};
        _ -> error
    end;
fix_form(_) ->
    error.

fix_define([
    {atom, Anno, ?PP_FORM}, {'(', _}, {')', _}, {'->', _}, {atom, AnnoA, define}, {'(', _}, Name, {',', _} | Tokens
]) ->
    [{dot, _}, {')', _} | Body] = lists:reverse(Tokens),
    Define = erl_syntax:set_pos(erl_syntax:atom(define), AnnoA),
    Text = erl_syntax:set_pos(erl_syntax:text(tokens_to_string(lists:reverse(Body))), AnnoA),
    {form, erl_syntax:set_pos(erl_syntax:attribute(Define, [Name, Text]), Anno)};
fix_define(_) ->
    error.

%% Text.

tokens_to_string([{atom, _, A} | Tokens]) -> io_lib:write_atom(A) ++ " " ++ tokens_to_string(Tokens);
tokens_to_string([{string, _, S} | Tokens]) -> io_lib:write_string(S) ++ " " ++ tokens_to_string(Tokens);
tokens_to_string([{char, _, C} | Tokens]) -> io_lib:write_char(C) ++ " " ++ tokens_to_string(Tokens);
tokens_to_string([{float, _, F} | Tokens]) -> float_to_list(F) ++ " " ++ tokens_to_string(Tokens);
tokens_to_string([{integer, _, N} | Tokens]) -> integer_to_list(N) ++ " " ++ tokens_to_string(Tokens);
tokens_to_string([{var, _, A} | Tokens]) -> atom_to_list(A) ++ " " ++ tokens_to_string(Tokens);
tokens_to_string([{dot, _} | Tokens]) -> ".\n" ++ tokens_to_string(Tokens);
tokens_to_string([{A, _} | Tokens]) -> atom_to_list(A) ++ " " ++ tokens_to_string(Tokens);
tokens_to_string([]) -> "".

format_error(macro_args) -> message("macro call missing end parenthesis");
format_error({error, Text}) -> Text;
format_error({warning, Text}) -> Text;
format_error({unknown, Reason}) -> message(io_lib:format("unknown error: ~tP", [Reason, 15])).

message(Text) -> io_lib:format("~s: ~ts", [?MODULE, Text]).
