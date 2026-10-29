%% Clause's qlc_pt (plan 21 step LIB73, docs/libraries/library.md): the qlc parse transform at run time, for
%% qlc:string_to_handle/1,2,3 and erl_eval's qlc:q/1,2. clause_qlc_pt's builtins run OTP's transform in C++
%% (runtime/src/builtins/qlc_pt/), match specification attempts through the C++ ms_transform. Without a compiler at
%% run time, messages come from erl_lint, as OTP's qlc_pt falls back to when the compiler is not available.
-module(qlc_pt).
-export([parse_transform/2, transform_from_evaluator/2, transform_expression/2]).

parse_transform(Forms, Options) ->
    finished(clause_qlc_pt:parse_transform(Forms, Options, behaviours(Forms))).

transform_from_evaluator(LC, Bindings) ->
    finished(clause_qlc_pt:transform_expression(LC, Bindings, false)).

transform_expression(LC, Bindings) ->
    finished(clause_qlc_pt:transform_expression(LC, Bindings, true)).

%% A transform that ran sets the backtrace depth as OTP's transform/2 does; the current file is forgotten.
finished({Result, Transformed}) ->
    _ = Transformed andalso erlang:system_flag(backtrace_depth, 500),
    _ = erase(qlc_current_file),
    Result.

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
