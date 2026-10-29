%% Clause's ms_transform (plan 21 step LIB72, docs/libraries/library.md): the translation of literal funs given to ets:fun2ms/1
%% and dbg:fun2ms/1 into match specifications, over the abstract format. parse_transform/2 rewrites the calls inside
%% a module's forms; transform_from_shell/3 translates the clauses of one shell fun into a match specification term.
%% The translation runs in C++ (clause_ms_transform, C++ ms_transform), which qlc_pt also calls directly; this module
%% shapes its results, formats its errors and turns a shell fun's specification into a term.
-module(ms_transform).
-export([
    format_error/1,
    parse_transform/2,
    parse_transform_info/0,
    transform_from_shell/3
]).

%% Error codes of OTP's ms_transform; guard errors are the general codes, body errors add 100.
-define(BODY_BASE, 100).
-define(ERR_NOFUN, 1).
-define(ERR_ETS_HEAD, 2).
-define(ERR_DBG_HEAD, 3).
-define(ERR_HEADMATCH, 4).
-define(ERR_SEMI_GUARD, 5).
-define(ERR_UNBOUND_VARIABLE, 6).
-define(ERR_HEADBADREC, 7).
-define(ERR_HEADBADFIELD, 8).
-define(ERR_HEADMULTIFIELD, 9).
-define(ERR_HEADDOLLARATOM, 10).
-define(ERR_HEADBINMATCH, 11).
-define(ERR_GENMATCH, 16).
-define(ERR_GENLOCALCALL, 17).
-define(ERR_GENELEMENT, 18).
-define(ERR_GENBADFIELD, 19).
-define(ERR_GENBADREC, 20).
-define(ERR_GENMULTIFIELD, 21).
-define(ERR_GENREMOTECALL, 22).
-define(ERR_GENBINCONSTRUCT, 23).
-define(ERR_GENDISALLOWEDOP, 24).
-define(WARN_SHADOW_VAR, 50).

format_error({?WARN_SHADOW_VAR, Name}) ->
    text("variable ~p shadowed in ms_transform fun head", [Name]);
format_error(?ERR_NOFUN) ->
    "Parameter of ets/dbg:fun2ms/1 is not a literal fun";
format_error(?ERR_ETS_HEAD) ->
    "ets:fun2ms requires fun with single variable or tuple parameter";
format_error(?ERR_DBG_HEAD) ->
    "dbg:fun2ms requires fun with single variable or list parameter";
format_error(?ERR_HEADMATCH) ->
    "in fun head, only matching (=) on toplevel can be translated into match_spec";
format_error(?ERR_SEMI_GUARD) ->
    "fun with semicolon (;) in guard cannot be translated into match_spec";
format_error(?ERR_GENMATCH) ->
    "fun with guard matching ('=' in guard) is illegal as match_spec as well";
format_error(?ERR_GENMATCH + ?BODY_BASE) ->
    "fun with body matching ('=' in body) is illegal as match_spec";
format_error({?ERR_UNBOUND_VARIABLE, Name}) ->
    text("the variable ~s is unbound, cannot translate into match_spec", [Name]);
format_error({?ERR_HEADBADREC, Name}) ->
    text("fun head contains unknown record type ~tw", [Name]);
format_error({?ERR_HEADBADFIELD, Record, Field}) ->
    text("fun head contains reference to unknown field ~tw in record type ~tw", [Field, Record]);
format_error({?ERR_HEADMULTIFIELD, Record, Field}) ->
    text("fun head contains already defined field ~tw in record type ~tw", [Field, Record]);
format_error({?ERR_HEADDOLLARATOM, Atom}) ->
    text("fun head contains atom ~w, which conflics with reserved atoms in match_spec heads", [Atom]);
format_error({?ERR_HEADBINMATCH, Name}) ->
    text(
        "fun head contains bit syntax matching of variable ~w, which cannot be translated into match_spec",
        [Name]
    );
format_error(Error) ->
    case part_error(Error) of
        {Part, Code, Details} -> part_message(Code, Part, Details);
        none -> text("Unknown error code ~tw", [Error])
    end.

%% A guard or body error: its part, general code and details; none for other terms.
part_error(Error) when is_tuple(Error), tuple_size(Error) >= 2, is_integer(element(1, Error)) ->
    [Code | Details] = tuple_to_list(Error),
    case Code >= ?BODY_BASE of
        true -> {body, Code - ?BODY_BASE, Details};
        false -> {guard, Code, Details}
    end;
part_error(_) ->
    none.

