%% Clause's erl_pp (plan 21 step LIB34, docs/library.md): the abstract format printed as Erlang source, written from
%% OTP's documentation and observed output. Printing has two passes. The first turns a form into layout items:
%% characters, leaves of known width, words, atoms, strings, chars and values written later, and the groups below.
%% The second lays the items out: a group goes on one line when it fits the line width, otherwise its parts go on
%% lines of their own, indented. Hooks print the forms erl_pp does not know.
%%
%% Groups: [I | Is] (I then Is, with no space), {list, Parts} (on one line, or parts on lines of their own),
%% {first, I, Part} (Part after I, indented by I's width), {seq, Before, After, Separator, Parts} (separated parts
%% between Before and After), {prefer_nl, Separator, Parts} (one part per line unless the indentation is negative),
%% {force_nl, Comment, I} (a comment line before I). A part is an item, {step, I1, I2} (I2 on the next line, one
%% indentation in, when the whole does not fit) or {cstep, I1, I2} (as step, but I2 stays after a narrow I1).
-module(erl_pp).
-export([
    attribute/1,
    attribute/2,
    expr/1,
    expr/2,
    expr/3,
    expr/4,
    exprs/1,
    exprs/2,
    exprs/3,
    form/1,
    form/2,
    function/1,
    function/2,
    guard/1,
    guard/2,
    legalize_vars/1
]).

-define(LINE_WIDTH, 72).
-define(INDENT, 4).
%% A string printed across lines is cut in pieces of at least this many characters.
-define(MIN_PIECE, 5).

%% How leaves are written: values and atoms, singleton atom types, strings and chars; and the line width and
%% indentation step of the layout.
-record(pp, {value, singleton, string, char, width = ?LINE_WIDTH, indent = ?INDENT}).
%% The options of the first pass: the hook, the encoding and the options as given.
-record(opts, {hook, encoding, given}).

form(Form) -> form(Form, none).

form(Form, Options) -> layout(form_items(Form, opts(Options)), 0, pp(Options)).

attribute(Attribute) -> attribute(Attribute, none).

attribute(Attribute, Options) -> layout(attribute_items(Attribute, opts(Options)), 0, pp(Options)).

function(Function) -> function(Function, none).

function(Function, Options) -> layout(function_items(Function, opts(Options)), 0, pp(Options)).

guard(Guard) -> guard(Guard, none).

guard(Guard, Options) -> layout(guard_items(Guard, opts(Options)), 0, pp(Options)).

exprs(Es) -> exprs(Es, 0, none).

exprs(Es, Options) -> exprs(Es, 0, Options).

exprs(Es, I, Options) -> layout({seq, [], [], [$,], items(Es, opts(Options))}, I, pp(Options)).

expr(E) -> layout(item(E, 0, opts(none)), 0, pp(none)).

expr(E, Options) -> layout(item(E, 0, opts(Options)), 0, pp(Options)).

expr(E, I, Options) -> layout(item(E, 0, opts(Options)), I, pp(Options)).

expr(E, I, Precedence, Options) -> layout(item(E, Precedence, opts(Options)), I, pp(Options)).

%% legalize_vars(Function): the function with each variable whose name is not a valid Erlang variable (as the
%% compiler makes up when it expands records) renamed: in capitals, then with more X in front until it is unused.
legalize_vars({function, Anno, Name, Arity, Clauses}) ->
    {Valid, Invalid} = fold_vars(fun classify_var/2, {#{}, #{}}, Clauses),
    Renames = maps:fold(fun legal_name/3, Valid, Invalid),
    {function, Anno, Name, Arity,
        map_vars(fun({var, A, V}) -> {var, A, map_get(V, Renames)} end, Clauses)};
legalize_vars(Form) ->
    erlang:error(badarg, [Form]).

classify_var({var, _, Name}, {Valid, Invalid}) ->
    case atom_to_list(Name) of
        [First | _] = Text when First >= $a, First =< $z -> {Valid, Invalid#{Name => Text}};
        _ -> {Valid#{Name => Name}, Invalid}
    end.

legal_name(Name, Text, Used) ->
    Candidate = list_to_atom(string:to_upper(Text)),
    case Used of
        #{Candidate := _} -> legal_name(Name, [$X | Text], Used);
        #{} -> Used#{Name => Candidate}
    end.

fold_vars(F, Acc, Forms) when is_list(Forms) ->
    lists:foldl(fun(Form, A) -> fold_vars(F, A, Form) end, Acc, Forms);
fold_vars(F, Acc, {var, _, _} = V) ->
    F(V, Acc);
fold_vars(F, Acc, Form) when is_tuple(Form) -> fold_vars(F, Acc, tuple_to_list(Form));
fold_vars(_, Acc, _) ->
    Acc.

map_vars(F, Forms) when is_list(Forms) -> [map_vars(F, Form) || Form <- Forms];
map_vars(F, {var, _, _} = V) -> F(V);
map_vars(F, Form) when is_tuple(Form) -> list_to_tuple(map_vars(F, tuple_to_list(Form)));
map_vars(_, Form) -> Form.

%% Options

opts(Options) when is_list(Options) ->
    #opts{
        hook = proplists:get_value(hook, Options, none),
        encoding = encoding(Options),
        given = Options
    };
opts(Hook) ->
    #opts{hook = Hook, encoding = encoding([]), given = Hook}.

%% Only an options list chooses the encoding of the second pass; a bare hook prints in latin1.
pp(Options) when is_list(Options) ->
    Quote = proplists:get_bool(quote_singleton_atom_types, Options),
    PP = writers(encoding(Options), Quote),
    PP#pp{
        indent = proplists:get_value(indent, Options, ?INDENT),
        width = proplists:get_value(linewidth, Options, ?LINE_WIDTH)
    };
pp(_Hook) ->
    writers(latin1, false).

writers(latin1, Quote) ->
    Value = fun(V) -> io_lib:format("~p", [V]) end,
    #pp{
        value = Value,
        singleton = singleton_writer(Quote, Value, fun io_lib:write_string_as_latin1/2),
        string = fun io_lib:write_string_as_latin1/1,
        char = fun io_lib:write_char_as_latin1/1
    };
writers(unicode, Quote) ->
    Value = fun(V) -> io_lib:format("~tp", [V]) end,
    #pp{
        value = Value,
        singleton = singleton_writer(Quote, Value, fun io_lib:write_string/2),
        string = fun io_lib:write_string/1,
        char = fun io_lib:write_char/1
    }.

