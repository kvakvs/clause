%% Clause's erl_eval (plan 21 step LIB32, docs/libraries/library.md): an interpreter of the abstract format, written from OTP's
%% documentation and observed behavior. Bindings are orddicts or maps and come back as given. Calls go through the
%% local and non-local function handlers as OTP documents; errors are raised through the non-local handler as calls of
%% erlang:raise/3. Funs made here are closures of their arity over {Anno, Bindings, LocalHandler, NonLocalHandler,
%% UsedVars, Clauses} (and the name of a named fun), so fun_data/1 and erlang:fun_info/2 see what OTP shows. Pattern
%% matching (binary patterns and generators included) and constant folding run in C++ as clause_erl_eval's and
%% erl_eval's builtins, bit syntax segments are built by them; exprs/2 and expr/2 check a command with erl_lint
%% first.
-module(erl_eval).
-export([
    add_binding/3,
    binding/2,
    bindings/1,
    check_command/2,
    del_binding/2,
    eval_str/1,
    expr/2,
    expr/3,
    expr/4,
    expr/5,
    expr_list/2,
    expr_list/3,
    expr_list/4,
    exprs/2,
    exprs/3,
    exprs/4,
    extended_parse_exprs/1,
    extended_parse_term/1,
    fun_data/1,
    is_constant_expr/1,
    match_clause/4,
    new_bindings/0,
    new_bindings/1,
    partial_eval/1
]).

-include("erl_eval.types.hrl").

