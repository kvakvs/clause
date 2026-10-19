%% Clause's erl_scan (plan 21 step LIB30, docs/builtins.md#erl-scan): OTP 29's token scanner. The runtime builtin
%% clause_erl_scan:scan/6 scans; this module reads the options, runs the reserved_word_fun and text_fun callbacks on
%% the tokens and keeps the re-entrant scanner's continuation: the tokens done and the characters of the unfinished
%% token, scanned again with the next characters.
-module(erl_scan).
-export([
    category/1,
    column/1,
    continuation_location/1,
    end_location/1,
    f_reserved_word/1,
    format_error/1,
    line/1,
    location/1,
    reserved_word/1,
    string/1,
    string/2,
    string/3,
    symbol/1,
    text/1,
    tokens/3,
    tokens/4
]).
-include("erl_scan.types.hrl").

%% The options of one scan: the reserved_word_fun (default: reserved_word/1), the text_fun deciding which tokens keep
%% their text (none when the text option or nothing decides), and the flags of clause_erl_scan:scan/6.
-record(scan_options, {reserved = default, text_fun = none, flags}).

category({Category, _Anno}) -> Category;
category({Category, _Anno, _Symbol}) -> Category;
category(Token) -> erlang:error(badarg, [Token]).

symbol({Category, _Anno}) -> Category;
symbol({_Category, _Anno, Symbol}) -> Symbol;
symbol(Token) -> erlang:error(badarg, [Token]).

column(Token) -> erl_anno:column(element(2, Token)).
end_location(Token) -> erl_anno:end_location(element(2, Token)).
line(Token) -> erl_anno:line(element(2, Token)).
location(Token) -> erl_anno:location(element(2, Token)).
text(Token) -> erl_anno:text(element(2, Token)).

continuation_location({erl_scan_continuation, _, no_col, _, Line, _, _, _}) -> Line;
continuation_location({erl_scan_continuation, _, Column, _, Line, _, _, _}) -> {Line, Column}.

reserved_word(Atom) ->
    f_reserved_word(Atom) orelse lists:member(Atom, erl_features:keywords()).

f_reserved_word(Atom) ->
    lists:member(Atom, [
        'after',
        'begin',
        'case',
        'try',
        'cond',
        'catch',
        'andalso',
        'orelse',
        'end',
        'fun',
        'if',
        'let',
        'of',
        'receive',
        'when',
        'bnot',
        'not',
        'div',
        'rem',
        'band',
        'and',
        'bor',
        'bxor',
        'bsl',
        'bsr',
        'or',
        'xor'
    ]).

format_error({unterminated, char}) ->
    "unterminated character";
format_error({unterminated, What, Head}) ->
    Quote =
        case What of
            atom -> $';
            _ -> $"
        end,
    lists:flatten(["unterminated ", what(What), " starting with ", io_lib:write_string(Head, Quote)]);
format_error({illegal, Type}) ->
    lists:flatten(io_lib:fwrite("illegal ~w", [Type]));
format_error({base, Base}) ->
    lists:flatten(io_lib:fwrite("illegal base '~w'", [Base]));
format_error(indentation) ->
    "bad indentation in triple-quoted string";
format_error(white_space) ->
    "not white space after start of triple-quoted string";
format_error(string_concat) ->
    "adjacent string literals without intervening white space";
format_error(Other) ->
    lists:flatten(io_lib:write(Other)).

%% What an unterminated descriptor names.
what(atom) -> "atom";
what(string) -> "string";
what({string, _Quotes}) -> "triple-quoted string";
what({sigil, Name, Open, Close}) -> [$~, atom_to_list(Name), Open, Close, " sigil string"];
what({sigil, Name, _Quotes}) -> "triple-quoted ~" ++ atom_to_list(Name) ++ " sigil string".

string(String) ->
    string(String, 1, []).

string(String, StartLocation) ->
    string(String, StartLocation, []).

string(String, Line, Options) when is_list(String), is_integer(Line) ->
    scan_string(String, Line, no_col, options(Options));
string(String, {Line, Column}, Options) when
    is_list(String), is_integer(Line), is_integer(Column), Column >= 1
->
    scan_string(String, Line, Column, options(Options)).