part_message(?ERR_GENLOCALCALL, Part, [Name, Arity]) ->
    text(
        "fun containing the local function call '~tw/~w' (called in ~w) cannot be translated into match_spec",
        [
            Name, Arity, Part
        ]
    );
part_message(?ERR_GENREMOTECALL, Part, [Module, Name, Arity]) ->
    text(
        "fun containing the remote function call '~w:~tw/~w' (called in ~w) cannot be translated into match_spec",
        [Module, Name, Arity, Part]
    );
part_message(?ERR_GENELEMENT, Part, [Element]) ->
    text("the language element ~ts (in ~w) cannot be translated into match_spec", [Element, Part]);
part_message(?ERR_GENBINCONSTRUCT, Part, [Name]) ->
    text("bit syntax construction with variable ~w (in ~w) cannot be translated into match_spec", [
        Name, Part
    ]);
part_message(?ERR_GENBADREC, Part, [Name]) ->
    text("fun ~w contains unknown record type ~tw", [Part, Name]);
part_message(?ERR_GENBADFIELD, Part, [Record, Field]) ->
    text("fun ~w contains reference to unknown field ~tw in record type ~tw", [Part, Field, Record]);
part_message(?ERR_GENMULTIFIELD, Part, [Record, Field]) ->
    text("fun ~w contains already defined field ~tw in record type ~tw", [Part, Field, Record]);
part_message(?ERR_GENDISALLOWEDOP, guard, [Operator]) ->
    text("the operator ~w is not allowed in guards", [Operator]);
part_message(?ERR_GENDISALLOWEDOP, body, [Operator]) ->
    text("the operator ~w is not allowed in function bodies", [Operator]);
part_message(Code, Part, Details) ->
    Base =
        case Part of
            body -> ?BODY_BASE;
            guard -> 0
        end,
    text("Unknown error code ~tw", [list_to_tuple([Code + Base | Details])]).

text(Format, Arguments) ->
    lists:flatten(io_lib:format(Format, Arguments)).

parse_transform_info() ->
    #{error_location => column}.

%% parse_transform(Forms, Options): the forms with every ets:fun2ms/1 and dbg:fun2ms/1 of a literal fun replaced by
%% its match specification; {warning, Forms, Warnings} when a head variable shadows a bound one, {error, ...} for
%% the first fun that cannot be translated.
parse_transform(Forms, _Options) ->
    guarded(fun() ->
        case clause_ms_transform:transform_forms(Forms) of
            {error, File, Anno, Reason} ->
                {error, [{File, [{location(Anno), ?MODULE, Reason}]}], []};
            {ok, Transformed, _, [], Used} ->
                silence_records(Transformed, Used);
            {ok, Transformed, File, Warnings, Used} ->
                Reports = [{File, [{Location, ?MODULE, Reason}]} || {Location, Reason} <- Warnings],
                {warning, silence_records(Transformed, Used), Reports}
        end
    end).

%% transform_from_shell(Dialect, Clauses, Bindings): the match specification of a shell fun's clauses, with the
%% variables imported from the shell replaced by their values from Bindings.
transform_from_shell(Dialect, Clauses, Bindings) ->
    try
        Spec = guarded(fun() -> shell_spec(Dialect, Clauses) end),
        normalise(bind_imports(Spec, Bindings))
    catch
        throw:{ms_error, Anno, Reason} ->
            {error, [{"TOP_LEVEL", [{location(Anno), ?MODULE, Reason}]}], []}
    end.

shell_spec(Dialect, Clauses) ->
    case clause_ms_transform:clause_list(Dialect, Clauses) of
        {ok, Spec} -> Spec;
        {error, Anno, Reason} -> throw({ms_error, Anno, Reason})
    end.

%% Run a translation; an error or exit in it becomes the exit OTP's catch gives, an ms_error throw passes.
guarded(Translate) ->
    try
        Translate()
    catch
        error:Reason:Stack -> exit({Reason, Stack});
        exit:Reason -> exit(Reason)
    end.

location(unknown) -> none;
location(Anno) -> erl_anno:location(Anno).

%% Records replaced by tuples get -compile({nowarn_unused_record, [Names]}) first, so lint does not report them.
silence_records(Forms, []) ->
    Forms;
silence_records(Forms, Used) ->
    [{attribute, erl_anno:new(0), compile, {nowarn_unused_record, [lists:usort(Used)]}} | Forms].

%% Replace the imported variables of a shell fun's specification by their values; an unknown one is an error.
bind_imports(List, Bindings) when is_list(List) ->
    [bind_imports(Element, Bindings) || Element <- List];