%% The function handlers of one evaluation and the used variables of the funs inside the code evaluated
%% (erl_lint:used_vars/2's tree), passed down instead of OTP's separate arguments.
-record(ev, {lf = none, ef = none, fuvs = #{}}).

%% Evaluation API

exprs(Exprs, Bs) ->
    checked(Exprs, Bs),
    seq(Exprs, Bs, #ev{}, none).

exprs(Exprs, Bs, Lf) ->
    seq(Exprs, Bs, #ev{lf = Lf}, none).

exprs(Exprs, Bs, Lf, Ef) ->
    seq(Exprs, Bs, #ev{lf = Lf, ef = Ef}, none).

expr(E, Bs) ->
    checked([E], Bs),
    eval(E, Bs, #ev{}, none).

expr(E, Bs, Lf) ->
    eval(E, Bs, #ev{lf = Lf}, none).

expr(E, Bs, Lf, Ef) ->
    eval(E, Bs, #ev{lf = Lf, ef = Ef}, none).

expr(E, Bs, Lf, Ef, RBs) ->
    eval(E, Bs, #ev{lf = Lf, ef = Ef}, RBs).

expr_list(Es, Bs) ->
    values(Es, Bs, #ev{}).

expr_list(Es, Bs, Lf) ->
    values(Es, Bs, #ev{lf = Lf}).

expr_list(Es, Bs, Lf, Ef) ->
    values(Es, Bs, #ev{lf = Lf, ef = Ef}).

%% check_command(Exprs, Bindings): ok or {error, {Location, erl_lint, Error}}, the first error of the command.
check_command(Es, Bs) ->
    Opts = [{features, erl_features:enabled()}],
    case erl_lint:exprs_opt(Es, bindings(Bs), Opts) of
        {ok, _Ws} -> ok;
        {error, [{_File, [Error | _]}], _Ws} -> {error, Error}
    end.

%% Only exprs/2 and expr/2 check: with a handler, constructs erl_lint rejects may be meant for the handler.
checked(Es, Bs) ->
    case check_command(Es, Bs) of
        ok -> ok;
        {error, {_Location, _Module, Error}} -> erlang:raise(error, Error, stacktrace())
    end.

%% fun_data(Term): {fun_data, Bindings, Clauses} or {named_fun_data, Bindings, Name, Clauses} for a fun made here.
fun_data(F) when is_function(F) ->
    case fun_env(F) of
        {ok, {_Anno, Bs, _Lf, _Ef, _Fuvs, Cs}} -> {fun_data, Bs, Cs};
        {ok, {_Anno, Bs, _Lf, _Ef, _Fuvs, Cs, Name}} -> {named_fun_data, Bs, Name, Cs};
        none -> false
    end;
fun_data(_) ->
    false.

%% The information a fun made here keeps; none for any other fun.
fun_env(F) ->
    case erlang:fun_info(F, module) of
        {module, ?MODULE} ->
            case erlang:fun_info(F, env) of
                {env, [{_, _, _, _, _, _} = Info]} -> {ok, Info};
                {env, [{_, _, _, _, _, _, _} = Info]} -> {ok, Info};
                _ -> none
            end;
        _ ->
            none
    end.

%% match_clause(Clauses, Values, Bindings, LocalHandler): {Body, Bindings} of the first clause matching, or nomatch.
match_clause(Cs, Vs, Bs, Lf) ->
    select_clause(Cs, Vs, Bs, #ev{lf = Lf}).

%% Bindings

new_bindings() -> orddict:new().

new_bindings(Bs) when is_map(Bs) -> #{};
new_bindings(Bs) when is_list(Bs) -> orddict:new().

bindings(Bs) when is_map(Bs) -> maps:to_list(Bs);
bindings(Bs) when is_list(Bs) -> orddict:to_list(Bs).

binding(Name, Bs) when is_map(Bs) ->
    case Bs of
        #{Name := Value} -> {value, Value};
        #{} -> unbound
    end;
binding(Name, Bs) when is_list(Bs) ->
    case orddict:find(Name, Bs) of
        {ok, Value} -> {value, Value};
        error -> unbound
    end.

add_binding(Name, Value, Bs) when is_map(Bs) -> Bs#{Name => Value};
add_binding(Name, Value, Bs) when is_list(Bs) -> orddict:store(Name, Value, Bs).

del_binding(Name, Bs) when is_map(Bs) -> maps:remove(Name, Bs);
del_binding(Name, Bs) when is_list(Bs) -> orddict:erase(Name, Bs).

%% New bindings shadowing old ones.
add_bindings(New, Old) when is_map(New), is_map(Old) ->
    maps:merge(Old, New);
add_bindings(New, Old) ->
    lists:foldl(
        fun({Name, Value}, Acc) -> orddict:store(Name, Value, Acc) end, Old, orddict:to_list(New)
    ).

%% Bindings of expressions evaluated side by side: a variable both bind must have the same value, else badmatch (of
%% the first's value for maps, the second's for orddicts, as OTP raises).
merge_bindings(Bs1, Bs2, Anno, Ev) when is_map(Bs1), is_map(Bs2) ->
    maps:merge_with(
        fun
            (_, V, V) -> V;
            (_, _, V) -> fail({badmatch, V}, Anno, Bs1, Ev, none)
        end,
        Bs2,
        Bs1
    );
merge_bindings(Bs1, Bs2, Anno, Ev) ->
    lists:foldl(
        fun({Name, Value}, Acc) ->
            case orddict:find(Name, Acc) of
                {ok, Value} -> Acc;
                {ok, Other} -> fail({badmatch, Other}, Anno, Bs1, Ev, none);
                error -> orddict:store(Name, Value, Acc)
            end
        end,
        Bs2,
        orddict:to_list(Bs1)
    ).

filter_bindings(Keep, Bs) when is_map(Bs) -> maps:filter(Keep, Bs);
filter_bindings(Keep, Bs) when is_list(Bs) -> orddict:filter(Keep, Bs).

%% Results: RBs is none (return {value, V, Bs}), value (return V, inside a fun called from compiled code) or the
%% bindings to return when a fun's body ends.

ret(V, _Bs, value) -> V;
ret(V, Bs, none) -> {value, V, Bs};
ret(V, _Bs, RBs) when is_list(RBs); is_map(RBs) -> {value, V, RBs}.

%% The stack of the evaluation, for the errors it raises.
stacktrace() ->
    try
        throw(stack)
    catch
        throw:stack:Stack -> Stack
    end.

%% An error of the evaluated code, raised through the non-local handler as erlang:raise(error, Reason, Stack).
fail(Reason, Anno, Bs, Ev, RBs) ->
    fail(Reason, stacktrace(), Anno, Bs, Ev, RBs).

fail(Reason, Stack, Anno, Bs, Ev, RBs) ->
    apply_mf(erlang, raise, [error, Reason, Stack], Anno, Bs, Ev, RBs).

%% Sequences and lists of expressions

seq([E], Bs, Ev, RBs) ->
    eval(E, Bs, Ev, RBs);
seq([E | Es], Bs, Ev, RBs) ->
    {value, _, Bs1} = eval(E, Bs, Ev, none),
    seq(Es, Bs1, Ev, RBs).

%% Expressions evaluated side by side over the same bindings, values pushed on Vs, bindings merged.
side_by_side([E | Es], Vs, BsOrig, Bs0, Ev) ->
    {value, V, Bs1} = eval(E, BsOrig, Ev, none),
    side_by_side(Es, [V | Vs], BsOrig, merge_bindings(Bs1, Bs0, element(2, E), Ev), Ev);
side_by_side([], Vs, _BsOrig, Bs, _Ev) ->
    {Vs, Bs}.

values(Es, Bs, Ev) ->
    {Vs, Bs1} = side_by_side(Es, [], Bs, Bs, Ev),
    {lists:reverse(Vs), Bs1}.

%% The evaluator

eval({var, Anno, V}, Bs, Ev, RBs) ->
    case binding(V, Bs) of
        {value, Value} -> ret(Value, Bs, RBs);
        unbound -> fail({unbound, V}, Anno, Bs, Ev, RBs)
    end;
eval({Leaf, _, Value}, Bs, _Ev, RBs) when
    Leaf =:= char; Leaf =:= integer; Leaf =:= float; Leaf =:= atom; Leaf =:= string
->
    ret(Value, Bs, RBs);
eval({nil, _}, Bs, _Ev, RBs) ->
    ret([], Bs, RBs);
eval({cons, Anno, H0, T0}, Bs, Ev, RBs) ->
    {value, H, Bs1} = eval(H0, Bs, Ev, none),
    {value, T, Bs2} = eval(T0, Bs, Ev, none),
    ret([H | T], merge_bindings(Bs1, Bs2, Anno, Ev), RBs);
eval({lc, _, E, Qs}, Bs, Ev, RBs) ->
    ret(lists:reverse(comprehension(lc, E, Qs, Bs, Ev, [])), Bs, RBs);
eval({bc, _, E, Qs}, Bs, Ev, RBs) ->
    ret(comprehension(bc, E, Qs, Bs, Ev, <<>>), Bs, RBs);
eval({mc, _, E, Qs}, Bs, Ev, RBs) ->
    ret(maps:from_list(lists:reverse(comprehension(mc, E, Qs, Bs, Ev, []))), Bs, RBs);
eval({tuple, _, Es}, Bs, Ev, RBs) ->
    {Vs, Bs1} = values(Es, Bs, Ev),
    ret(list_to_tuple(Vs), Bs1, RBs);
eval({record_field, _, {atom, _, Name}, E}, Bs, Ev, RBs) ->
    {value, V, Bs1} = eval(E, Bs, Ev, none),
    ret({Name, V}, Bs1, RBs);
eval({record_field, Anno, Name, _}, Bs, Ev, RBs) ->
    fail({undef_record, Name}, Anno, Bs, Ev, RBs);
eval({record_field, Anno, Src, Name, Field}, Bs, Ev, RBs) ->
    record_access(Anno, Src, Name, Field, Bs, Ev, RBs);
eval({record_index, Anno, Name, _}, Bs, Ev, RBs) ->
    fail({undef_record, Name}, Anno, Bs, Ev, RBs);
eval({record, Anno, Name, Fields}, Bs, Ev, RBs) ->
    record_create(Anno, Name, Fields, Bs, Ev, RBs);
eval({record, Anno, Src, Name, Fields}, Bs, Ev, RBs) ->
    record_update(Anno, Src, Name, Fields, Bs, Ev, RBs);
eval({map, Anno, Src, Fields}, Bs, Ev, RBs) ->
    {value, Map0, Bs1} = eval(Src, Bs, Ev, none),
    {Assocs, Bs2} = map_fields(Fields, Bs, Ev),
    _ = maps:put(k, v, Map0),
    Map = lists:foldl(fun map_put/2, Map0, Assocs),
    ret(Map, merge_bindings(Bs2, Bs1, Anno, Ev), RBs);
eval({map, _, Fields}, Bs, Ev, RBs) ->
    {Assocs, Bs1} = map_fields(Fields, Bs, Ev),
    ret(lists:foldl(fun({map_assoc, K, V}, M) -> M#{K => V} end, #{}, Assocs), Bs1, RBs);
eval({block, _, Es}, Bs, Ev, RBs) ->
    seq(Es, Bs, Ev, RBs);
eval({'if', Anno, Cs}, Bs, Ev, RBs) ->
    if_clauses(Cs, Anno, Bs, Ev, RBs);
eval({'case', Anno, E, Cs}, Bs, Ev, RBs) ->
    {value, V, Bs1} = eval(E, Bs, Ev, none),
    case select_clause(Cs, [V], Bs1, Ev) of
        {Body, Bs2} -> seq(Body, Bs2, Ev, RBs);
        nomatch -> fail({case_clause, V}, Anno, Bs1, Ev, RBs)
    end;
eval({'try', Anno, B, Cases, Catches, After}, Bs, Ev, RBs) ->
    try_expr(Anno, B, Cases, Catches, After, Bs, Ev, RBs);
eval({'receive', _, Cs}, Bs, Ev, RBs) ->
    receive_clauses(infinity, Cs, unused, Bs, Ev, RBs);
eval({'receive', _, Cs, Timeout0, After}, Bs, Ev, RBs) ->
    {value, Timeout, Bs1} = eval(Timeout0, Bs, Ev, none),
    receive_clauses(Timeout, Cs, {After, Bs1}, Bs, Ev, RBs);
eval({'fun', _, {function, M0, F0, A0}}, Bs, Ev, RBs) ->
    {[M, F, A], Bs1} = values([M0, F0, A0], Bs, Ev),
    ret(erlang:make_fun(M, F, A), Bs1, RBs);
eval({'fun', Anno, {function, Name, Arity}}, Bs, Ev, RBs) ->
    local_fun(Anno, Name, Arity, Bs, Ev, RBs);
eval({'fun', Anno, {clauses, Cs}} = Fun, Bs, Ev, RBs) ->
    make_fun(Anno, Fun, Cs, none, Bs, Ev, RBs);
eval({named_fun, Anno, Name, Cs} = Fun, Bs, Ev, RBs) ->
    make_fun(Anno, Fun, Cs, Name, Bs, Ev, RBs);
eval({call, _, {remote, _, {atom, _, qlc}, {atom, _, q}}, [{lc, _, _E, _Qs} = LC | As0]}, Bs, Ev, RBs) when
    length(As0) =< 1
->
    qlc_query(LC, As0, Bs, Ev, RBs);
eval(
    {call, A1, {remote, A2, {record_field, _, {atom, _, ''}, {atom, _, qlc} = Mod}, {atom, _, q} = Func}, As},
    Bs,
    Ev,
    RBs
) when
    is_tuple(hd(As)), element(1, hd(As)) =:= lc, length(As) =< 2
->
    %% The old ''.qlc:q spelling.
    eval({call, A1, {remote, A2, Mod, Func}, As}, Bs, Ev, RBs);
eval({call, Anno, {remote, _, Mod, Func}, As0}, Bs, Ev, RBs) ->
    {value, M, Bs1} = eval(Mod, Bs, Ev, none),
    {value, F, Bs2} = eval(Func, Bs, Ev, none),
    {As, Bs3} = values(As0, merge_bindings(Bs1, Bs2, Anno, Ev), Ev),
    case is_atom(M) andalso erl_internal:bif(M, F, length(As)) of
        true -> bif(F, As, Anno, Bs3, Ev, RBs);
        false -> apply_mf(M, F, As, Anno, Bs3, Ev, RBs)
    end;
eval({call, Anno, {atom, _, Func}, As0}, Bs, Ev, RBs) ->
    case erl_internal:bif(Func, length(As0)) of
        true ->
            {As, Bs1} = values(As0, Bs, Ev#ev{fuvs = #{}}),
            bif(Func, As, Anno, Bs1, Ev, RBs);
        false ->
            local_call(Func, As0, Anno, Bs, Ev, RBs)
    end;
eval({call, Anno, Func0, As0}, Bs, Ev, RBs) ->
    {value, Func, Bs1} = eval(Func0, Bs, Ev, none),
    {As, Bs2} = values(As0, Bs1, Ev),
    case Func of
        {M, F} when is_atom(M), is_atom(F) -> fail({badfun, Func}, Anno, Bs, Ev, RBs);
        _ -> apply_fun(Func, As, Anno, Bs2, Ev, RBs)
    end;
eval({'catch', _, E}, Bs, Ev, RBs) ->
    try eval(E, Bs, Ev, none) of
        {value, V, Bs1} -> ret(V, Bs1, RBs)
    catch
        throw:Term -> ret(Term, Bs, RBs);
        exit:Reason -> ret({'EXIT', Reason}, Bs, RBs);
        error:Reason:Stack -> ret({'EXIT', {Reason, Stack}}, Bs, RBs)
    end;
eval({match, Anno, P, E}, Bs, Ev, RBs) ->
    {value, V, Bs1} = eval(E, Bs, Ev, none),
    case match(P, V, Anno, Bs1, Bs1, Ev) of
        {match, Bs2} -> ret(V, Bs2, RBs);
        nomatch -> fail({badmatch, V}, Anno, Bs, Ev, RBs)
    end;
eval({'maybe', _, Es}, Bs, Ev, RBs) ->
    {_, V} = maybe_exprs(Es, Bs, Ev),
    ret(V, Bs, RBs);
eval({'maybe', Anno, Es, {'else', _, Cs}}, Bs, Ev, RBs) ->
    case maybe_exprs(Es, Bs, Ev) of
        {success, V} ->
            ret(V, Bs, RBs);
        {failure, V} ->
            case select_clause(Cs, [V], Bs, Ev) of
                {Body, Bs1} -> seq(Body, Bs1, Ev, RBs);
                nomatch -> fail({else_clause, V}, Anno, Bs, Ev, RBs)
            end
    end;
eval({op, Anno, Op, A0}, Bs, Ev, RBs) ->
    {value, A, Bs1} = eval(A0, Bs, Ev, none),
    apply_mf(erlang, Op, [A], Anno, Bs1, Ev, RBs);
eval({op, Anno, 'andalso', L0, R0}, Bs, Ev, RBs) ->
    {value, L, Bs1} = eval(L0, Bs, Ev, none),
    V =
        case L of
            true -> right_operand(R0, Bs1, Ev);
            false -> false;
            _ -> fail({badarg, L}, Anno, Bs, Ev, RBs)
        end,
    ret(V, Bs1, RBs);
eval({op, Anno, 'orelse', L0, R0}, Bs, Ev, RBs) ->
    {value, L, Bs1} = eval(L0, Bs, Ev, none),
    V =
        case L of
            true -> true;
            false -> right_operand(R0, Bs1, Ev);
            _ -> fail({badarg, L}, Anno, Bs, Ev, RBs)
        end,
    ret(V, Bs1, RBs);
eval({op, Anno, Op, L0, R0}, Bs, Ev, RBs) ->
    {value, L, Bs1} = eval(L0, Bs, Ev, none),
    {value, R, Bs2} = eval(R0, Bs, Ev, none),
    apply_mf(erlang, Op, [L, R], Anno, merge_bindings(Bs1, Bs2, Anno, Ev), Ev, RBs);
eval({bin, _, Fields}, Bs, Ev, RBs) ->
    Eval = fun(E, B) -> eval(E, B, Ev, none) end,
    Error = fun(A, Reason, Stack) -> fail(Reason, Stack, A, Bs, Ev, RBs) end,
    {value, V, Bs1} = bits_expr(Fields, Bs, Eval, Error),
    ret(V, Bs1, RBs);
eval({remote, Anno, _, _}, Bs, Ev, RBs) ->
    fail({badexpr, ':'}, Anno, Bs, Ev, RBs).

right_operand(R0, Bs, Ev) ->
    {value, R, _} = eval(R0, Bs, Ev, none),
    R.

map_put({map_assoc, K, V}, M) -> maps:put(K, V, M);
map_put({map_exact, K, V}, M) -> maps:update(K, V, M).

%% The fields of a map expression in order, each over the bindings the previous one left.
map_fields(Fields, Bs, Ev) ->
    {Assocs, Bs1} = lists:foldl(
        fun(Field, {Acc, B0}) ->
            {Tag, K0, V0} = map_field(Field),
            {value, K, B1} = eval(K0, B0, Ev, none),
            {value, V, B2} = eval(V0, B1, Ev, none),
            {[{Tag, K, V} | Acc], B2}
        end,
        {[], Bs},
        Fields
    ),
    {lists:reverse(Assocs), Bs1}.

map_field({map_field_assoc, _, K, V}) -> {map_assoc, K, V};
map_field({map_field_exact, _, K, V}) -> {map_exact, K, V}.

maybe_exprs([{maybe_match, Anno, P, E0} | Es], Bs, Ev) ->
    {value, V, Bs1} = eval(E0, Bs, Ev, none),
    case match(P, V, Anno, Bs1, Bs1, Ev) of
        {match, _} when Es =:= [] -> {success, V};
        {match, Bs2} -> maybe_exprs(Es, Bs2, Ev);
        nomatch -> {failure, V}
    end;
maybe_exprs([E], Bs, Ev) ->
    {value, V, _} = eval(E, Bs, Ev, none),
    {success, V};
maybe_exprs([E | Es], Bs, Ev) ->
    {value, _, Bs1} = eval(E, Bs, Ev, none),
    maybe_exprs(Es, Bs1, Ev).

%% Native records

record_create(Anno, {shell_default = M, N}, Fields, Bs, Ev, RBs) ->
    case
        [
            {Err, {{M, N}, F}}
         || {record_field, _, {atom, _, F}, {nil, Err}} <- Fields,
            Err =:= novalue orelse Err =:= badfield
        ]
    of
        [] ->
            {Vs, Bs1} = values(Fields, Bs, Ev),
            ret(records:create(M, N, Vs, #{is_exported => false}), Bs1, RBs);
        [Error | _] ->
            fail(Error, Anno, Bs, Ev, RBs)
    end;
record_create(Anno, {M, N}, Fields, Bs, Ev, RBs) ->
    try records:get_definition(M, N) of
        {Options, Definition} ->
            {Vs, Bs1} = values(Fields, Bs, Ev),
            case record_init(Definition, Vs, []) of
                {novalue, K} -> fail({novalue, {{M, N}, K}}, Anno, Bs, Ev, RBs);
                {badfield, F} -> fail({badfield, {{M, N}, F}}, Anno, Bs, Ev, RBs);
                Init -> ret(records:create(M, N, Init, Options), Bs1, RBs)
            end
    catch
        _:_ -> fail({undef_record, {M, N}}, Anno, Bs, Ev, RBs)
    end;
record_create(Anno, Name, _Fields, Bs, Ev, RBs) ->
    fail({undef_record, Name}, Anno, Bs, Ev, RBs).

%% The fields of a construction in definition order, defaults filled in; a missing value or unknown field fails.
record_init([{K, Default} | Definition], Vs, Acc) ->
    record_init(Definition, Vs, [field_value(K, Vs, {K, Default}) | Acc]);
record_init([K | Definition], Vs, Acc) ->
    case lists:keyfind(K, 1, Vs) of
        false -> {novalue, K};
        Given -> record_init(Definition, Vs, [Given | Acc])
    end;
record_init([], Vs, Acc) ->
    case [F || {F, _} <- Vs, not lists:keymember(F, 1, Acc)] of
        [] -> lists:reverse(Acc);
        [F | _] -> {badfield, F}
    end.

field_value(K, Vs, Default) ->
    case lists:keyfind(K, 1, Vs) of
        false -> Default;
        Given -> Given
    end.

%% X#m:r{...}, X#_{...} and X#r{...} of native records.
record_update(Anno, {var, _, Src0}, Name, Fields, Bs, Ev, RBs) ->
    case binding(Src0, Bs) of
        {value, Src} ->
            case record_name(Src, Name) of
                {ok, M, N} ->
                    {Vs, Bs1} = values(Fields, Bs, Ev),
                    ret(records:update(Src, M, N, maps:from_list(Vs)), Bs1, RBs);
                error ->
                    fail({badarg, Src}, Anno, Bs, Ev, RBs)
            end;
        unbound ->
            fail({unbound, Src0}, Anno, Bs, Ev, RBs)
    end;
record_update(Anno, Src, Name, _Fields, Bs, Ev, RBs) ->
    fail({badarg, Src}, Anno, Bs, Ev, RBs),
    fail({badarg, Name}, Anno, Bs, Ev, RBs).

%% The module and name an update or access of Name gives a record; error when Src is no such record.
record_name(Src, {M, N}) ->
    case is_record(Src, M, N) of
        true -> {ok, M, N};
        false -> error
    end;
record_name(Src, []) ->
    case is_record(Src) of
        true -> {ok, records:get_module(Src), records:get_name(Src)};
        false -> error
    end;
record_name(Src, N) ->
    case is_record(Src, N) of
        true -> {ok, shell_default, N};
        false -> error
    end.

record_access(Anno, {var, _, Src0}, Name, {atom, _, K}, Bs, Ev, RBs) ->
    case binding(Src0, Bs) of
        {value, Src} ->
            case record_name(Src, Name) of
                {ok, _, _} -> ret(records:get(K, Src), Bs, RBs);
                error -> fail({badarg, Src}, Anno, Bs, Ev, RBs)
            end;
        unbound ->
            fail({unbound, Src0}, Anno, Bs, Ev, RBs)
    end;
record_access(Anno, Src, Name, Field, Bs, Ev, RBs) ->
    case Src of
        {var, _, _} -> ok;
        _ -> fail({badarg, Src}, Anno, Bs, Ev, RBs)
    end,
    case Field of
        {atom, _, _} -> ok;
        _ -> fail({badarg, Field}, Anno, Bs, Ev, RBs)
    end,
    fail({badarg, Name}, Anno, Bs, Ev, RBs).

%% Funs

%% fun Name/Arity: an auto-imported BIF, else a fun calling the local function when it is called.
local_fun(Anno, Name, Arity, Bs, Ev, RBs) ->
    case erl_internal:bif(Name, Arity) of
        true ->
            ret(erlang:make_fun(erlang, Name, Arity), Bs, RBs);
        false ->
            Args = [{var, Anno, list_to_atom("@arg" ++ [V])} || V <- lists:seq($a, $a + Arity - 1)],
            Clause =
                {clause, Anno, Args, [{atom, Anno, true}], [{call, Anno, {atom, Anno, Name}, Args}]},
            eval({'fun', Anno, {clauses, [Clause]}}, Bs, Ev, RBs)
    end.

make_fun(Anno, Fun, Cs, Name, Bs, Ev, RBs) ->
    {Env, Inner} = closure_bindings(Fun, Cs, Bs, Ev),
    Info =
        case Name of
            none -> {Anno, Env, Ev#ev.lf, Ev#ev.ef, Inner, Cs};
            _ -> {Anno, Env, Ev#ev.lf, Ev#ev.ef, Inner, Cs, Name}
        end,
    Arity = length(element(3, hd(Cs))),
    F =
        if
            Arity =< 20, Name =:= none -> closure(Arity, Info);
            Arity =< 20 -> named_closure(Arity, Info);
            true -> fail({argument_limit, limit_form(Anno, Name, Cs)}, Anno, Bs, Ev, RBs)
        end,
    ret(F, Bs, RBs).

limit_form(Anno, none, Cs) -> {'fun', erl_anno:location(Anno), to_terms(Cs)};
limit_form(Anno, Name, Cs) -> {named_fun, erl_anno:location(Anno), Name, to_terms(Cs)}.

%% Only the bindings the fun uses are kept; the used variables of the funs inside come along.
closure_bindings(Fun, Cs, Bs, #ev{fuvs = Fuvs}) ->
    {Used, Inner} =
        case Fuvs of
            #{Cs := Known} ->
                Known;
            #{} ->
                [{_, Computed}] = maps:to_list(erl_lint:used_vars([Fun], bindings(Bs))),
                Computed
        end,
    {filter_bindings(fun(K, _) -> lists:member(K, Used) end, Bs), Inner}.

%% A fun of each arity up to 20 evaluating the clauses of Info when called.
closure(0, Info) ->
    fun() -> eval_fun([], Info) end;
closure(1, Info) ->
    fun(A) -> eval_fun([A], Info) end;
closure(2, Info) ->
    fun(A, B) -> eval_fun([A, B], Info) end;
closure(3, Info) ->
    fun(A, B, C) -> eval_fun([A, B, C], Info) end;
closure(4, Info) ->
    fun(A, B, C, D) -> eval_fun([A, B, C, D], Info) end;
closure(5, Info) ->
    fun(A, B, C, D, E) -> eval_fun([A, B, C, D, E], Info) end;
closure(6, Info) ->
    fun(A, B, C, D, E, F) -> eval_fun([A, B, C, D, E, F], Info) end;
closure(7, Info) ->
    fun(A, B, C, D, E, F, G) -> eval_fun([A, B, C, D, E, F, G], Info) end;
closure(8, Info) ->
    fun(A, B, C, D, E, F, G, H) -> eval_fun([A, B, C, D, E, F, G, H], Info) end;
closure(9, Info) ->
    fun(A, B, C, D, E, F, G, H, I) -> eval_fun([A, B, C, D, E, F, G, H, I], Info) end;
closure(10, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J) -> eval_fun([A, B, C, D, E, F, G, H, I, J], Info) end;
closure(11, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K) -> eval_fun([A, B, C, D, E, F, G, H, I, J, K], Info) end;
closure(12, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L], Info)
    end;
closure(13, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M], Info)
    end;
closure(14, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N], Info)
    end;
closure(15, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O], Info)
    end;
closure(16, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P], Info)
    end;
closure(17, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q], Info)
    end;
closure(18, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R], Info)
    end;
closure(19, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S], Info)
    end;
closure(20, Info) ->
    fun(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T) ->
        eval_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T], Info)
    end.

%% The same for named funs, which see themselves under their name.
named_closure(0, Info) ->
    fun RF() -> eval_named_fun([], RF, Info) end;
named_closure(1, Info) ->
    fun RF(A) -> eval_named_fun([A], RF, Info) end;
named_closure(2, Info) ->
    fun RF(A, B) -> eval_named_fun([A, B], RF, Info) end;
named_closure(3, Info) ->
    fun RF(A, B, C) -> eval_named_fun([A, B, C], RF, Info) end;
named_closure(4, Info) ->
    fun RF(A, B, C, D) -> eval_named_fun([A, B, C, D], RF, Info) end;
named_closure(5, Info) ->
    fun RF(A, B, C, D, E) -> eval_named_fun([A, B, C, D, E], RF, Info) end;
named_closure(6, Info) ->
    fun RF(A, B, C, D, E, F) -> eval_named_fun([A, B, C, D, E, F], RF, Info) end;
named_closure(7, Info) ->
    fun RF(A, B, C, D, E, F, G) -> eval_named_fun([A, B, C, D, E, F, G], RF, Info) end;
named_closure(8, Info) ->
    fun RF(A, B, C, D, E, F, G, H) -> eval_named_fun([A, B, C, D, E, F, G, H], RF, Info) end;
named_closure(9, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I) -> eval_named_fun([A, B, C, D, E, F, G, H, I], RF, Info) end;
named_closure(10, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J], RF, Info)
    end;
named_closure(11, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K], RF, Info)
    end;
named_closure(12, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L], RF, Info)
    end;
named_closure(13, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M], RF, Info)
    end;
named_closure(14, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M, N) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N], RF, Info)
    end;
named_closure(15, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O], RF, Info)
    end;
named_closure(16, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P], RF, Info)
    end;
named_closure(17, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q], RF, Info)
    end;
named_closure(18, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R], RF, Info)
    end;
named_closure(19, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S], RF, Info)
    end;
named_closure(20, Info) ->
    fun RF(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T) ->
        eval_named_fun([A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T], RF, Info)
    end.

%% A call of a fun made here from compiled code or apply: only the value comes back.
eval_fun(As, {Anno, Bs, Lf, Ef, Fuvs, Cs}) ->
    fun_clauses(Cs, As, Anno, Bs, none, #ev{lf = Lf, ef = Ef, fuvs = Fuvs}, value).

eval_named_fun(As, Self, {Anno, Bs, Lf, Ef, Fuvs, Cs, Name}) ->
    fun_clauses(Cs, As, Anno, Bs, {Name, Self}, #ev{lf = Lf, ef = Ef, fuvs = Fuvs}, value).

%% The first clause whose head matches and whose guard holds; head variables shadow the fun's bindings.
fun_clauses([{clause, _, H, G, B} | Cs], As, Anno, Bs0, Self, Ev, RBs) ->
    Bs1 =
        case Self of
            none -> Bs0;
            {Name, Fun} -> add_binding(Name, Fun, Bs0)
        end,
    case match_list(H, As, Anno, new_bindings(Bs0), Bs1, Ev) of
        {match, Bsn} ->
            Bs2 = add_bindings(Bsn, Bs1),
            case guard(G, Bs2, Ev) of
                true -> seq(B, Bs2, Ev, RBs);
                false -> fun_clauses(Cs, As, Anno, Bs0, Self, Ev, RBs)
            end;
        nomatch ->
            fun_clauses(Cs, As, Anno, Bs0, Self, Ev, RBs)
    end;
fun_clauses([], As, Anno, Bs, _Self, Ev, RBs) ->
    Stack = [{?MODULE, '-inside-an-interpreted-fun-', As} | stacktrace()],
    fail(function_clause, Stack, Anno, Bs, Ev, RBs).

%% Calls

%% Created for qlc (plan 21 step LIB73): qlc:q/1,2 of a query list comprehension, transformed by qlc first with the
%% local calls hidden from the transform so that it does not expand them; a transform error is the value.
qlc_query(LC, As0, Bs, Ev, RBs) ->
    MaxLine = max_line(LC),
    {LC1, _, Hidden} = hide_calls(LC, MaxLine + 1, #{}),
    case (qlc_module()):transform_from_evaluator(LC1, Bs) of
        {ok, {call, A, Remote, [QLC]}} ->
            eval({call, A, Remote, [unhide_calls(QLC, MaxLine, Hidden) | As0]}, Bs, Ev, RBs);
        {not_ok, Error} ->
            ret(Error, Bs, RBs)
    end.

%% Created for qlc: reached by a runtime name so that only programs evaluating queries link it.
qlc_module() -> qlc.

%% The greatest line of the annotations of an expression, 0 without any.
max_line(Expr) ->
    erl_parse:fold_anno(
        fun(A, Max) ->
            case erl_anno:line(A) of
                L when is_integer(L), L > Max -> L;
                _ -> Max
            end
        end,
        0,
        Expr
    ).

%% Each local call as a call of m:f annotated with a fresh line (Id) above the expression's lines, Hidden mapping the
%% line back to the function name; auto-imported BIFs stay as they are but use up an Id as well.
hide_calls({call, A, {atom, _, N} = Atom, Args}, Id0, Hidden0) ->
    {NArgs, Id, Hidden} = hide_calls(Args, Id0, Hidden0),
    Call =
        case erl_internal:bif(N, length(Args)) of
            true -> {call, A, Atom, NArgs};
            false -> {call, erl_anno:new(Id), {remote, A, {atom, A, m}, {atom, A, f}}, NArgs}
        end,
    {Call, Id + 1, Hidden#{Id => {call, Atom}}};
hide_calls(T0, Id0, Hidden0) when is_tuple(T0) ->
    {L, Id, Hidden} = hide_calls(tuple_to_list(T0), Id0, Hidden0),
    {list_to_tuple(L), Id, Hidden};
hide_calls([E0 | Es0], Id0, Hidden0) ->
    {E, Id1, Hidden1} = hide_calls(E0, Id0, Hidden0),
    {Es, Id, Hidden} = hide_calls(Es0, Id1, Hidden1),
    {[E | Es], Id, Hidden};
hide_calls(E, Id, Hidden) ->
    {E, Id, Hidden}.

unhide_calls({call, Anno, {remote, A, {atom, A, m}, {atom, A, f}} = F, Args}, MaxLine, Hidden) ->
    case erl_anno:line(Anno) of
        Line when Line > MaxLine ->
            {call, Atom} = map_get(Line, Hidden),
            {call, A, Atom, unhide_calls(Args, MaxLine, Hidden)};
        _ ->
            {call, Anno, F, unhide_calls(Args, MaxLine, Hidden)}
    end;
unhide_calls(T, MaxLine, Hidden) when is_tuple(T) ->
    list_to_tuple(unhide_calls(tuple_to_list(T), MaxLine, Hidden));
unhide_calls([E | Es], MaxLine, Hidden) ->
    [unhide_calls(E, MaxLine, Hidden) | unhide_calls(Es, MaxLine, Hidden)];
unhide_calls(E, _MaxLine, _Hidden) ->
    E.

%% A local call: the local function handler's, or undef without one.
local_call(Func, As0, _Anno, Bs, #ev{lf = {value, F}} = Ev, value) ->
    {As, _} = values(As0, Bs, Ev),
    F(Func, As);
local_call(Func, As0, _Anno, Bs, #ev{lf = {value, F}} = Ev, RBs) ->
    {As, Bs1} = values(As0, Bs, Ev),
    ret(F(Func, As), Bs1, RBs);
local_call(Func, As0, Anno, Bs, #ev{lf = {value, F, Extra}} = Ev, RBs) ->
    Handler = fun(Name, Args) -> apply(F, [Name, Args | Extra]) end,
    local_call(Func, As0, Anno, Bs, Ev#ev{lf = {value, Handler}}, RBs);
local_call(Func, As, Anno, Bs, #ev{lf = {eval, F}} = Ev, RBs) ->
    local_result(F(Func, As, Bs), Anno, Ev, RBs);
local_call(Func, As, Anno, Bs, #ev{lf = {eval, F, Extra}} = Ev, RBs) ->
    local_result(apply(F, [Func, As, Bs | Extra]), Anno, Ev, RBs);
local_call(Func, As0, _Anno, Bs, #ev{lf = {M, F}} = Ev, RBs) when is_atom(M), is_atom(F) ->
    {As, Bs1} = values(As0, Bs, Ev),
    ret(M:F(Func, As), Bs1, RBs);
local_call(Func, As, Anno, _Bs, #ev{lf = {M, F, Extra}} = Ev, RBs) ->
    local_result(apply(M, F, [Func, As | Extra]), Anno, Ev, RBs);
local_call(Func, As0, Anno, Bs, #ev{lf = none} = Ev, RBs) ->
    fail(undef, [{?MODULE, Func, length(As0)} | stacktrace()], Anno, Bs, Ev, RBs).

local_result({value, V, Bs}, _Anno, _Ev, RBs) ->
    ret(V, Bs, RBs);
local_result({eval, F, As, Bs}, Anno, Ev, RBs) ->
    apply_fun(F, As, Anno, Bs, Ev#ev{ef = none}, RBs).

%% An auto-imported BIF; apply/2,3 are hidden from the non-local handler.
bif(apply, [erlang, apply, As], Anno, Bs, Ev, RBs) -> bif(apply, As, Anno, Bs, Ev, RBs);
bif(apply, [M, F, As], Anno, Bs, Ev, RBs) -> apply_mf(M, F, As, Anno, Bs, Ev, RBs);
bif(apply, [F, As], Anno, Bs, Ev, RBs) -> apply_fun(F, As, Anno, Bs, Ev, RBs);
bif(Name, As, Anno, Bs, Ev, RBs) -> apply_mf(erlang, Name, As, Anno, Bs, Ev, RBs).

%% A call of a fun: one made here runs its clauses directly, others go through the non-local handler.
apply_fun(Func, As, Anno, Bs, Ev, RBs) ->
    Env =
        case is_function(Func) of
            true -> fun_env(Func);
            false -> none
        end,
    case Env of
        {ok, Info} -> call_interpreted(Func, Info, As, Anno, Bs, Ev, RBs);
        none -> call_handler(Func, As, Anno, Bs, Ev, RBs)
    end.

%% Inside another fun's body (RBs is bindings) those bindings come back, else the caller's.
call_interpreted(Func, Info, As, Anno, Bs, Ev, RBs) ->
    Returned =
        case RBs of
            none -> Bs;
            _ -> RBs
        end,
    case erlang:fun_info(Func, arity) =:= {arity, length(As)} of
        true -> run_interpreted(Func, Info, As, Returned);
        false -> fail({badarity, {Func, As}}, Anno, Bs, Ev, RBs)
    end.

run_interpreted(_Func, {Anno, Bs, Lf, Ef, Fuvs, Cs}, As, RBs) ->
    fun_clauses(Cs, As, Anno, Bs, none, #ev{lf = Lf, ef = Ef, fuvs = Fuvs}, RBs);
run_interpreted(Func, {Anno, Bs, Lf, Ef, Fuvs, Cs, Name}, As, RBs) ->
    fun_clauses(Cs, As, Anno, Bs, {Name, Func}, #ev{lf = Lf, ef = Ef, fuvs = Fuvs}, RBs).

call_handler(Func, As, _Anno, _Bs, #ev{ef = none}, value) ->
    apply(Func, As);
call_handler(Func, As, _Anno, Bs, #ev{ef = none}, RBs) ->
    ret(apply(Func, As), Bs, RBs);
call_handler(Func, As, Anno, _Bs, #ev{ef = {value, F}}, value) ->
    non_local(F, Anno, Func, As);
call_handler(Func, As, Anno, Bs, #ev{ef = {value, F}}, RBs) ->
    ret(non_local(F, Anno, Func, As), Bs, RBs).

%% A call of Module:Function, through the non-local handler when there is one.
apply_mf(M, F, As, _Anno, _Bs, #ev{ef = none}, value) ->
    apply(M, F, As);
apply_mf(M, F, As, _Anno, Bs, #ev{ef = none}, RBs) ->
    ret(apply(M, F, As), Bs, RBs);
apply_mf(M, F, As, Anno, _Bs, #ev{ef = {value, H}}, value) ->
    non_local(H, Anno, {M, F}, As);
apply_mf(M, F, As, Anno, Bs, #ev{ef = {value, H}}, RBs) ->
    ret(non_local(H, Anno, {M, F}, As), Bs, RBs).

non_local(H, Anno, FuncSpec, As) when is_function(H, 3) -> H(Anno, FuncSpec, As);
non_local(H, _Anno, FuncSpec, As) when is_function(H, 2) -> H(FuncSpec, As).

%% Clauses

if_clauses([{clause, _, [], G, B} | Cs], Anno, Bs, Ev, RBs) ->
    case guard(G, Bs, Ev) of
        true -> seq(B, Bs, Ev, RBs);
        false -> if_clauses(Cs, Anno, Bs, Ev, RBs)
    end;
if_clauses([], Anno, Bs, Ev, RBs) ->
    fail(if_clause, Anno, Bs, Ev, RBs).

try_expr(Anno, B, Cases, Catches, After, Bs, Ev, RBs) ->
    stacktrace_vars(Catches, Anno, Bs, Ev, RBs),
    try seq(B, Bs, Ev, none) of
        {value, V, Bs1} when Cases =:= [] ->
            ret(V, Bs1, RBs);
        {value, V, Bs1} ->
            case select_clause(Cases, [V], Bs1, Ev) of
                {Body, Bs2} -> seq(Body, Bs2, Ev, RBs);
                nomatch -> fail({try_clause, V}, Anno, Bs, Ev, RBs)
            end
    catch
        Class:Reason:Stack when Catches =:= [] ->
            erlang:raise(Class, Reason, Stack);
        Class:Reason:Stack ->
            case select_clause(Catches, [{Class, Reason, Stack}], Bs, Ev) of
                {Body, Bs2} -> seq(Body, Bs2, Ev, RBs);
                nomatch -> erlang:raise(Class, Reason, Stack)
            end
    after
        After =/= [] andalso seq(After, Bs, Ev, none)
    end.

%% A catch clause's stack trace variable must be new.
stacktrace_vars([{clause, _, [{tuple, _, [_, _, Var]}], _, _} | Cs], Anno, Bs, Ev, RBs) ->
    case Var of
        {var, _, V} ->
            case binding(V, Bs) of
                {value, _} -> fail(stacktrace_bound, Anno, Bs, Ev, RBs);
                unbound -> stacktrace_vars(Cs, Anno, Bs, Ev, RBs)
            end;
        _ ->
            fail({illegal_stacktrace_variable, Var}, Anno, Bs, Ev, RBs)
    end;
stacktrace_vars([], _Anno, _Bs, _Ev, _RBs) ->
    ok.

receive_clauses(Timeout, Cs, After, Bs, Ev, RBs) ->
    Match = fun(Message) -> select_clause(Cs, [Message], Bs, Ev) end,
    case prim_eval:'receive'(Match, Timeout) of
        {Body, Bs1} ->
            seq(Body, Bs1, Ev, RBs);
        timeout ->
            {AfterBody, Bs1} = After,
            seq(AfterBody, Bs1, Ev, RBs)
    end.

select_clause([{clause, Anno, H, G, B} | Cs], Vs, Bs, Ev) ->
    case match_list(H, Vs, Anno, Bs, Bs, Ev) of
        {match, Bs1} ->
            case guard(G, Bs1, Ev) of
                true -> {B, Bs1};
                false -> select_clause(Cs, Vs, Bs, Ev)
            end;
        nomatch ->
            select_clause(Cs, Vs, Bs, Ev)
    end;
select_clause([], _Vs, _Bs, _Ev) ->
    nomatch.

%% Guards: a disjunction of conjunctions, or one conjunction; a test that fails is false.
guard([G | _] = Disjunction, Bs, Ev) when is_list(G) ->
    lists:any(fun(Conjunction) -> conjunction(Conjunction, Bs, Ev) end, Disjunction);
guard(Tests, Bs, Ev) ->
    conjunction(Tests, Bs, Ev).

conjunction([G | Gs], Bs, Ev) ->
    case erl_lint:is_guard_test(G) of
        true ->
            case guard_test(G, Bs, Ev) of
                {value, true, Bs1} -> conjunction(Gs, Bs1, Ev);
                {value, false, _} -> false
            end;
        false ->
            fail(guard_expr, element(2, G), Bs, Ev, none)
    end;
conjunction([], _Bs, _Ev) ->
    true.

guard_test({call, A, {atom, Ln, F}, As}, Bs, Ev) ->
    expr_guard_test({call, A, {atom, Ln, type_test(F)}, As}, Bs, Ev);
guard_test({call, A, {remote, Ar, {atom, Am, erlang}, {atom, Af, F}}, As}, Bs, Ev) ->
    expr_guard_test(
        {call, A, {remote, Ar, {atom, Am, erlang}, {atom, Af, type_test(F)}}, As}, Bs, Ev
    );
guard_test(G, Bs, Ev) ->
    expr_guard_test(G, Bs, Ev).

expr_guard_test(G, Bs, Ev) ->
    try {value, true, _} = eval(G, Bs, Ev, none) of
        Result -> Result
    catch
        error:_ -> {value, false, Bs}
    end.

%% The old type tests (integer(X), ...) as their is_ forms.
type_test(integer) -> is_integer;
type_test(float) -> is_float;
type_test(number) -> is_number;
type_test(atom) -> is_atom;
type_test(list) -> is_list;
type_test(tuple) -> is_tuple;
type_test(pid) -> is_pid;
type_test(reference) -> is_reference;
type_test(port) -> is_port;
type_test(function) -> is_function;
type_test(binary) -> is_binary;
type_test(record) -> is_record;
type_test(map) -> is_map;
type_test(Test) -> Test.

%% Comprehensions

%% Each qualifier in turn over the accumulator; the template adds to it when all hold.
comprehension(Kind, Template, [{zip, Anno, Generators} | Qs], Bs, Ev, Acc) ->
    zip(Kind, Template, Anno, zip_sources(Generators, Bs, Ev), Qs, Bs, Ev, Acc);
comprehension(Kind, Template, [Q | Qs], Bs, Ev, Acc) ->
    case is_generator(Q) of
        true ->
            Next = fun(Bs1, Acc1) -> comprehension(Kind, Template, Qs, Bs1, Ev, Acc1) end,
            generator(Q, Bs, Ev, Acc, Next);
        false ->
            filter(Q, Bs, Ev, Acc, fun(Bs1) -> comprehension(Kind, Template, Qs, Bs1, Ev, Acc) end)
    end;
comprehension(lc, Template, [], Bs, Ev, Acc) ->
    Es =
        case Template of
            [_ | _] -> Template;
            _ -> [Template]
        end,
    {Vs, _} = side_by_side(Es, Acc, Bs, Bs, Ev),
    Vs;
comprehension(bc, Template, [], Bs, Ev, Acc) ->
    {value, V, _} = eval(Template, Bs, Ev, none),
    <<Acc/bitstring, V/bitstring>>;
comprehension(mc, Template, [], Bs, Ev, Acc) ->
    Fields =
        case Template of
            [_ | _] -> Template;
            _ -> [Template]
        end,
    lists:foldl(
        fun({map_field_assoc, _, K0, V0}, Acc1) ->
            {[K, V], _} = values([K0, V0], Bs, Ev),
            [{K, V} | Acc1]
        end,
        Acc,
        Fields
    ).

is_generator({Tag, _, _, _}) ->
    lists:member(Tag, [
        match,
        generate,
        generate_strict,
        b_generate,
        b_generate_strict,
        m_generate,
        m_generate_strict
    ]);
is_generator(_) ->
    false.

filter(F, Bs, Ev, Acc, Next) ->
    case erl_lint:is_guard_test(F) of
        true ->
            case guard_test(F, Bs, Ev) of
                {value, true, Bs1} -> Next(Bs1);
                {value, false, _} -> Acc
            end;
        false ->
            case eval(F, Bs, Ev, none) of
                {value, true, Bs1} -> Next(Bs1);
                {value, false, _} -> Acc;
                {value, V, _} -> fail({bad_filter, V}, element(2, F), Bs, Ev, none)
            end
    end.

generator({match, Anno, P, E}, Bs, Ev, Acc, Next) ->
    generator({generate_strict, Anno, P, {cons, Anno, E, {nil, Anno}}}, Bs, Ev, Acc, Next);
generator({Tag, Anno, P, E}, Bs, Ev, Acc, Next) when Tag =:= generate; Tag =:= generate_strict ->
    {value, L, _} = eval(E, Bs, Ev, none),
    list_generator(L, P, Anno, Bs, Ev, Next, Tag =:= generate, Acc);
generator({Tag, Anno, P, E}, Bs, Ev, Acc, Next) when
    Tag =:= b_generate; Tag =:= b_generate_strict
->
    {value, Bin, _} = eval(E, Bs, Ev, none),
    bits_generator(Bin, P, Anno, Bs, Ev, Next, Tag =:= b_generate, Acc);
generator({Tag, Anno, {map_field_exact, _, K, V}, E}, Bs, Ev, Acc, Next) ->
    {value, Map, _} = eval(E, Bs, Ev, none),
    Iter = map_iterator(Map, Anno, Bs, Ev),
    map_generator(Iter, {tuple, Anno, [K, V]}, Anno, Bs, Ev, Next, Tag =:= m_generate, Acc).

%% A map, or an iterator maps:foreach/2 accepts.
map_iterator(Map, _Anno, _Bs, _Ev) when is_map(Map) ->
    maps:iterator(Map);
map_iterator(Iter, Anno, Bs, Ev) ->
    try maps:foreach(fun(_, _) -> ok end, Iter) of
        _ -> Iter
    catch
        _:_ -> fail({bad_generator, Iter}, Anno, Bs, Ev, none)
    end.

%% Generator patterns bind anew: matched over empty bindings, then shadowing the outer ones.
list_generator([V | Rest], P, Anno, Bs, Ev, Next, Relaxed, Acc) ->
    case match(P, V, Anno, new_bindings(Bs), Bs, Ev) of
        {match, Bsn} ->
            list_generator(Rest, P, Anno, Bs, Ev, Next, Relaxed, Next(add_bindings(Bsn, Bs), Acc));
        nomatch when Relaxed -> list_generator(Rest, P, Anno, Bs, Ev, Next, Relaxed, Acc);
        nomatch ->
            fail({badmatch, V}, Anno, Bs, Ev, none)
    end;
list_generator([], _P, _Anno, _Bs, _Ev, _Next, _Relaxed, Acc) ->
    Acc;
list_generator(Term, _P, Anno, Bs, Ev, _Next, _Relaxed, _Acc) ->
    fail({bad_generator, Term}, Anno, Bs, Ev, none).

bits_generator(<<_/bitstring>> = Bin, P, Anno, Bs, Ev, Next, Relaxed, Acc) ->
    case bin_generate(P, Bin, Bs, Ev) of
        {match, Rest, Bsn} ->
            bits_generator(Rest, P, Anno, Bs, Ev, Next, Relaxed, Next(add_bindings(Bsn, Bs), Acc));
        {nomatch, Rest} when Relaxed -> bits_generator(Rest, P, Anno, Bs, Ev, Next, Relaxed, Acc);
        {nomatch, _} ->
            fail({badmatch, Bin}, Anno, Bs, Ev, none);
        done when not Relaxed, Bin =/= <<>> -> fail({badmatch, Bin}, Anno, Bs, Ev, none);
        done ->
            Acc
    end;
bits_generator(Term, _P, Anno, Bs, Ev, _Next, _Relaxed, _Acc) ->
    fail({bad_generator, Term}, Anno, Bs, Ev, none).

map_generator(Iter0, P, Anno, Bs, Ev, Next, Relaxed, Acc) ->
    case maps:next(Iter0) of
        {K, V, Iter} ->
            case match(P, {K, V}, Anno, new_bindings(Bs), Bs, Ev) of
                {match, Bsn} ->
                    map_generator(
                        Iter, P, Anno, Bs, Ev, Next, Relaxed, Next(add_bindings(Bsn, Bs), Acc)
                    );
                nomatch when Relaxed -> map_generator(Iter, P, Anno, Bs, Ev, Next, Relaxed, Acc);
                nomatch ->
                    fail({badmatch, {K, V}}, Anno, Bs, Ev, none)
            end;
        none ->
            Acc
    end.

%% Binary generator patterns over the outer bindings for sizes (clause_erl_eval:bits_gen/4): {match, Rest,
%% Bindings}, {nomatch, Rest} once a segment did not match (the rest still taken), or done when a segment cannot be
%% taken; bad specifiers raise.
bin_generate({bin, Anno, Fields}, Bin, Bs, Ev) ->
    try
        clause_erl_eval:bits_gen(Fields, Bin, new_bindings(Bs), Bs)
    catch
        error:Reason -> fail(Reason, Anno, Bs, Ev, none)
    end.

%% Zip generators: the sources evaluated once, then one element of each per round. A round whose elements do not all
%% match (or whose patterns bind a variable differently) is skipped, or an error for strict generators; generators
%% ending at different times are {bad_generators, Remaining}.

zip_sources(Generators, Bs, Ev) ->
    [zip_source(G, Bs, Ev) || G <- Generators].

zip_source({Tag, Anno, {map_field_exact, _, K, V}, E}, Bs, Ev) when
    Tag =:= m_generate; Tag =:= m_generate_strict
->
    {value, Map, _} = eval(E, Bs, Ev, none),
    {Tag, Anno, {tuple, Anno, [K, V]}, map_iterator(Map, Anno, Bs, Ev)};
zip_source({Tag, Anno, P, E}, Bs, Ev) ->
    {value, Source, _} = eval(E, Bs, Ev, none),
    {Tag, Anno, P, Source}.

zip(Kind, Template, Anno, Sources, Qs, Bs, Ev, Acc) ->
    Remaining =
        case zip_remaining(Sources, Bs, Ev) of
            {ok, Values} -> Values;
            {error, Reason} -> fail(Reason, Anno, Bs, Ev, none)
        end,
    Strict = strict_vars(Sources),
    case zip_round(Sources, Bs, Ev, Strict, new_bindings(Bs), [], continue) of
        error ->
            fail({bad_generators, Remaining}, Anno, Bs, Ev, none);
        done ->
            Acc;
        {skip, Rest} ->
            zip(Kind, Template, Anno, Rest, Qs, Bs, Ev, Acc);
        {Bound, Rest} ->
            Acc1 = comprehension(Kind, Template, Qs, add_bindings(Bound, Bs), Ev, Acc),
            zip(Kind, Template, Anno, Rest, Qs, Bs, Ev, Acc1)
    end.

%% The values left in each generator: an error when some have ended and others not.
zip_remaining(Sources, Bs, Ev) ->
    Values = [zip_value(S, Bs, Ev) || S <- Sources],
    Ended = [V || V <- Values, V =:= [] orelse V =:= <<>> orelse V =:= #{}],
    if
        Ended =:= []; length(Ended) =:= length(Values) -> {ok, list_to_tuple(Values)};
        true -> {error, {bad_generators, list_to_tuple(Values)}}
    end.

zip_value({Tag, _, _, Iter}, _Bs, _Ev) when Tag =:= m_generate; Tag =:= m_generate_strict ->
    case maps:next(Iter) of
        none -> #{};
        _ -> iterator_map(Iter, #{})
    end;
zip_value({Tag, _, P, <<_/bitstring>> = Bin}, Bs, Ev) when
    Tag =:= b_generate; Tag =:= b_generate_strict
->
    case bin_generate(P, Bin, Bs, Ev) of
        done -> <<>>;
        _ -> Bin
    end;
zip_value({_, _, _, Value}, _Bs, _Ev) ->
    Value.

iterator_map(Iter0, Acc) ->
    case maps:next(Iter0) of
        {K, V, Iter} -> iterator_map(Iter, Acc#{K => V});
        none -> Acc
    end.

%% The variables of strict generator patterns: a clash on one of them is an error, not a skipped round.
strict_vars(Sources) ->
    lists:usort(lists:append([pattern_vars(P) || {Tag, _, P, _} <- Sources, is_strict(Tag)])).

is_strict(Tag) -> lists:member(Tag, [generate_strict, b_generate_strict, m_generate_strict]).

pattern_vars({tuple, _, Es}) ->
    lists:append([pattern_vars(E) || E <- Es]);
pattern_vars({var, _, V}) ->
    [V];
pattern_vars({bin, _, Elements}) ->
    lists:append([pattern_vars(V) || {bin_element, _, V, _, _} <- Elements]);
pattern_vars(_) ->
    [].

%% One round: the next element of each generator matched, bindings joined. State is continue, or skip once an
%% element failed to match. Returns error, done, {skip, Rest} or {Bindings, Rest}.
zip_round([{Tag, Anno, P, Source} | Sources], Bs, Ev, Strict, Joined, Rest, State) ->
    case zip_next(Tag, P, Source, Anno, Bs, Ev) of
        {ok, Bsn, Next} ->
            Gen = {Tag, Anno, P, Next},
            zip_join(Sources, Bs, Ev, Strict, Joined, [Gen | Rest], Bsn, is_strict(Tag), State);
        {nomatch, Next} ->
            case is_strict(Tag) of
                true ->
                    error;
                false ->
                    zip_round(Sources, Bs, Ev, Strict, Joined, [{Tag, Anno, P, Next} | Rest], skip)
            end;
        ended ->
            done;
        error ->
            error
    end;
zip_round([], _Bs, _Ev, _Strict, _Joined, Rest, skip) ->
    {skip, lists:reverse(Rest)};
zip_round([], _Bs, _Ev, _Strict, Joined, Rest, continue) ->
    {Joined, lists:reverse(Rest)}.

%% A matched element's bindings joined to the round's. A clash skips the round, or is an error for a strict generator
%% or a variable of a strict pattern; once skipping, only strict generators still add their bindings.
zip_join(Sources, Bs, Ev, Strict, Joined, Rest, Bsn, StrictTag, State) ->
    case {join_bindings(bindings(Bsn), Joined, Strict), StrictTag, State} of
        {strict_clash, _, _} -> error;
        {clash, true, _} -> error;
        {clash, false, _} -> zip_round(Sources, Bs, Ev, Strict, Joined, Rest, skip);
        {{ok, _}, false, skip} -> zip_round(Sources, Bs, Ev, Strict, Joined, Rest, skip);
        {{ok, Joined1}, _, _} -> zip_round(Sources, Bs, Ev, Strict, Joined1, Rest, State)
    end.

join_bindings([{V, Value} | Vs], Joined, Strict) ->
    case binding(V, Joined) of
        {value, Value} ->
            join_bindings(Vs, Joined, Strict);
        {value, _} ->
            case lists:member(V, Strict) of
                true -> strict_clash;
                false -> clash
            end;
        unbound ->
            join_bindings(Vs, add_binding(V, Value, Joined), Strict)
    end;
join_bindings([], Joined, _Strict) ->
    {ok, Joined}.

%% The next element of one generator: {ok, Bindings, Rest}, {nomatch, Rest}, ended or error.
zip_next(Tag, P, [H | T], Anno, Bs, Ev) when Tag =:= generate; Tag =:= generate_strict ->
    case match(P, H, Anno, new_bindings(Bs), Bs, Ev) of
        {match, Bsn} -> {ok, Bsn, T};
        nomatch -> {nomatch, T}
    end;
zip_next(Tag, _P, [], _Anno, _Bs, _Ev) when Tag =:= generate; Tag =:= generate_strict ->
    ended;
zip_next(Tag, P, <<_/bitstring>> = Bin, _Anno, Bs, Ev) when
    Tag =:= b_generate; Tag =:= b_generate_strict
->
    case bin_generate(P, Bin, Bs, Ev) of
        {match, Rest, Bsn} -> {ok, Bsn, Rest};
        {nomatch, Rest} -> {nomatch, Rest};
        done when Bin =:= <<>> -> ended;
        done when Tag =:= b_generate -> ended;
        done -> error
    end;
zip_next(Tag, P, Iter, Anno, Bs, Ev) when Tag =:= m_generate; Tag =:= m_generate_strict ->
    case maps:next(Iter) of
        {K, V, Next} ->
            case match(P, {K, V}, Anno, new_bindings(Bs), Bs, Ev) of
                {match, Bsn} -> {ok, Bsn, Next};
                nomatch -> {nomatch, Next}
            end;
        none ->
            ended
    end;
zip_next(_Tag, _P, _Term, _Anno, _Bs, _Ev) ->
    error.

%% Pattern matching

%% match(Pattern, Term, Anno, Bindings, OuterBindings, Ev): {match, Bindings} or nomatch, matched in C++
%% (clause_erl_eval:match/4); an illegal pattern raises {illegal_pattern, Pattern}. OuterBindings give binary sizes
%% and map keys.
match(P, Term, Anno, Bs, BBs, Ev) ->
    case clause_erl_eval:match(P, Term, Bs, BBs) of
        invalid -> fail({illegal_pattern, to_term(P)}, Anno, Bs, Ev, none);
        Result -> Result
    end.

match_list([P | Ps], [T | Ts], Anno, Bs, BBs, Ev) ->
    case match(P, T, Anno, Bs, BBs, Ev) of
        {match, Bs1} -> match_list(Ps, Ts, Anno, Bs1, BBs, Ev);
        nomatch -> nomatch
    end;
match_list([], [], _Anno, Bs, _BBs, _Ev) ->
    {match, Bs};
match_list(_, _, _Anno, _Bs, _BBs, _Ev) ->
    nomatch.

%% Bit syntax

%% bin_element specifiers settled as make_bit_type/4 does: {Size, {Type, Unit, Sign, Endian}}.
bit_type(Anno, Size, Types, Error) ->
    try
        clause_erl_eval:bit_type(Size, Types)
    catch
        error:Reason -> Error(Anno, Reason, stacktrace())
    end.

%% A binary expression: every field's value and size evaluated in order first, then the binary built.
bits_expr(Fields, Bs0, Eval, Error) ->
    {Builders, Bs} = lists:foldl(
        fun(Field, {Acc, B}) ->
            {Builder, B1} = bits_field(Field, B, Eval, Error),
            {[Builder | Acc], B1}
        end,
        {[], Bs0},
        Fields
    ),
    {value,
        lists:foldl(
            fun(Build, Acc) -> <<Acc/bitstring, (Build())/bitstring>> end,
            <<>>,
            lists:reverse(Builders)
        ),
        Bs}.

bits_field({bin_element, _, {string, _, S}, Size, Types}, Bs, _Eval, _Error) when
    (Size =:= default andalso Types =:= default) orelse
        (Size =:= {integer, element(2, Size), 8} andalso
            Types =:= [integer, {unit, 1}, unsigned, big])
->
    Latin1 = [C band 16#FF || C <- S],
    {fun() -> list_to_binary(Latin1) end, Bs};
bits_field({bin_element, Anno, {string, _, S}, Size0, Types}, Bs, Eval, Error) ->
    {Size1, Spec} = bit_type(Anno, Size0, Types, Error),
    {Size, Bs1} = bit_size(Size0, Size1, Bs, Eval),
    Build = fun() ->
        S =:= "" andalso segment(Anno, 0, Size, Spec, Error),
        <<<<(segment(Anno, C, Size, Spec, Error))/bitstring>> || C <- S>>
    end,
    {Build, Bs1};
bits_field({bin_element, Anno, E, Size0, Types}, Bs, Eval, Error) ->
    {value, V, Bs1} = Eval(E, Bs),
    {Size1, Spec} = bit_type(Anno, Size0, Types, Error),
    {Size, Bs2} = bit_size(Size0, Size1, Bs1, Eval),
    {fun() -> segment(Anno, V, Size, Spec, Error) end, Bs2}.

bit_size(default, Size, Bs, _Eval) ->
    {Size, Bs};
bit_size(_, SizeExpr, Bs, Eval) ->
    {value, Size, Bs1} = Eval(SizeExpr, Bs),
    {Size, Bs1}.

segment(Anno, Value, Size, Spec, Error) ->
    case clause_erl_eval:bit_segment(Value, Size, Spec) of
        Bits when is_bitstring(Bits) -> Bits;
        Reason -> Error(Anno, Reason, stacktrace())
    end.

%% Constant expressions (runtime builtins, runtime/src/builtins/front_end/eval_match.cpp)

%% partial_eval(Expr): an operator expression over constants folded to the integer, float or atom it gives, else
%% Expr as it is.
partial_eval(_) -> clause_builtin_placeholder.

%% is_constant_expr(Expr): whether Expr folds to a number.
is_constant_expr(_) -> clause_builtin_placeholder.

%% Strings

%% eval_str(String): {ok, Value} of the expressions of one form, or {error, Message} as OTP words it.
eval_str(Bin) when is_binary(Bin) ->
    eval_str(binary_to_list(Bin));
eval_str(Str) when is_list(Str) ->
    case scan_str(Str) of
        {ok, Tokens} ->
            case erl_parse:parse_exprs(Tokens) of
                {ok, Exprs} ->
                    eval_parsed(Exprs);
                {error, {_Location, Module, Args}} ->
                    {error, text("*** ~ts", [Module:format_error(Args)])}
            end;
        {error, _} = Error ->
            Error
    end.

scan_str(Str) ->
    case erl_scan:tokens([], Str, 0) of
        {done, {ok, Tokens, _}, Rest} ->
            case lists:all(fun(C) -> C =:= $\s orelse C =:= $\n orelse C =:= $\t end, Rest) of
                true -> {ok, Tokens};
                false -> {error, text("*** Non-white space found after end-of-form: ~ts", [Rest])}
            end;
        {more, _} ->
            {error, text("*** Incomplete form (missing .<cr>?)", [])}
    end.

eval_parsed(Exprs) ->
    try exprs(Exprs, new_bindings()) of
        {value, Value, _} -> {ok, Value}
    catch
        error:Reason -> {error, text("*** eval failed: ~p", [Reason])};
        throw:Reason -> {error, text("*** eval failed with thrown exception: ~p", [Reason])};
        exit:Reason -> {error, text("*** eval failed with exit exception: ~p", [Reason])}
    end.

text(Format, Args) -> lists:flatten(io_lib:format(Format, Args)).

%% Extended parsing: the printed forms of funs, pids, ports and references (#Fun<m.f.1>, <0.1.2>, #Port<0.3>,
%% #Ref<0.1.2.3>) among tokens scanned with the text option.

%% extended_parse_exprs(Tokens): {ok, Exprs} with such items as calls of list_to_pid/1 and friends or fun M:F/A, the
%% annotations reset to locations; else erl_parse:parse_exprs/1's answer.
extended_parse_exprs(Tokens) ->
    Fixed = fix_tokens(Tokens),
    case erl_parse:parse_exprs(Fixed) of
        {ok, Exprs} -> {ok, [erl_parse:map_anno(fun reset_anno/1, item_exprs(E)) || E <- Exprs]};
        _ -> erl_parse:parse_exprs([setelement(2, T, reset_anno(element(2, T))) || T <- Fixed])
    end.

reset_anno(Anno) -> erl_anno:new(erl_anno:location(Anno)).

%% Item token runs become a string token holding their text, its annotation text naming the item kind.
fix_tokens([]) ->
    [];
fix_tokens([T | Ts] = Tokens) ->
    try fix_item(Tokens) of
        {Item, Rest} -> [Item | fix_tokens(Rest)];
        none -> [T | fix_tokens(Ts)]
    catch
        _:_ -> [T | fix_tokens(Ts)]
    end.

fix_item(Tokens) ->
    case item_run(Tokens) of
        {Annos, Rest, Kind} ->
            Text = lists:append([erl_anno:text(A) || A <- Annos]),
            _ = item_value(Kind, Text),
            {{string, erl_anno:set_text(atom_to_list(Kind), hd(Annos)), Text}, Rest};
        none ->
            none
    end.

item_run([
    {'#', A1}, {var, A2, 'Fun'}, {'<', A3}, {atom, A4, _}, {'.', A5}, {float, A6, _}, {'>', A7} | Ts
]) ->
    {[A1, A2, A3, A4, A5, A6, A7], Ts, function};
item_run([
    {'#', A1},
    {var, A2, 'Fun'},
    {'<', A3},
    {atom, A4, _},
    {'.', A5},
    {atom, A6, _},
    {'.', A7},
    {integer, A8, _},
    {'>', A9}
    | Ts
]) ->
    {[A1, A2, A3, A4, A5, A6, A7, A8, A9], Ts, function};
item_run([{'<', A1}, {float, A2, _}, {'.', A3}, {integer, A4, _}, {'>', A5} | Ts]) ->
    {[A1, A2, A3, A4, A5], Ts, pid};
item_run([{'#', A1}, {var, A2, 'Port'}, {'<', A3}, {float, A4, _}, {'>', A5} | Ts]) ->
    {[A1, A2, A3, A4, A5], Ts, port};
item_run([
    {'#', A1},
    {var, A2, 'Ref'},
    {'<', A3},
    {float, A4, _},
    {'.', A5},
    {float, A6, _},
    {'>', A7}
    | Ts
]) ->
    {[A1, A2, A3, A4, A5, A6, A7], Ts, reference};
item_run(_) ->
    none.

item_value(pid, Text) ->
    list_to_pid(Text);
item_value(port, Text) ->
    list_to_port(Text);
item_value(reference, Text) ->
    list_to_ref(Text);
item_value(function, Text) ->
    {M, F, A} = item_mfa(Text),
    erlang:make_fun(M, F, A).

item_mfa(Text) ->
    {ok, [_, _, _, {atom, _, M}, _, {atom, _, F}, _, {integer, _, A} | _], _} = erl_scan:string(
        Text
    ),
    {M, F, A}.

%% The string tokens fix_tokens/1 made, back as the expressions of their items.
item_exprs({string, A, S} = T) ->
    try item_expr(erl_anno:text(A), A, S) of
        Expr -> Expr
    catch
        _:_ -> T
    end;
item_exprs(T) when is_tuple(T) ->
    list_to_tuple(item_exprs(tuple_to_list(T)));
item_exprs([E | Es]) ->
    [item_exprs(E) | item_exprs(Es)];
item_exprs(T) ->
    T.

item_expr("pid", A, [$< | _] = S) ->
    item_call(A, list_to_pid, S);
item_expr("port", A, [$# | _] = S) ->
    item_call(A, list_to_port, S);
item_expr("reference", A, [$# | _] = S) ->
    item_call(A, list_to_ref, S);
item_expr("function", A, [$# | _] = S) ->
    {M, F, Arity} = item_mfa(S),
    {'fun', A, {function, {atom, A, M}, {atom, A, F}, {integer, A, Arity}}}.

item_call(A, Function, S) ->
    {call, A, {remote, A, {atom, A, erlang}, {atom, A, Function}}, [{string, A, S}]}.

%% extended_parse_term(Tokens): {ok, Term} of one expression of literals and items, else an error.
extended_parse_term(Tokens) ->
    case extended_parse_exprs(Tokens) of
        {ok, [Expr]} ->
            try item_term(Expr) of
                Term -> {ok, Term}
            catch
                _:_ -> {error, {erl_anno:location(element(2, Expr)), ?MODULE, "bad term"}}
            end;
        {ok, [_, Expr | _]} ->
            {error, {erl_anno:location(element(2, Expr)), ?MODULE, "bad term"}};
        {error, _} = Error ->
            Error
    end.

%% The term of a literal expression whose leaves may be items.
item_term({call, _, {remote, _, {atom, _, erlang}, {atom, _, F}}, [{string, _, S}]}) when
    F =:= list_to_pid; F =:= list_to_port; F =:= list_to_ref
->
    erlang:F(S);
item_term({cons, _, H, T}) ->
    [item_term(H) | item_term(T)];
item_term({tuple, _, Es}) ->
    list_to_tuple([item_term(E) || E <- Es]);
item_term({map, _, Fields}) ->
    maps:from_list(
        lists:map(fun({map_field_assoc, _, K, V}) -> {item_term(K), item_term(V)} end, Fields)
    );
item_term(Expr) ->
    erl_parse:normalise(Expr).

to_terms(Abstrs) -> [to_term(A) || A <- Abstrs].

to_term(Abstr) -> erl_parse:anno_to_term(Abstr).
