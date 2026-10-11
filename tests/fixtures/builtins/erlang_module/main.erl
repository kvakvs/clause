-module(main).
-export([main/1]).
-compile({no_auto_import, [erase/1]}).

%% The erlang library module (plan step 41B): builtins win over its stubs in every call form, stubs raise undef
%% only when called, OTP's auto-imports reach the module and a local definition shadows an auto-import.
main(_) ->
    M = erlang,
    F = abs,
    3 = M:F(-3),
    4 = apply(M, F, [-4]),
    Abs = fun erlang:abs/1,
    5 = Abs(-5),
    6 = erlang:abs(-6),
    {'EXIT', {undef, [{erlang, check_old_code, 1, _} | _]}} = (catch erlang:check_old_code(a)),
    {'EXIT', {undef, [{erlang, check_old_code, 1, _} | _]}} = (catch check_old_code(a)),
    Stub = fun check_old_code/1,
    {'EXIT', {undef, [{erlang, check_old_code, 1, _} | _]}} = (catch Stub(a)),
    {'EXIT', {undef, [{erlang, check_old_code, 1, _} | _]}} = (catch M:check_old_code(a)),
    {'EXIT', {boom, [{erlang, nif_error, 1, _} | _]}} = (catch erlang:nif_error(boom)),
    {'EXIT', {boom, [{erlang, nif_error, [x], _} | _]}} = (catch erlang:nif_error(boom, [x])),
    {local, c} = erase(c),
    io:format("erlang_module: ok~n").

erase(X) -> {local, X}.