singleton_writer(true, _Value, Quoted) -> fun(A) -> Quoted(atom_to_list(A), $') end;
singleton_writer(false, Value, _Quoted) -> Value.

encoding(Options) ->
    case proplists:get_value(encoding, Options, utf8) of
        latin1 -> latin1;
        utf8 -> unicode;
        unicode -> unicode
    end.

%% First pass: forms

form_items({attribute, _, _, _} = Attribute, Opts) -> attribute_items(Attribute, Opts);
form_items({function, _, _, _, _} = Function, Opts) -> function_items(Function, Opts);
form_items({error, _} = Message, Opts) -> message(Message, Opts);
form_items({warning, _} = Message, Opts) -> message(Message, Opts);
form_items({eof, _}, _Opts) -> $\n.

message(Message, #opts{encoding = latin1}) -> leaf(io_lib:format("~p\n", [Message]));
message(Message, #opts{encoding = unicode}) -> leaf(io_lib:format("~tp\n", [Message])).

attribute_items({attribute, _, Kind, Type}, _Opts) when
    Kind =:= type; Kind =:= opaque; Kind =:= nominal
->
    [type_attribute(Kind, Type), leaf(".\n")];
attribute_items({attribute, _, Kind, Spec}, _Opts) when Kind =:= spec; Kind =:= callback ->
    [spec_attribute(Kind, Spec), leaf(".\n")];
attribute_items({attribute, _, Name, Arg}, Opts) ->
    [attribute_body(Name, Arg, Opts), leaf(".\n")].

attribute_body(module, {M, Params}, _Opts) ->
    A = anno0(),
    simple_attribute(module, [
        {var, A, dotted(M)},
        lists:foldr(fun(V, Tail) -> {cons, A, {var, A, V}, Tail} end, {nil, A}, Params)
    ]);
attribute_body(module, M, _Opts) ->
    simple_attribute(module, [{var, anno0(), dotted(M)}]);
attribute_body(export, Functions, _Opts) ->
    listed_attribute(export, name_arities(Functions));
attribute_body(import, Name, _Opts) when is_list(Name) ->
    simple_attribute(import, [{var, anno0(), dotted(Name)}]);
attribute_body(import, {From, Functions}, _Opts) ->
    listed_attribute(import, [leaf(dotted(From)), name_arities(Functions)]);
attribute_body(export_type, Types, _Opts) ->
    listed_attribute(export_type, name_arities(Types));
attribute_body(optional_callbacks, Functions, Opts) ->
    try
        listed_attribute(optional_callbacks, name_arities(Functions))
    catch
        _:_ -> simple_attribute(optional_callbacks, [abstract(Functions, Opts)])
    end;
attribute_body(file, {Name, Line}, _Opts) ->
    simple_attribute(file, [{string, anno0(), Name}, {integer, anno0(), Line}]);
attribute_body(record, {Name, Fields}, Opts) ->
    [{first, [leaf("-record("), {atom, Name}, $,], record_fields(Fields, Opts)}, $)];
attribute_body(native_record, {Name, Fields}, Opts) ->
    [{first, [leaf("-record #"), {atom, Name}], record_fields(Fields, Opts)}];
attribute_body(Name, Arg, Opts) ->
    simple_attribute(Name, [abstract(Arg, Opts)]).

abstract(Term, #opts{encoding = Encoding}) -> erl_parse:abstract(Term, [{encoding, Encoding}]).

simple_attribute(Name, Args) -> {first, [$-, {atom, Name}], arguments(Args, opts(none))}.

listed_attribute(Name, Args) -> {first, [$-, {atom, Name}], [{seq, $(, $), [$,], Args}]}.

%% A module or package name: the dotted parts of an old package name, or an atom.
dotted(['' | Parts]) -> [$. | dotted(Parts)];
dotted([Part]) -> io_lib:write(Part);
dotted([Part | Parts]) -> [io_lib:write(Part), $. | dotted(Parts)];
dotted(A) when is_atom(A) -> io_lib:write(A).

name_arities([]) ->
    ['[]'];
name_arities(Pairs) ->
    [
        {seq, $[, $], $,, [
            [{atom, Name}, leaf(io_lib:format("/~w", [Arity]))]
         || {Name, Arity} <- Pairs
        ]}
    ].

%% Types and specifications

type_attribute(Kind, {Name, Type, Vars}) ->
    {first, leaf("-" ++ atom_to_list(Kind) ++ " "),
        typed(call({atom, anno0(), Name}, Vars, 0, opts(none)), Type)}.

type_item(T) -> type_item(T, 0).

type_item({ann_type, _, [V, T]}, Prec) ->
    {L, P, R} = erl_parse:type_inop_prec('::'),
    paren(P, Prec, {list, [{cstep, [type_item(V, L), ' ::'], type_item(T, R)}]});
type_item({paren_type, _, [T]}, Prec) ->
    type_item(T, Prec);
type_item({type, _, union, Ts}, Prec) ->
    {_L, P, R} = erl_parse:type_inop_prec('|'),
    paren(P, Prec, {seq, [], [], [' |'], [type_item(T, R) || T <- Ts]});
type_item({type, _, list, [T]}, _) ->
    {seq, $[, $], $,, [type_item(T)]};
type_item({type, _, nonempty_list, [T]}, _) ->
    {seq, $[, $], [$,], [type_item(T), leaf("...")]};
type_item({type, Anno, nil, []}, _) ->
    item({nil, Anno}, 0, opts(none));
type_item({type, Anno, map, any}, _) ->
    named_type({atom, Anno, map}, []);
type_item({type, _, map, Pairs}, Prec) ->
    {P, _R} = erl_parse:type_preop_prec('#'),
    paren(P, Prec, {first, [$#], tuple_type(Pairs, fun map_pair_type/2)});
type_item({type, Anno, tuple, any}, _) ->
    named_type({atom, Anno, tuple}, []);
type_item({type, _, tuple, Ts}, _) ->
    tuple_type(Ts, fun type_item/2);
type_item({type, _, record, [{atom, _, Name} | Fields]}, Prec) ->
    {P, _R} = erl_parse:type_preop_prec('#'),
    paren(P, Prec, {first, [record_name(Name)], tuple_type(Fields, fun field_type/2)});
type_item({type, _, range, [_, _] = Bounds}, Prec) ->
    {_L, P, R} = erl_parse:type_inop_prec('..'),
    paren(P, Prec, separated(Bounds, '..', fun(E, Opts) -> item(E, R, Opts) end, opts(none)));
type_item({type, _, binary, [Base, Unit]}, _) ->
    binary_type(Base, Unit);
type_item({type, _, 'fun', []}, _) ->
    leaf("fun()");
type_item({type, _, 'fun', [{type, _, Args}, _]} = Fun, _) when Args =:= any; Args =:= product ->
    [fun_type(['fun', $(], Fun), $)];
type_item({type, _, 'fun', [{type, _, product, _}, _]} = Fun, _) ->
    [fun_type(['fun', $(], Fun), $)];
type_item({type, Anno, T, Ts}, _) ->
    named_type({atom, Anno, T}, Ts);
type_item({user_type, Anno, T, Ts}, _) ->
    named_type({atom, Anno, T}, Ts);
type_item({remote_type, Anno, [M, F, Ts]}, _) ->
    named_type({remote, Anno, M, F}, Ts);
type_item({atom, _, T}, _) ->
    {singleton_atom_type, T};
type_item(E, Prec) ->
    item(E, Prec, opts(none)).

%% <<_:Base, _:_*Unit>>, each part left out when 0.
binary_type(Base, Unit) ->
    P = erl_parse:max_prec(),
    Parts =
        [[leaf("_:"), item(Base, P, opts(none))] || not is_zero(Base)] ++
            [[leaf("_:_*"), item(Unit, P, opts(none))] || not is_zero(Unit)],
    case Parts of
        [] -> leaf("<<>>");
        _ -> {seq, '<<', '>>', [$,], Parts}
    end.

is_zero({integer, _, 0}) -> true;
is_zero(_) -> false.

map_pair_type({type, _, map_field_assoc, [K, V]}, Prec) ->
    {list, [{cstep, [type_item(K, Prec), leaf(" =>")], type_item(V, Prec)}]};
map_pair_type({type, _, map_field_exact, [K, V]}, Prec) ->
    {list, [{cstep, [type_item(K, Prec), leaf(" :=")], type_item(V, Prec)}]}.

field_type({type, _, field_type, [Name, Type]}, _Prec) -> typed(item(Name, 0, opts(none)), Type).

typed(Before, Type) -> {list, [{cstep, [Before, ' ::'], type_item(Type)}]}.

tuple_type([], _) -> leaf("{}");
tuple_type(Ts, F) -> {seq, ${, $}, [$,], [F(T, 0) || T <- Ts]}.

spec_attribute(Kind, {Function, Types}) ->
    Name =
        case Function of
            {F, _A} -> {atom, F};
            {M, F, _A} -> [{atom, M}, $:, {atom, F}]
        end,
    {first, leaf(lists:concat(["-", Kind, " "])),
        {list, [{first, Name, {prefer_nl, [$;], [signature(T) || T <- Types]}}]}}.

signature({type, _, bounded_fun, [Fun, Constraints]}) ->
    Guard = {list, [{step, 'when', separated(Constraints, [$,], fun constraint/2, opts(none))}]},
    {list, [{step, fun_type([], Fun), Guard}]};
signature(Fun) ->
    fun_type([], Fun).

constraint({type, _, constraint, [{atom, _, is_subtype}, [{var, _, _} = V, Type]]}, _Opts) ->
    typed(item(V, 0, opts(none)), Type);
constraint({type, _, constraint, [Tag, As]}, _Opts) ->
    named_type(Tag, As).

fun_type(Before, {type, _, 'fun', [Args, Result]}) ->
    {first, Before, {step, [type_arguments(Args), ' ->'], type_item(Result)}}.

type_arguments({type, _, any}) -> leaf("(...)");
type_arguments({type, _, product, Ts}) -> type_list(Ts).

named_type(Tag, Ts) -> {first, item(Tag, 0, opts(none)), type_list(Ts)}.

type_list(Ts) -> {seq, $(, $), [$,], [type_item(T, 0) || T <- Ts]}.

%% Functions, clauses and guards

function_items({function, _, Name, _Arity, Clauses}, Opts) ->
    [{prefer_nl, $;, [function_clause(Name, C, Opts) || C <- Clauses]}, leaf(".\n")].

function_clause(Name, {clause, Anno, Head, Guard, Body}, Opts) ->
    {step, guarded(call({atom, Anno, Name}, Head, 0, Opts), Guard, Opts), body(Body, Opts)}.

guarded(Before, Guard, Opts) -> [{list, [{step, Before, guard_items(Guard, Opts)}]}, ' ->'].

guard_items([G | _] = Guard, Opts) when is_list(G) ->
    {list, [{step, 'when', separated(Guard, [$;], fun conjunction/2, Opts)}]};
guard_items([_ | _] = Tests, Opts) ->
    guard_items([Tests], Opts);
guard_items([], _Opts) ->
    [].

conjunction(Tests, Opts) -> separated(Tests, [$,], fun item/2, Opts).

body([E], Opts) -> item(E, Opts);
body(Es, Opts) -> {prefer_nl, [$,], items(Es, Opts)}.

clauses(F, Cs, Opts) -> {prefer_nl, [$;], [F(C, Opts) || C <- Cs]}.

if_clause({clause, _, [], Guard, Body}, Opts) ->
    {step, [if_guard(Guard, Opts), ' ->'], body(Body, Opts)}.

if_guard([G | _] = Guard, Opts) when is_list(G) -> separated(Guard, $;, fun conjunction/2, Opts);
if_guard([_ | _] = Tests, Opts) -> if_guard([Tests], Opts);
if_guard([], _Opts) -> leaf("true").

case_clause({clause, _, [P], Guard, Body}, Opts) ->
    {step, guarded(item(P, 0, Opts), Guard, Opts), body(Body, Opts)}.

catch_clause({clause, _, [{tuple, _, [Class, Reason, Stack]}], Guard, Body}, Opts) ->
    Pattern = [item(Class, 0, Opts), $:, item(Reason, 0, Opts)],
    WithStack =
        case Stack of
            {var, _, '_'} -> Pattern;
            _ -> Pattern ++ [$:, item(Stack, 0, Opts)]
        end,
    {step, guarded(WithStack, Guard, Opts), body(Body, Opts)}.

fun_clauses(Cs, Opts, none) ->
    {prefer_nl, [$;], [fun_clause(C, Opts) || C <- Cs]};
fun_clauses(Cs, Opts, Name) ->
    {prefer_nl, [$;], [
        begin
            {step, Head, Body} = fun_clause(C, Opts),
            {step, [atom_to_list(Name), Head], Body}
        end
     || C <- Cs
    ]}.

fun_clause({clause, _, Args, Guard, Body}, Opts) ->
    {step, guarded(arguments(Args, Opts), Guard, Opts), body(Body, Opts)}.

cond_clause({clause, _, [], [[E]], Body}, Opts) -> {step, [item(E, Opts), ' ->'], body(Body, Opts)}.

qualifiers(Qs, Opts) -> {prefer_nl, [$,], [qualifier(Q, Opts) || Q <- Qs]}.

qualifier({zip, _, Qs}, Opts) ->
    {prefer_nl, ["&&"], [qualifier(Q, Opts) || Q <- Qs]};
qualifier({Generator, _, P, E}, Opts) when
    Generator =:= m_generate; Generator =:= m_generate_strict
->
    {list, [{step, [map_field(P, Opts), leaf(arrow(Generator))], item(E, 0, Opts)}]};
qualifier({Generator, _, P, E} = Q, Opts) ->
    case arrow(Generator) of
        none -> item(Q, 0, Opts);
        Arrow -> {list, [{step, [item(P, 0, Opts), leaf(Arrow)], item(E, 0, Opts)}]}
    end;
qualifier(Q, Opts) ->
    item(Q, 0, Opts).

arrow(generate) -> " <-";
arrow(generate_strict) -> " <:-";
arrow(b_generate) -> " <=";
arrow(b_generate_strict) -> " <:=";
arrow(m_generate) -> " <-";
arrow(m_generate_strict) -> " <:-";
arrow(_) -> none.

%% First pass: expressions

items(Es, Opts) -> [item(E, Opts) || E <- Es].

item(E, Opts) -> item(E, 0, Opts).

item({var, _, V}, _, _) when is_integer(V) -> leaf(io_lib:format("_~w", [V]));
item({var, _, V}, _, _) ->
    leaf(io_lib:format("~ts", [V]));
item({char, _, C}, _, _) ->
    {char, C};
item({integer, _, N}, _, _) ->
    leaf(io_lib:write(N));
item({float, _, F}, _, _) ->
    leaf(io_lib:write(F));
item({atom, _, A}, _, _) ->
    {atom, A};
item({string, _, S}, _, _) ->
    {string, S};
item({nil, _}, _, _) ->
    '[]';
item({cons, _, H, T}, _, Opts) ->
    list_items(T, [H], Opts);
item({lc, _, E, Qs}, _, Opts) ->
    comprehension($[, item(E, Opts), Qs, $], Opts);
item({bc, _, E, Qs}, _, Opts) ->
    comprehension('<<', item(E, erl_parse:max_prec(), Opts), Qs, '>>', Opts);
item({mc, _, E, Qs}, _, Opts) ->
    comprehension('#{', map_field(E, Opts), Qs, $}, Opts);
item({tuple, _, Es}, _, Opts) ->
    tuple(Es, fun item/2, Opts);
item({record_index, _, Name, F}, Prec, Opts) ->
    {P, R} = erl_parse:preop_prec('#'),
    paren(P, Prec, [record_name(Name), $., item(F, R, Opts)]);
item({record, _, Name, Fs}, Prec, Opts) ->
    {P, _R} = erl_parse:preop_prec('#'),
    paren(P, Prec, {first, record_name(Name), record_fields(Fs, Opts)});
item({record_field, _, Rec, Name, F}, Prec, Opts) ->
    {L, P, R} = erl_parse:inop_prec('#'),
    [$# | Bare] = record_name(Name),
    paren(P, Prec, [item(Rec, L, Opts), [hash_space(Rec, [$#]), Bare, $.], item(F, R, Opts)]);
item({record, _, Rec, Name, Fs}, Prec, Opts) ->
    {L, P, _R} = erl_parse:inop_prec('#'),
    paren(
        P,
        Prec,
        {first, [item(Rec, L, Opts), hash_space(Rec, []), record_name(Name)],
            record_fields(Fs, Opts)}
    );
item({record_field, _, {atom, _, ''}, F}, Prec, Opts) ->
    {_L, P, R} = erl_parse:inop_prec('.'),
    paren(P, Prec, [$., item(F, R, Opts)]);
item({record_field, _, Rec, F}, Prec, Opts) ->
    {L, P, R} = erl_parse:inop_prec('.'),
    paren(P, Prec, [item(Rec, L, Opts), $., item(F, R, Opts)]);
item({map, _, Fs}, Prec, Opts) ->
    {P, _R} = erl_parse:preop_prec('#'),
    paren(P, Prec, {first, $#, tuple(Fs, fun map_field/2, Opts)});
item({map, _, Map, Fs}, Prec, Opts) ->
    {L, P, _R} = erl_parse:inop_prec('#'),
    paren(
        P,
        Prec,
        {first, [item(Map, L, Opts) | hash_space(Map, [$#])], tuple(Fs, fun map_field/2, Opts)}
    );
item({block, _, Es}, _, Opts) ->
    {list, [{step, 'begin', body(Es, Opts)}, {reserved, 'end'}]};
item({'if', _, Cs}, _, Opts) ->
    {list, [{step, 'if', clauses(fun if_clause/2, Cs, Opts)}, {reserved, 'end'}]};
item({'case', _, E, Cs}, _, Opts) ->
    Head = {list, [{step, 'case', item(E, Opts)}, {reserved, 'of'}]},
    {list, [{step, Head, clauses(fun case_clause/2, Cs, Opts)}, {reserved, 'end'}]};
item({'cond', _, Cs}, _, Opts) ->
    {list, [{step, leaf("cond"), clauses(fun cond_clause/2, Cs, Opts)}, {reserved, 'end'}]};
item({'receive', _, Cs}, _, Opts) ->
    {list, [{step, 'receive', clauses(fun case_clause/2, Cs, Opts)}, {reserved, 'end'}]};
item({'receive', _, Cs, Timeout, After}, _, Opts) ->
    AfterItems = {list, [{step, [item(Timeout, Opts), ' ->'], body(After, Opts)}]},
    {list, [
        {step, 'receive', clauses(fun case_clause/2, Cs, Opts)},
        {step, 'after', AfterItems},
        {reserved, 'end'}
    ]};
item({'fun', _, {function, F, A}}, _, _Opts) ->
    [leaf("fun "), {atom, F}, leaf(io_lib:format("/~w", [A]))];
item({'fun', A, {function, _, _} = Function, Extra}, Prec, Opts) ->
    {force_nl, fun_info(Extra), item({'fun', A, Function}, Prec, Opts)};
item({'fun', _, {function, M, F, A}}, _, Opts) ->
    ["fun ", item(M, Opts), $:, item(F, Opts), $/, item(A, Opts)];
item({'fun', _, {clauses, Cs}}, _, Opts) ->
    {list, [{first, 'fun', fun_clauses(Cs, Opts, none)}, {reserved, 'end'}]};
item({named_fun, _, Name, Cs}, _, Opts) ->
    {list, [{first, ['fun', " "], fun_clauses(Cs, Opts, Name)}, {reserved, 'end'}]};
item({'fun', _, {clauses, Cs}, Extra}, _, Opts) ->
    {force_nl, fun_info(Extra),
        {list, [{first, 'fun', fun_clauses(Cs, Opts, none)}, {reserved, 'end'}]}};
item({named_fun, _, Name, Cs, Extra}, _, Opts) ->
    {force_nl, fun_info(Extra),
        {list, [{first, ['fun', " "], fun_clauses(Cs, Opts, Name)}, {reserved, 'end'}]}};
item({call, _, {remote, _, {atom, _, M}, {atom, _, F} = Local} = Remote, Args}, Prec, Opts) ->
    %% An auto-imported BIF prints without its module, except float/1.
    case erl_internal:bif(M, F, length(Args)) andalso F =/= float of
        true -> call(Local, Args, Prec, Opts);
        false -> call(Remote, Args, Prec, Opts)
    end;
item({call, _, Name, Args}, Prec, Opts) ->
    call(Name, Args, Prec, Opts);
item({'try', _, Es, Cases, Catches, After}, _, Opts) ->
    try_items(Es, Cases, Catches, After, Opts);
item({'catch', _, E}, Prec, Opts) ->
    {P, R} = erl_parse:preop_prec('catch'),
    paren(P, Prec, {list, [{step, 'catch', item(E, R, Opts)}]});
item({'maybe', _, Es}, _, Opts) ->
    {list, [{step, 'maybe', body(Es, Opts)}, {reserved, 'end'}]};
item({'maybe', _, Es, {'else', _, Cs}}, _, Opts) ->
    {list, [
        {step, 'maybe', body(Es, Opts)},
        {step, 'else', clauses(fun case_clause/2, Cs, Opts)},
        {reserved, 'end'}
    ]};
item({maybe_match, _, L, R}, _, Opts) ->
    {list, [{cstep, [item(L, 0, Opts), leaf(" ?=")], item(R, 0, Opts)}]};
item({match, _, L, R}, Prec, Opts) ->
    {LP, P, RP} = erl_parse:inop_prec('='),
    paren(P, Prec, {list, [{cstep, [item(L, LP, Opts), ' ='], item(R, RP, Opts)}]});
item({op, _, Op, A}, Prec, Opts) when Op =:= '+'; Op =:= '-' ->
    {P, R} = erl_parse:preop_prec(Op),
    paren(P, Prec, [{reserved, leaf(atom_to_list(Op))}, item(A, R, Opts)]);
item({op, _, Op, A}, Prec, Opts) ->
    {P, R} = erl_parse:preop_prec(Op),
    paren(P, Prec, [{reserved, leaf(io_lib:format("~s ", [Op]))}, item(A, R, Opts)]);
item({op, _, Op, L, R}, Prec, Opts) ->
    {LP, P, RP} = erl_parse:inop_prec(Op),
    Parts = [item(L, LP, Opts), {reserved, leaf(io_lib:format("~s", [Op]))}, item(R, RP, Opts)],
    case Op =:= 'andalso' orelse Op =:= 'orelse' of
        true -> paren(P, Prec, {prefer_nl, [[]], Parts});
        false -> paren(P, Prec, {list, Parts})
    end;
item({remote, _, M, F}, Prec, Opts) ->
    {L, P, R} = erl_parse:inop_prec(':'),
    paren(P, Prec, [item(M, L, Opts), $:, item(F, R, Opts)]);
item({bin, _, []}, _, _Opts) ->
    leaf("<<>>");
item({bin, _, Fs}, _, Opts) ->
    ['<<', separated(Fs, $,, fun bin_element/2, Opts), '>>'];
item({debug_line, Line, Index}, _, _Opts) ->
    leaf(io_lib:format("beam_instruction:debug_line(~p, ~p)", [Line, Index]));
item({executable_line, Line, Index}, _, _Opts) ->
    leaf(io_lib:format("beam_instruction:executable_line(~p, ~p)", [Line, Index]));
item({value, _, V}, _, _) ->
    {value, V};
item(Other, _Prec, #opts{hook = none}) ->
    leaf(io_lib:format("INVALID-FORM:~w:", [Other]));
item(E, Prec, #opts{hook = {M, F, Extra}}) when M =/= 'fun' ->
    {ehook, E, Prec, {M, F, Extra}};
item(E, Prec, #opts{hook = Hook, given = Options}) ->
    {hook, E, Prec, Hook, Options}.

try_items(Es, Cases, Catches, After, Opts) ->
    Try =
        case Cases of
            [] ->
                {step, 'try', body(Es, Opts)};
            _ ->
                {step, {list, [{step, 'try', body(Es, Opts)}, {reserved, 'of'}]},
                    clauses(fun case_clause/2, Cases, Opts)}
        end,
    CatchPart = [{step, 'catch', clauses(fun catch_clause/2, Catches, Opts)} || Catches =/= []],
    AfterPart = [{step, 'after', body(After, Opts)} || After =/= []],
    {list, [Try] ++ CatchPart ++ AfterPart ++ [{reserved, 'end'}]}.

comprehension(Open, Template, Qs, Close, Opts) ->
    Inner = {list, [{step, [Template, leaf(" ||")], qualifiers(Qs, Opts)}]},
    {list, [{seq, Open, [], [[]], [{force_nl, leaf(" "), [Inner]}]}, Close]}.

%% An integer or fun before # is kept apart from it by a space.
hash_space({integer, _, _}, Rest) -> [$\s | Rest];
hash_space({'fun', _, {function, _, _}}, Rest) -> [$\s | Rest];
hash_space({'fun', _, {function, _, _, _}}, Rest) -> [$\s | Rest];
hash_space(_, Rest) -> Rest.

call(Name, Args, Prec, Opts) ->
    {F, P} = erl_parse:func_prec(),
    paren(P, Prec, {first, item(Name, F, Opts), arguments(Args, Opts)}).

fun_info(Extra) -> [leaf("% fun-info: "), {value, Extra}].

bin_element({bin_element, _, E, Size, Types}, Opts) ->
    P = erl_parse:max_prec(),
    Value = item(E, P, Opts),
    Sized =
        case Size of
            default -> Value;
            _ -> [Value, $:, item(Size, P, Opts)]
        end,
    case Types of
        default -> Sized;
        _ -> [Sized, $/ | lists:join($-, [bin_type(T) || T <- Types])]
    end.

bin_type({A, B}) ->
    [item(erl_parse:abstract(A), opts(none)), $:, item(erl_parse:abstract(B), opts(none))];
bin_type(T) ->
    item(erl_parse:abstract(T), opts(none)).

record_name({M, N}) when is_atom(M), is_atom(N) -> [$#, {atom, M}, $:, {atom, N}];
record_name([]) -> [$#, $_];
record_name(N) when is_atom(N) -> [$#, {atom, N}].

record_fields(Fs, Opts) -> tuple(Fs, fun record_field/2, Opts).

record_field({record_field, _, F, V}, Opts) ->
    {L, _P, R} = erl_parse:inop_prec('='),
    {list, [{cstep, [item(F, L, Opts), ' ='], item(V, R, Opts)}]};
record_field({typed_record_field, {record_field, _, F, V}, Type}, Opts) ->
    {L, _P, R} = erl_parse:inop_prec('='),
    {list, [{cstep, [item(F, L, Opts), ' ='], typed(item(V, R, Opts), Type)}]};
record_field({typed_record_field, Field, Type}, Opts) ->
    typed(record_field(Field, Opts), Type);
record_field({record_field, _, F}, Opts) ->
    item(F, 0, Opts).

map_field({map_field_assoc, _, K, V}, Opts) ->
    {list, [{step, [item(K, 0, Opts), leaf(" =>")], item(V, 0, Opts)}]};
map_field({map_field_exact, _, K, V}, Opts) ->
    {list, [{step, [item(K, 0, Opts), leaf(" :=")], item(V, 0, Opts)}]}.

list_items({cons, _, H, T}, Es, Opts) -> list_items(T, [H | Es], Opts);
list_items({nil, _}, Es, Opts) -> {seq, $[, $], [$,], items(lists:reverse(Es), Opts)};
list_items(Tail, Es, Opts) -> {seq, $[, $], [{$,, ' |'}], items(lists:reverse(Es, [Tail]), Opts)}.

tuple([], _F, _Opts) -> leaf("{}");
tuple(Es, F, Opts) -> {seq, ${, $}, [$,], [F(E, Opts) || E <- Es]}.

arguments(As, Opts) -> {seq, $(, $), [$,], items(As, Opts)}.

separated(Es, Sep, F, Opts) -> {seq, [], [], Sep, [F(E, Opts) || E <- Es]}.

paren(P, Prec, Item) when P < Prec -> [$(, Item, $)];
paren(_P, _Prec, Item) -> Item.

leaf(Chars) -> {leaf, string:length(Chars), Chars}.

anno0() -> erl_anno:new(0).

%% Second pass: layout. Each item lays out as {Chars, Size}, Size being its width on one line or {line, Width} with
%% the width of its last line once it spans lines. A negative indentation lays everything out on one line.

layout(Item, I, PP) ->
    {Chars, _Size} = lay(Item, I, PP),
    [Chars].

lay([], _I, _PP) ->
    {[], 0};
lay(C, _I, _PP) when is_integer(C) ->
    {C, 1};
lay({leaf, Size, Chars}, _I, _PP) ->
    {Chars, Size};
lay([Item | Items], I, PP) ->
    run_on(Items, lay(Item, I, PP), I, PP);
lay({list, Parts}, I, PP) ->
    lay({seq, [], [], [[]], Parts}, I, PP);
lay({first, Before, Part}, I, PP) ->
    lay({seq, Before, [], [[]], [Part]}, I, PP);
lay({seq, Before, After, Sep, Parts}, I0, PP) ->
    lay_seq(Before, After, Sep, Parts, I0, PP);
lay({force_nl, _Comment, Item}, I, PP) when I < 0 ->
    lay(Item, I, PP);
lay({force_nl, Comment, Item}, I, PP) ->
    lay({prefer_nl, [], [Comment, Item]}, I, PP);
lay({prefer_nl, Sep, Parts}, I, PP) when I < 0 ->
    lay({seq, [], [], Sep, Parts}, I, PP);
lay({prefer_nl, Sep, Parts}, I, PP) ->
    Laid = lay_parts(Parts, Sep, I, [], PP),
    case sizes(Laid) of
        [] -> {[], 0};
        Sizes -> {stack_lines(Laid, I, PP), at_line(lists:last(Sizes), I)}
    end;
lay({value, V}, I, PP) ->
    lay(flat_leaf((PP#pp.value)(V)), I, PP);
lay({atom, A}, I, PP) ->
    lay(flat_leaf((PP#pp.value)(A)), I, PP);
lay({singleton_atom_type, A}, I, PP) ->
    lay(flat_leaf((PP#pp.singleton)(A)), I, PP);
lay({char, C}, I, PP) ->
    lay(flat_leaf((PP#pp.char)(C)), I, PP);
lay({string, S}, I, PP) ->
    lay(string_item(S, I, PP), I, PP);
lay({reserved, Word}, I, PP) ->
    lay(Word, I, PP);
lay({hook, E, Prec, Hook, Options}, I, _PP) ->
    hooked(Hook(E, I, Prec, Options), I);
lay({ehook, E, Prec, {M, F, Extra} = MFE}, I, _PP) ->
    hooked(apply(M, F, [E, I, Prec, MFE | Extra]), I);
lay(Word, _I, _PP) when is_atom(Word) ->
    word(Word).

%% A sequence on one line when Before, its parts and their separators fit; else the parts stepped and separated by
%% newlines where needed.
lay_seq(Before, After, Sep, Parts, I0, PP) ->
    LaidBefore = lay(Before, I0, PP),
    I = advanced(LaidBefore, I0),
    Laid = lay_parts(Parts, Sep, I, After, PP),
    {Chars, Sizes} = nonzero_unzip(lists:append(Laid)),
    {BeforeChars, BeforeSizes} = nonzero_unzip([LaidBefore]),
    SepCount =
        case is_list(Sep) andalso Sep =/= [] of
            true -> max(0, length(Chars) - 1);
            false -> 0
        end,
    case fits(I0, BeforeSizes ++ Sizes, SepCount, PP) of
        {yes, Size} when SepCount > 0 ->
            {BeforeChars ++ separate(Chars, $\s), Size};
        {yes, Size} ->
            {BeforeChars ++ Chars, Size};
        no ->
            {LaidChars, LaidSize} = break_lines(stepped(Laid, I, PP), Parts, I, SepCount, PP),
            {[BeforeChars, LaidChars], at_line(LaidSize, I0)}
    end.

%% Each part laid out as [Head, Tail]: Tail is the stepped part on its own lines, or {[], 0}. The last part is
%% followed by After, the one before it by the last separator, the others by the separator.
lay_parts([], _Sep, I, After, PP) ->
    [[lay(After, I, PP), {[], 0}]];
lay_parts(Parts, Sep0, I, After, PP) ->
    {Sep, LastSep} = separators(Sep0),
    follow(Parts, fun(Part, S) -> lay_part(Part, S, I, PP) end, Sep, LastSep, After).

separators([{S, Last}]) -> {[S], [Last]};
separators({_, _} = Seps) -> Seps;
separators(S) -> {S, S}.

follow([Part], Lay, _Sep, _LastSep, After) ->
    [Lay(Part, After)];
follow([P1, P2], Lay, _Sep, LastSep, After) ->
    [Lay(P1, LastSep), Lay(P2, After)];
follow([Part | Parts], Lay, Sep, LastSep, After) ->
    [Lay(Part, Sep) | follow(Parts, Lay, Sep, LastSep, After)].

lay_part({step, Head, Tail}, S, I, PP) ->
    [lay(Head, I, PP), lay([Tail, S], increased(I, PP#pp.indent), PP)];
lay_part({cstep, Head, Tail}, S, I, PP) ->
    {_, HeadSize} = LaidHead = lay(Head, I, PP),
    case is_integer(HeadSize) andalso HeadSize < PP#pp.indent of
        true -> [run_on([leaf("\s"), Tail, S], LaidHead, I, PP), {[], 0}];
        false -> [LaidHead, lay([Tail, S], increased(I, PP#pp.indent), PP)]
    end;
lay_part({reserved, Word}, S, I, PP) ->
    [lay([Word, S], I, PP), {[], 0}];
lay_part(Item, S, I, PP) ->
    [lay([Item, S], I, PP), {[], 0}].

%% Items one after the other, each starting where the previous one ended.
run_on(Items, First, I0, PP) ->
    {Laid, _} = lists:mapfoldl(
        fun(Item, Column) ->
            One = lay(Item, Column, PP),
            {One, advanced(One, Column)}
        end,
        advanced(First, I0),
        Items
    ),
    {Chars, Sizes} = nonzero_unzip([First | Laid]),
    {Chars, line_size(Sizes)}.

sizes(Laid) ->
    {_, Sizes} = nonzero_unzip(lists:append(Laid)),
    Sizes.

nonzero_unzip(Laid) -> lists:unzip([CS || {_, Size} = CS <- Laid, Size =/= 0]).

%% The parts of a sequence that did not fit: a part that is a leaf or atom joins the previous line while the line has
%% room, any other part starts a new line.
break_lines([{Chars, Size}], [], _I, _SepCount, _PP) ->
    {Chars, Size};
break_lines(Laid, Parts, I, SepCount, PP) when I >= 0 ->
    Newline = newline(I),
    [{Chars1, Size1} | RestLaid] = Laid,
    [Part1 | RestParts] = Parts,
    Column =
        case is_atomic(Part1) of
            true -> I + Size1;
            false -> PP#pp.width + 1
        end,
    join_lines(RestLaid, RestParts, I, Column, Newline, SepCount, Size1, [Chars1], PP).

join_lines([{Chars, Size} | Laid], [Part | Parts], I0, Column, Newline, SepCount, Width, Acc, PP) ->
    case is_atomic(Part) andalso is_integer(Size) of
        true ->
            Next = Column + Size + 1,
            case Next =< PP#pp.width of
                true ->
                    Acc1 =
                        case SepCount > 0 of
                            true -> [Chars, $\s | Acc];
                            false -> [Chars | Acc]
                        end,
                    join_lines(
                        Laid, Parts, I0, Next, Newline, SepCount, Width + Size + 1, Acc1, PP
                    );
                false ->
                    join_lines(
                        Laid,
                        Parts,
                        I0,
                        I0 + Size,
                        Newline,
                        SepCount,
                        Size + 1,
                        [Chars, Newline | Acc],
                        PP
                    )
            end;
        false ->
            join_lines(
                Laid, Parts, I0, PP#pp.width + 1, Newline, SepCount, 0, [Chars, Newline | Acc], PP
            )
    end;
join_lines(_Laid, _Parts, _I0, _Column, _Newline, _SepCount, Width, Acc, _PP) ->
    {lists:reverse(Acc), Width}.

is_atomic({atom, _}) -> true;
is_atomic({singleton_atom_type, _}) -> true;
is_atomic(Word) when is_atom(Word) -> true;
is_atomic({leaf, _, _}) -> true;
is_atomic(_) -> false.

%% The parts of a prefer_nl group, one per line.
stack_lines(Laid, I, PP) when I >= 0 ->
    {Chars, _} = nonzero_unzip(stepped(Laid, I, PP)),
    separate(Chars, newline(I)).

%% Each laid part as one {Chars, Size}: a stepped tail goes on the next line, one indentation in.
stepped(Laid, I, PP) ->
    lists:map(
        fun
            ([{_, 0}, {_, 0}]) ->
                {[], 0};
            ([{Head, HeadSize}, {_, 0}]) ->
                {Head, HeadSize};
            ([{Head, HeadSize}, {Tail, TailSize}]) when TailSize > 0 ->
                {separate([Head, Tail], newline(I + PP#pp.indent)), line_size([HeadSize, TailSize])}
        end,
        Laid
    ).

separate([First | Rest], Sep) -> [First | [[Sep, Chars] || Chars <- Rest]].

newline(0) -> $\n;
newline(I) when I > 0 -> [$\n | lists:duplicate(I, $\s)].

fits(I0, Sizes, SepCount, PP) ->
    try
        Size = lists:sum(Sizes) + SepCount,
        true = increased(I0, Size) =< PP#pp.width,
        {yes, Size}
    catch
        _:_ -> no
    end.

%% The width of items laid one after the other: the last line's width once one of them spans lines.
line_size(Sizes) -> line_size(Sizes, 0, false).

line_size([], Size, false) -> Size;
line_size([], Size, true) -> {line, Size};
line_size([{line, Width} | Sizes], _, _) -> line_size(Sizes, Width, true);
line_size([Size | Sizes], Sum, Line) -> line_size(Sizes, Sum + Size, Line).

at_line({line, _} = Size, _I) -> Size;
at_line(Size, I) when I >= 0 -> {line, Size + I}.

%% The column after laid characters.
advanced({_Chars, {line, Width}}, _I) -> Width;
advanced({_Chars, Size}, I) -> increased(I, Size).

increased(I, _By) when I < 0 -> I;
increased(I, By) -> I + By.

hooked(Chars, I) when I < 0 ->
    {Chars, string:length(Chars)};
hooked(Chars, I) ->
    Column = io_lib:indentation(Chars, I),
    case has_newline(Chars) of
        true -> {Chars, {line, Column}};
        false -> {Chars, Column}
    end.

has_newline([$\n | _]) -> true;
has_newline([C | Cs]) when is_integer(C) -> has_newline(Cs);
has_newline([C | Cs]) -> has_newline(C) orelse has_newline(Cs);
has_newline([]) -> false.

flat_leaf(Chars) ->
    Flat = lists:flatten(Chars),
    {leaf, string:length(Flat), Flat}.

%% A string cut in pieces that fit what is left of the line, laid out as a list.
string_item(S, I, PP) when I < 0; S =:= [] ->
    flat_leaf((PP#pp.string)(S));
string_item(S, I, PP) ->
    Width = max(PP#pp.width - I, ?MIN_PIECE),
    {list, string_pieces(S, Width, Width, PP)}.

string_pieces([], _N, _Width, _PP) ->
    [];
string_pieces(S, N, Width, PP) ->
    Piece = string:slice(S, 0, N),
    Written = (PP#pp.string)(Piece),
    case string:length(Written) > Width andalso N > ?MIN_PIECE of
        true ->
            string_pieces(S, N - 1, Width, PP);
        false ->
            [
                flat_leaf(Written)
                | string_pieces(string:slice(S, string:length(Piece)), Width, Width, PP)
            ]
    end.

%% Keywords and operators as leaves.
word(Word) ->
    Text = atom_to_list(Word),
    {Text, string:length(Text)}.
