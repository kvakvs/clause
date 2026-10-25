%% Clause's erl_pp (plan 21 step LIB34C, docs/builtins.md#erl-pp): the abstract format printed as Erlang source,
%% written from OTP's documentation and observed output. The printing runs in C++
%% (runtime/src/builtins/front_end/pp*.cpp); this module calls the hook for each form the printer does not know
%% and prints again with the hook's text, and renames variables for legalize_vars/1.
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

form(Form) -> form(Form, none).

form(Form, Options) -> print({form, Form, 0, 0, Options}).

attribute(Attribute) -> attribute(Attribute, none).

attribute(Attribute, Options) -> print({attribute, Attribute, 0, 0, Options}).

function(Function) -> function(Function, none).

function(Function, Options) -> print({function, Function, 0, 0, Options}).

guard(Guard) -> guard(Guard, none).

guard(Guard, Options) -> print({guard, Guard, 0, 0, Options}).

exprs(Es) -> exprs(Es, 0, none).

exprs(Es, Options) -> exprs(Es, 0, Options).

exprs(Es, I, Options) -> print({exprs, Es, I, 0, Options}).

expr(E) -> print({expr, E, 0, 0, none}).

expr(E, Options) -> print({expr, E, 0, 0, Options}).

expr(E, I, Options) -> print({expr, E, I, 0, Options}).

expr(E, I, Precedence, Options) -> print({expr, E, I, Precedence, Options}).

%% The characters of a request, the hook answering each form the printer does not know, in order.
print(Request) -> print(Request, []).

print({_, _, _, _, Options} = Request, Answers) ->
    case clause_erl_pp:print(Request, Answers) of
        {hook, E, I, Precedence} -> print(Request, Answers ++ [hook(E, I, Precedence, Options)]);
        Chars -> Chars
    end.

%% The hook of the options: an {M, F, Extra} hook gets itself after the precedence, a fun the options as given.
hook(E, I, Precedence, Options) when is_list(Options) ->
    call_hook(proplists:get_value(hook, Options), E, I, Precedence, Options);
hook(E, I, Precedence, Hook) ->
    call_hook(Hook, E, I, Precedence, Hook).

call_hook({M, F, Extra} = MFE, E, I, Precedence, _Options) when M =/= 'fun' ->
    apply(M, F, [E, I, Precedence, MFE | Extra]);
call_hook(Hook, E, I, Precedence, Options) ->
    Hook(E, I, Precedence, Options).

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
