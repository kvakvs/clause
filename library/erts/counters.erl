%% Clause's replacement for OTP's counters (docs/library.md#runtime-module-replacements), generated from
%% counters.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Counters are signed atomics arrays (plan step 41N,
%% docs/builtins.md#atomics-and-counters), tagged with the option they were made with as OTP's are.
-module(counters).
-export([
    add/3,
    get/2,
    info/1,
    new/2,
    put/3,
    sub/3
]).
-include("counters.types.hrl").

add(Ref, Ix, Incr) -> call(Ref, fun(R) -> atomics:add(R, Ix, Incr) end).

get(Ref, Ix) -> call(Ref, fun(R) -> atomics:get(R, Ix) end).

info({atomics, R}) -> call(fun() -> atomics:info(R) end);
info({write_concurrency, R}) -> call(fun() -> erts_internal:counters_info(R) end);
info(_) -> erlang:error(badarg).

new(Size, Options) ->
    Kind =
        case Options of
            [] -> atomics;
            [atomics] -> atomics;
            [write_concurrency] -> write_concurrency;
            _ -> erlang:error(badarg, [Size, Options])
        end,
    try atomics:new(Size, [{signed, true}]) of
        Ref -> {Kind, Ref}
    catch
        error:badarg -> erlang:error(badarg, [Size, Options])
    end.

put(Ref, Ix, Value) -> call(Ref, fun(R) -> atomics:put(R, Ix, Value) end).

sub(Ref, Ix, Decr) when is_integer(Decr) -> add(Ref, Ix, -Decr);
sub(_, _, _) -> erlang:error(badarg).

%% Run an atomics operation on the array of a counters reference; badarg for anything else.
call({Kind, R}, Operation) when Kind =:= atomics; Kind =:= write_concurrency ->
    call(fun() -> Operation(R) end);
call(_, _) ->
    erlang:error(badarg).

%% Raise an atomics badarg as the counters function's own.
call(Operation) ->
    try
        Operation()
    catch
        error:badarg -> erlang:error(badarg)
    end.
