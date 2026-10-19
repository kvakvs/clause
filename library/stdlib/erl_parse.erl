%% Clause's erl_parse (plan 21 step LIB31, docs/builtins.md#erl-parse): OTP 29's parser of the abstract format. The
%% runtime builtin clause_erl_parse:parse/1 runs the grammar and its actions; this module rewrites the tokens of the
%% parse_* entry points, turns stops of the grammar into OTP's error terms, converts terms with an encoding fun and
%% walks annotations with funs. normalise/1, tokens/1,2, first_anno/1 and the precedence tables are builtins.
-module(erl_parse).
-export([
    abstract/1,
    abstract/2,
    anno_from_term/1,
    anno_to_term/1,
    first_anno/1,
    fold_anno/3,
    format_error/1,
    func_prec/0,
    inop_prec/1,
    map_anno/2,
    mapfold_anno/3,
    max_prec/0,
    new_anno/1,
    normalise/1,
    parse/1,
    parse_and_scan/1,
    parse_exprs/1,
    parse_form/1,
    parse_term/1,
    preop_prec/1,
    tokens/1,
    tokens/2,
    type_inop_prec/1,
    type_preop_prec/1
]).
-include("erl_parse.types.hrl").

%% The line yecc gives the end of an empty token list.
-define(NO_END, 999999).

%% The attribute names the grammar knows as keywords.
parse_form([{'-', A1}, {atom, A2, Name} | Tokens]) when
    Name =:= spec; Name =:= callback; Name =:= record
->
    parse([{'-', A1}, {Name, A2} | Tokens]);
parse_form(Tokens) ->
    parse(Tokens).

%% The expressions as the body of f() -> ...
parse_exprs(Tokens) ->
    case parse(function_head(Tokens)) of
        {ok, {function, _, f, 0, [{clause, _, [], [], Exprs}]}} -> {ok, Exprs};
        {error, _} = Error -> Error
    end.

parse_term(Tokens) ->
    case parse(function_head(Tokens)) of
        {ok, {function, _, f, 0, [{clause, _, [], [], [Expr]}]}} ->
            try erl_parse:normalise(Expr) of
                Term -> {ok, Term}
            catch
                _:_ -> bad_term(Expr)
            end;
        {ok, {function, _, f, 0, [{clause, _, [], [], [_, Second | _]}]}} ->
            bad_term(Second);
        {error, _} = Error ->
            Error
    end.

function_head(Tokens) ->
    Anno = erl_anno:new(0),
    [{atom, Anno, f}, {'(', Anno}, {')', Anno}, {'->', Anno} | Tokens].

bad_term(Expr) ->
    {error, {erl_anno:location(erl_parse:first_anno(Expr)), ?MODULE, "bad term"}}.

%% yecc's parse/1: one form, its dot and nothing after it.
parse(Tokens) ->
    finish(clause_erl_parse:parse(Tokens)).

%% yecc's parse_and_scan/1: the scanner is called for more tokens until it reports the end.
parse_and_scan({Function, Arguments}) ->
    scan_and_parse(fun() -> apply(Function, Arguments) end, []);
parse_and_scan({Module, Function, Arguments}) ->
    scan_and_parse(fun() -> apply(Module, Function, Arguments) end, []).

scan_and_parse(Next, Tokens) ->
    case Next() of
        {ok, More, _EndLocation} ->
            All = Tokens ++ More,
            case clause_erl_parse:parse(All) of
                {ok, _} -> scan_and_parse(Next, All);
                {syntax_error_at_end, _} -> scan_and_parse(Next, All);
                syntax_error_at_end -> scan_and_parse(Next, All);
                Stop -> finish(Stop)
            end;
        {eof, EndLocation} ->
            case clause_erl_parse:parse(Tokens) of
                {syntax_error_at_end, _} -> syntax_error({'$end', EndLocation});
                syntax_error_at_end -> syntax_error({'$end', EndLocation});
                Stop -> finish(Stop)
            end;
        {error, Descriptor, _EndLocation} ->
            {error, Descriptor}
    end.