scan_string(String, Line, Column, #scan_options{flags = Flags} = Options) ->
    case clause_erl_scan:scan(String, true, Line, Column, Flags, true) of
        {ok, Tokens, _Rest, EndLine, EndColumn} ->
            {ok, callbacks(Tokens, Options), location(EndLine, EndColumn)};
        {error, ErrorInfo, EndLocation, _Rest} ->
            {error, ErrorInfo, EndLocation}
    end.

tokens(Continuation, CharSpec, StartLocation) ->
    tokens(Continuation, CharSpec, StartLocation, []).

tokens([], CharSpec, Line, Options) when is_integer(Line) ->
    continue(CharSpec, Line, no_col, options(Options), []);
tokens([], CharSpec, {Line, Column}, Options) when
    is_integer(Line), is_integer(Column), Column >= 1
->
    continue(CharSpec, Line, Column, options(Options), []);
tokens(
    {erl_scan_continuation, Chars, Column, Done, Line, Options, _, _},
    CharSpec,
    _StartLocation,
    _Options
) ->
    continue(Chars ++ CharSpec, Line, Column, Options, Done).

%% Scan one form from the characters left over and the new ones (eof appended once the input has ended, as OTP
%% does); Done holds the token lists of earlier calls, most recent first.
continue(Chars, Line, Column, #scan_options{flags = Flags} = Options, Done) when
    is_list(Chars); Chars =:= eof
->
    case clause_erl_scan:scan(Chars, false, Line, Column, Flags, false) of
        {ok, Tokens, eof, EndLine, EndColumn} ->
            case collected(Done, callbacks(Tokens, Options)) of
                [] -> {done, {eof, location(EndLine, EndColumn)}, eof};
                All -> {done, {ok, All, location(EndLine, EndColumn)}, eof}
            end;
        {ok, Tokens, Rest, EndLine, EndColumn} ->
            {done, {ok, collected(Done, callbacks(Tokens, Options)), location(EndLine, EndColumn)},
                Rest};
        {more, Tokens, Rest, RestLine, RestColumn} ->
            Done1 = [callbacks(Tokens, Options) | Done],
            {more, {erl_scan_continuation, Rest, RestColumn, Done1, RestLine, Options, [], []}};
        {error, ErrorInfo, EndLocation, Rest} ->
            {done, {error, ErrorInfo, EndLocation}, Rest}
    end.

collected(Done, Tokens) ->
    lists:append(lists:reverse([Tokens | Done])).

location(Line, no_col) -> Line;
location(Line, Column) -> {Line, Column}.

%% OTP's options: return expands to return_comments and return_white_spaces; a reserved_word_fun that is not a
%% fun of one argument is badarg; text includes every token's text, a text_fun decides it otherwise.
options(Options) when is_list(Options) ->
    Expanded = lists:foldr(fun expand_option/2, [], Options),
    Reserved =
        case lists:keyfind(reserved_word_fun, 1, Expanded) of
            false -> default;
            {reserved_word_fun, Fun} when is_function(Fun, 1) -> Fun;
            _ -> erlang:error(badarg, [Options])
        end,
    Text = proplists:get_bool(text, Expanded),
    TextFun =
        case Text of
            true -> none;
            false -> proplists:get_value(text_fun, Expanded, none)
        end,
    Keywords =
        case Reserved of
            default -> erl_features:keywords();
            _ -> custom
        end,
    Flags = {
        proplists:get_bool(return_white_spaces, Expanded),
        proplists:get_bool(return_comments, Expanded),
        Text orelse TextFun =/= none,
        Keywords
    },
    #scan_options{reserved = Reserved, text_fun = TextFun, flags = Flags};
options(Option) ->
    options([Option]).

expand_option(return, Options) -> [return_comments, return_white_spaces | Options];
expand_option(Option, Options) -> [Option | Options].

%% The reserved_word_fun on unquoted atoms, then the text_fun on every token, in token order.
callbacks(Tokens, #scan_options{reserved = default, text_fun = none}) ->
    Tokens;
callbacks(Tokens, Options) ->
    [callback(Token, Options) || Token <- Tokens].

callback({'$unquoted', Anno, Name}, #scan_options{reserved = Reserved} = Options) ->
    case Reserved(Name) of
        true -> keep_text({Name, Anno}, Name, Options);
        false -> keep_text({atom, Anno, Name}, atom, Options)
    end;
callback(Token, Options) ->
    keep_text(Token, element(1, Token), Options).

keep_text(Token, _Category, #scan_options{text_fun = none}) ->
    Token;
keep_text(Token, Category, #scan_options{text_fun = TextFun}) ->
    Anno = element(2, Token),
    case TextFun(Category, erl_anno:text(Anno)) of
        true -> Token;
        false -> setelement(2, Token, erl_anno:new(erl_anno:location(Anno)))
    end.
