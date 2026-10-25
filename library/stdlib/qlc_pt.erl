%% Clause's qlc_pt (plan 21 step LIB73, docs/library.md): the qlc parse transform at run time, for
%% qlc:string_to_handle/1,2,3 and erl_eval's qlc:q/1,2. clause_qlc_pt's builtins run OTP's transform in C++
%% (runtime/src/builtins/qlc_pt/) and ask for each match specification attempt, which runs ms_transform here; the
%% builtin then replays its walk with the answers so far. Without a compiler at run time, messages come from
%% erl_lint, as OTP's qlc_pt falls back to when the compiler is not available.
-module(qlc_pt).
-export([parse_transform/2, transform_from_evaluator/2, transform_expression/2]).

parse_transform(Forms, Options) ->
    answered(fun(Answers) -> clause_qlc_pt:parse_transform(Forms, Options, behaviours(Forms), Answers) end, []).

transform_from_evaluator(LC, Bindings) ->
    answered(fun(Answers) -> clause_qlc_pt:transform_expression(LC, Bindings, false, Answers) end, []).

transform_expression(LC, Bindings) ->
    answered(fun(Answers) -> clause_qlc_pt:transform_expression(LC, Bindings, true, Answers) end, []).

%% Run the transform until it has every match specification it asks for (answers newest first). OTP's transform
%% raises the backtrace depth and leaves its file key erased from the process dictionary.
answered(Transform, Answers) ->
    case Transform(Answers) of
        {try_ms, Forms} ->
            answered(Transform, [try_ms(Forms) | Answers]);
        {done, Result, Transformed} ->
            _ = Transformed andalso erlang:system_flag(backtrace_depth, 500),
            _ = erase(qlc_current_file),
            Result
    end.

%% try_ms/4 after the forms are built: {ok, MS, AbstractMS} when ets:fun2ms/1 makes a match specification of the
%% fun, else no.
try_ms(Forms) ->
    X = ms_transform:parse_transform(Forms, []),
    case
        catch begin
            {function, 1, foo, 0, [{clause, 1, [], [], [MS0]}]} = lists:last(X),
            MS = erl_parse:normalise(var2const(MS0)),
            XMS = ets:match_spec_compile(MS),
            true = ets:is_compiled_ms(XMS),
            {ok, MS, MS0}
        end
    of
        {'EXIT', _Reason} -> no;
        Reply -> Reply
    end.

var2const({var, A, V}) -> {atom, A, V};
var2const(T) when is_tuple(T) -> list_to_tuple(var2const(tuple_to_list(T)));
var2const([E | Es]) -> [var2const(E) | var2const(Es)];
var2const(E) -> E.

%% erl_lint.erl's behaviours/1: each behaviour's callback lists for the lint runs over the module.
behaviours(Forms) ->
    Names = lists:usort([B || {attribute, _, Tag, B} <- Forms, Tag =:= behaviour orelse Tag =:= behavior]),
    [{B, behaviour_info(B, callbacks), behaviour_info(B, optional_callbacks)} || B <- Names].

behaviour_info(Behaviour, Kind) ->
    try Behaviour:behaviour_info(Kind) of
        Value -> {ok, Value}
    catch
        _:_ -> error
    end.