finish({syntax_error, Token}) ->
    syntax_error(Token);
finish({syntax_error_at_end, Last}) ->
    syntax_error({'$end', end_location(Last)});
finish(syntax_error_at_end) ->
    syntax_error({'$end', ?NO_END});
finish({format, Location, Format, Arguments}) ->
    {error, {Location, ?MODULE, io_lib:format(Format, Arguments)}};
finish(Result) ->
    Result.

%% yecc's message: the token's text when it has one, else the token written out.
syntax_error(Token) ->
    {error, {token_location(Token), ?MODULE, ["syntax error before: ", token_text(Token)]}}.

token_location(Token) ->
    try
        erl_scan:location(Token)
    catch
        _:_ -> element(2, Token)
    end.

end_location(Token) ->
    try erl_anno:end_location(element(2, Token)) of
        undefined -> token_location(Token);
        Location -> Location
    catch
        _:_ -> token_location(Token)
    end.

token_text(Token) ->
    try erl_scan:text(Token) of
        undefined -> written_token(Token);
        Text -> Text
    catch
        _:_ -> written_token(Token)
    end.

written_token(Token) ->
    try
        token_string(Token)
    catch
        _:_ -> io_lib:format("~tp", [Token])
    end.

token_string({atom, _, Atom}) -> io_lib:write_atom(Atom);
token_string({integer, _, Integer}) -> io_lib:write(Integer);
token_string({float, _, Float}) -> io_lib:write(Float);
token_string({char, _, Char}) -> io_lib:write_char(Char);
token_string({var, _, Name}) -> io_lib:format("~s", [Name]);
token_string({string, _, String}) -> io_lib:write_string(String);
token_string({reserved_symbol, _, Symbol}) -> io_lib:write(Symbol);
token_string({_Category, _, Value}) -> io_lib:format("~tp", [Value]);
token_string({dot, _}) -> "'.'";
token_string({'$end', _}) -> [];
token_string({Symbol, _}) when is_atom(Symbol) -> io_lib:write_atom(Symbol);
token_string(Other) -> io_lib:format("~tp", [Other]).

format_error(Message) ->
    case io_lib:deep_char_list(Message) of
        true -> Message;
        false -> io_lib:write(Message)
    end.

abstract(Term) ->
    clause_erl_parse:abstract(Term, erl_anno:new(0), utf8).

abstract(Term, Options) when is_list(Options) ->
    Encoding = encoding(proplists:get_value(encoding, Options, utf8)),
    Location =
        case proplists:get_value(location, Options) of
            undefined -> proplists:get_value(line, Options, 0);
            Given -> Given
        end,
    encode(Term, erl_anno:new(Location), Encoding);
abstract(Term, Location) ->
    encode(Term, erl_anno:new(Location), utf8).

encoding(Encoding) when
    Encoding =:= latin1; Encoding =:= unicode; Encoding =:= utf8; Encoding =:= none
->
    Encoding;
encoding(Fun) when is_function(Fun, 1) ->
    Fun;
encoding(Other) ->
    erlang:error({badarg, Other}).

encode(Term, Anno, Encoding) when is_atom(Encoding) ->
    clause_erl_parse:abstract(Term, Anno, Encoding);
encode(Term, Anno, Fun) ->
    abstract_by(Term, Anno, Fun).

%% abstract/3 with an encoding fun deciding which integers are characters of strings.
abstract_by(Term, Anno, Fun) when is_list(Term), Term =/= [] ->
    abstract_list(Term, [], Anno, Fun);
abstract_by(Term, Anno, Fun) when is_tuple(Term) ->
    {tuple, Anno, [abstract_by(Element, Anno, Fun) || Element <- tuple_to_list(Term)]};
