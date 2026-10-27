%% Modules run from their forms (plan 21 LIB77B): clause_interp in Clause, compile:forms/2 and code:load_binary/3 in
%% OTP. Calls from compiled code (M:F(...), apply/3, fun M:F/A, spawn, gen_server callbacks), code queries and the
%% interpreted frames of exceptions.
-module(interpreted_modules).
-export([main/1]).

main(_) ->
    Loads = [load(File) || File <- ["interp_a.erl.in", "interp_b.erl.in", "interp_bad.erl.in", "interp_server.erl.in"]],
    show(loads, Loads),
    A = list_to_atom("interp_a"),
    B = list_to_atom("interp_b"),
    Server = list_to_atom("interp_server"),
    calls(A),
    show(remote_interpreted, A:call_b(2)),
    queries(A),
    show(spawn, spawned(A)),
    show(gen_server, served(Server)),
    errors(A, B),
    io:format("interpreted_modules: ok~n").

calls(A) ->
    show(dynamic, A:double(21)),
    show(apply, apply(A, sum, [[1, 2, 3]])),
    show(external_fun, lists:map(fun A:double/1, [1, 2])),
    show(make_fun, (erlang:make_fun(A, double, 1))(5)),
    show(fun_argument, A:apply_twice(fun(X) -> X + 1 end, 1)),
    show(local_fun, A:local_fun([1, 2, 3])),
    show(records, A:records()),
    show(deep, A:deep(100000)),
    show(info, A:info()),
    show(catching, [A:catching({ok, 1}), A:catching(other)]).

queries(A) ->
    show(exported, [
        erlang:function_exported(A, double, 1),
        erlang:function_exported(A, step, 1),
        erlang:function_exported(A, module_info, 0),
        erlang:function_exported(A, module_info, 1)
    ]),
    show(code, [
        code:ensure_loaded(A),
        code:is_loaded(A),
        code:which(A),
        erlang:module_loaded(A),
        code:module_status(A)
    ]),
    show(module_info, [
        A:module_info(module),
        lists:sort(A:module_info(exports)),
        proplists:get_value(attribute_x, A:module_info(attributes))
    ]),
    show(module_info_keys, [Key || {Key, _} <- A:module_info()]).

spawned(A) ->
    Pid = spawn(A, echo, [self()]),
    Pid ! {msg, hello},
    receive
        {echoed, Message} -> Message
    after 5000 -> timeout
    end.

served(Server) ->
    {ok, Pid} = gen_server:start(Server, 10, []),
    ok = gen_server:call(Pid, {add, 5}),
    gen_server:call(Pid, get).

errors(A, B) ->
    show(badmatch, failure(fun() -> A:crash(error) end)),
    show(through_tail_call, failure(fun() -> B:fail(error) end)),
    show(badarg, failure(fun() -> A:element_of(5) end)),
    show(undef, failure(fun() -> A:step(1) end)),
    show(function_clause, failure(fun() -> A:sum(x) end)),
    show(badarith, failure(fun() -> A:double(a) end)),
    show(throw, failure(fun() -> A:throw_it(1) end)).

%% An exception's class, reason and the interpreted modules' frames (Clause shows no frames of failing BIFs).
failure(Fun) ->
    try Fun() of
        Value -> {returned, Value}
    catch
        Class:Reason:Stack ->
            {Class, Reason, [Frame || {M, _, _, _} = Frame <- Stack, interpreted(M)]}
    end.

interpreted(Module) -> lists:prefix("interp_", atom_to_list(Module)).

%% Interpret a module's source, or compile and load it where there is no interpreter.
load(File) ->
    {ok, Forms} = epp:parse_file(File, []),
    case code:ensure_loaded(clause_interp) of
        {module, clause_interp} -> clause_interp:load(Forms);
        {error, _} -> compile_and_load(File, Forms)
    end.

compile_and_load(File, Forms) ->
    case compile:forms(Forms, [return_errors, return_warnings]) of
        {ok, Module, Binary, Warnings} ->
            {module, Module} = code:load_binary(Module, File, Binary),
            {ok, Module, Warnings};
        Error ->
            Error
    end.

show(Tag, Value) -> io:format("~p: ~p~n", [Tag, Value]).
