-module(main).
-export([main/1, unused/0]).

% Export liveness (plan step 62C): what an executable keeps of its modules' exports. worker is reached only
% through literal spawn/apply/fun targets, so its other exports are left out; callbacks escapes as a value, so all
% of its exports stay; hidden is named only through a stack trace, so its unreachable dead/0 raises undef when
% called dynamically (docs/differences.md), unless the executable keeps every export (--keep-exports).

main(_) ->
    Self = self(),
    spawn(worker, run, [Self]),
    receive
        {done, Value} -> io:format("spawn ~p~n", [Value])
    end,
    io:format("apply ~p~n", [apply(worker, double, [21])]),
    Triple = fun worker:triple/1,
    io:format("fun ~p~n", [Triple(3)]),
    io:format("callback ~p~n", [service:start(callbacks)]),
    Hidden = hidden_name(),
    io:format("exported ~p~n", [erlang:function_exported(Hidden, dead, 0)]),
    io:format("dead ~p~n", [call_dead(Hidden)]),
    io:format("exports: ok~n").

% The module of the function on top of a stack trace, without spelling its name.
hidden_name() ->
    try
        hidden:fail()
    catch
        error:_:Stack ->
            [{Module, _, _, _} | _] = Stack,
            Module
    end.

% Call Module:dead() by a runtime module name.
call_dead(Module) ->
    try
        Module:dead()
    catch
        error:undef -> undef
    end.

unused() -> worker:idle().