abstract_by(Term, Anno, Fun) when is_map(Term) ->
    Fields = [
        {map_field_assoc, Anno, abstract_by(Key, Anno, Fun), abstract_by(Value, Anno, Fun)}
     || {Key, Value} <- maps:to_list(Term)
    ],
    {map, Anno, Fields};
abstract_by(Term, Anno, _Fun) ->
    clause_erl_parse:abstract(Term, Anno, none).

abstract_list([Head | Tail], Chars, Anno, Fun) ->
    case is_integer(Head) andalso Head >= 0 andalso Fun(Head) of
        true ->
            abstract_list(Tail, [Head | Chars], Anno, Fun);
        false ->
            Cons = {cons, Anno, abstract_by(Head, Anno, Fun), abstract_by(Tail, Anno, Fun)},
            chars_before(Chars, Cons, Anno)
    end;
abstract_list([], Chars, Anno, _Fun) ->
    {string, Anno, lists:reverse(Chars)};
abstract_list(Tail, Chars, Anno, Fun) ->
    chars_before(Chars, abstract_by(Tail, Anno, Fun), Anno).

%% The characters collected before an element that is none, last first, as conses of integers.
chars_before([Char | Chars], Built, Anno) ->
    chars_before(Chars, {cons, Anno, {integer, Anno, Char}, Built}, Anno);
chars_before([], Built, _Anno) ->
    Built.

map_anno(Fun, Abstract) ->
    {Mapped, []} = modify(Abstract, [], fun(Anno, Acc) -> {Fun(Anno), Acc} end),
    Mapped.

fold_anno(Fun, Acc0, Abstract) ->
    {_, Acc} = modify(Abstract, Acc0, fun(Anno, Acc) -> {Anno, Fun(Anno, Acc)} end),
    Acc.

mapfold_anno(Fun, Acc0, Abstract) ->
    modify(Abstract, Acc0, Fun).

new_anno(Term) ->
    map_anno(fun erl_anno:new/1, Term).

anno_to_term(Abstract) ->
    map_anno(fun erl_anno:to_term/1, Abstract).

anno_from_term(Term) ->
    map_anno(fun erl_anno:from_term/1, Term).

%% Each annotation of a tree, depth first and left to right: forms walk their contents, record names, field names
%% and bit types are not annotated.
modify({function, Name, Arity}, Acc, _Fun) ->
    {{function, Name, Arity}, Acc};
modify({function, Module, Name, Arity}, Acc0, Fun) ->
    {[Module1, Name1, Arity1], Acc} = modify([Module, Name, Arity], Acc0, Fun),
    {{function, Module1, Name1, Arity1}, Acc};
modify({attribute, Anno, record, {Name, Fields}}, Acc0, Fun) ->
    {[Anno1], [Fields1], Acc} = annotated(Anno, [Fields], Acc0, Fun),
    {{attribute, Anno1, record, {Name, Fields1}}, Acc};
modify({attribute, Anno, Kind, {Name, Types}}, Acc0, Fun) when Kind =:= spec; Kind =:= callback ->
    {[Anno1], [Types1], Acc} = annotated(Anno, [Types], Acc0, Fun),
    {{attribute, Anno1, Kind, {Name, Types1}}, Acc};
modify({attribute, Anno, Kind, {Name, Type, Variables}}, Acc0, Fun) when
    Kind =:= type; Kind =:= opaque; Kind =:= nominal
->
    {[Anno1], [Type1, Variables1], Acc} = annotated(Anno, [Type, Variables], Acc0, Fun),
    {{attribute, Anno1, Kind, {Name, Type1, Variables1}}, Acc};
modify({attribute, Anno, Name, Value}, Acc0, Fun) ->
    {Anno1, Acc} = Fun(Anno, Acc0),
    {{attribute, Anno1, Name, Value}, Acc};
modify({Info, _} = Node, Acc, _Fun) when Info =:= warning; Info =:= error; Info =:= eof ->
    {Node, Acc};