bind_imports({var, Anno, Name}, Bindings) ->
    case lists:keyfind(Name, 1, Bindings) of
        {Name, Value} -> {frozen, Anno, Value};
        false -> throw({ms_error, Anno, {?ERR_UNBOUND_VARIABLE, atom_to_list(Name)}})
    end;
bind_imports(Tuple, Bindings) when is_tuple(Tuple) ->
    list_to_tuple(bind_imports(tuple_to_list(Tuple), Bindings));
bind_imports(Other, _) ->
    Other.

%% The term an abstract match specification stands for.
normalise({frozen, _, Term}) ->
    Term;
normalise({char, _, Char}) ->
    Char;
normalise({integer, _, Integer}) ->
    Integer;
normalise({float, _, Float}) ->
    Float;
normalise({atom, _, Atom}) ->
    Atom;
normalise({string, _, String}) ->
    String;
normalise({nil, _}) ->
    [];
normalise({bin, _, Elements}) ->
    <<<<(segment(Element))/bitstring>> || Element <- Elements>>;
normalise({cons, _, Head, Tail}) ->
    [normalise(Head) | normalise(Tail)];
normalise({op, _, '++', Left, Right}) ->
    normalise(Left) ++ normalise(Right);
normalise({tuple, _, Elements}) ->
    list_to_tuple([normalise(Element) || Element <- Elements]);
normalise({map, _, Fields}) ->
    maps:from_list([normalise_field(Field) || Field <- Fields]);
normalise({op, _, '+', {Kind, _, Value}}) when Kind =:= char; Kind =:= integer; Kind =:= float ->
    Value;
normalise({op, _, '-', {Kind, _, Value}}) when Kind =:= char; Kind =:= integer; Kind =:= float ->
    -Value.

normalise_field({map_field_assoc, _, Key, Value}) -> {normalise(Key), normalise(Value)}.

%% One constant bit syntax segment; a string value gives one segment per character.
segment({bin_element, Anno, {string, _, String}, Size, Types}) ->
    <<
        <<(segment({bin_element, Anno, {integer, Anno, Char}, Size, Types}))/bitstring>>
     || Char <- String
    >>;
segment({bin_element, _, Value, Size, Types}) ->
    {Type, Unit, Endian} = segment_types(Types),
    Bits =
        case Size of
            default -> default;
            _ -> normalise(Size) * Unit
        end,
    build(normalise(Value), Type, Bits, Endian).

%% The type, unit and endianness of a segment's type list, with their defaults.
segment_types(default) ->
    segment_types([]);
segment_types(Specifiers) ->
    Type = last_of(
        [integer, float, binary, bytes, bitstring, bits, utf8, utf16, utf32], Specifiers, integer
    ),
    Endian = last_of([big, little, native], Specifiers, big),
    DefaultUnit =
        case Type of
            binary -> 8;
            bytes -> 8;
            _ -> 1
        end,
    Unit = lists:foldl(
        fun
            ({unit, U}, _) -> U;
            (_, Acc) -> Acc
        end,
        DefaultUnit,
        Specifiers
    ),
    {Type, Unit, Endian}.

last_of(Choices, Specifiers, Default) ->
    lists:foldl(
        fun(Specifier, Acc) ->
            case lists:member(Specifier, Choices) of
                true -> Specifier;
                false -> Acc
            end
        end,
        Default,
        Specifiers
    ).

build(Value, integer, default, Endian) -> build(Value, integer, 8, Endian);
build(Value, integer, Bits, big) -> <<Value:Bits/big>>;
build(Value, integer, Bits, little) -> <<Value:Bits/little>>;
build(Value, integer, Bits, native) -> <<Value:Bits/native>>;
build(Value, float, default, Endian) -> build(Value, float, 64, Endian);
build(Value, float, Bits, big) -> <<Value:Bits/float-big>>;
build(Value, float, Bits, little) -> <<Value:Bits/float-little>>;
build(Value, float, Bits, native) -> <<Value:Bits/float-native>>;
build(Value, utf8, _, _) -> <<Value/utf8>>;
build(Value, utf16, _, little) -> <<Value/utf16-little>>;
build(Value, utf16, _, native) -> <<Value/utf16-native>>;
build(Value, utf16, _, big) -> <<Value/utf16>>;
build(Value, utf32, _, little) -> <<Value/utf32-little>>;
build(Value, utf32, _, native) -> <<Value/utf32-native>>;
build(Value, utf32, _, big) -> <<Value/utf32>>;
build(Value, _, default, _) when is_bitstring(Value) -> Value;
build(Value, _, Bits, _) -> <<Value:Bits/bitstring>>.