modify({clauses, Clauses}, Acc0, Fun) ->
    {Clauses1, Acc} = modify(Clauses, Acc0, Fun),
    {{clauses, Clauses1}, Acc};
modify({typed_record_field, Field, Type}, Acc0, Fun) ->
    {[Field1, Type1], Acc} = modify([Field, Type], Acc0, Fun),
    {{typed_record_field, Field1, Type1}, Acc};
modify({Tag, Anno}, Acc0, Fun) ->
    {Anno1, Acc} = Fun(Anno, Acc0),
    {{Tag, Anno1}, Acc};
modify({Tag, Anno, E1}, Acc0, Fun) ->
    {[Anno1], [E2], Acc} = annotated(Anno, [E1], Acc0, Fun),
    {{Tag, Anno1, E2}, Acc};
modify({record, Anno, Name, Fields}, Acc0, Fun) ->
    {[Anno1], [Fields1], Acc} = annotated(Anno, [Fields], Acc0, Fun),
    {{record, Anno1, Name, Fields1}, Acc};
modify({record, Anno, Expr, Name, Fields}, Acc0, Fun) ->
    {[Anno1], [Expr1, Fields1], Acc} = annotated(Anno, [Expr, Fields], Acc0, Fun),
    {{record, Anno1, Expr1, Name, Fields1}, Acc};
modify({record_field, Anno, Expr, Name, Field}, Acc0, Fun) ->
    {[Anno1], [Expr1], Acc} = annotated(Anno, [Expr], Acc0, Fun),
    {{record_field, Anno1, Expr1, Name, Field}, Acc};
modify({Tag, Anno, E1, E2}, Acc0, Fun) ->
    {[Anno1], [F1, F2], Acc} = annotated(Anno, [E1, E2], Acc0, Fun),
    {{Tag, Anno1, F1, F2}, Acc};
modify({bin_element, Anno, Value, Size, Types}, Acc0, Fun) ->
    {[Anno1], [Value1, Size1], Acc} = annotated(Anno, [Value, Size], Acc0, Fun),
    {{bin_element, Anno1, Value1, Size1, Types}, Acc};
modify({Tag, Anno, E1, E2, E3}, Acc0, Fun) ->
    {[Anno1], [F1, F2, F3], Acc} = annotated(Anno, [E1, E2, E3], Acc0, Fun),
    {{Tag, Anno1, F1, F2, F3}, Acc};
modify({Tag, Anno, E1, E2, E3, E4}, Acc0, Fun) ->
    {[Anno1], [F1, F2, F3, F4], Acc} = annotated(Anno, [E1, E2, E3, E4], Acc0, Fun),
    {{Tag, Anno1, F1, F2, F3, F4}, Acc};
modify([Head | Tail], Acc0, Fun) ->
    {Head1, Acc1} = modify(Head, Acc0, Fun),
    {Tail1, Acc} = modify(Tail, Acc1, Fun),
    {[Head1 | Tail1], Acc};
modify([], Acc, _Fun) ->
    {[], Acc};
modify(Leaf, Acc, _Fun) when not is_tuple(Leaf), not is_list(Leaf) ->
    {Leaf, Acc}.

%% A node's annotation, then its children in order.
annotated(Anno, Children, Acc0, Fun) ->
    {Anno1, Acc1} = Fun(Anno, Acc0),
    {Children1, Acc} = modify(Children, Acc1, Fun),
    {[Anno1], Children1, Acc}.

first_anno(_) -> clause_builtin_placeholder.
func_prec() -> clause_builtin_placeholder.
inop_prec(_) -> clause_builtin_placeholder.
max_prec() -> clause_builtin_placeholder.
normalise(_) -> clause_builtin_placeholder.
preop_prec(_) -> clause_builtin_placeholder.
tokens(_) -> clause_builtin_placeholder.
tokens(_, _) -> clause_builtin_placeholder.
type_inop_prec(_) -> clause_builtin_placeholder.
type_preop_prec(_) -> clause_builtin_placeholder.
