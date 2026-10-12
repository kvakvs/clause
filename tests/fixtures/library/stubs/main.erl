-module(main).
-export([main/1]).

%% Every provided library module is discoverable (plan step 41K): one stub of each raises {not_implemented, F, A}
%% with the stub on top of the stack, called directly, through fun M:F/A and by a dynamic call.
main(_) ->
    not_implemented(atomics, info, 1, fun() -> atomics:info(x) end),
    not_implemented(counters, info, 1, fun() -> counters:info(x) end),
    not_implemented(erl_prim_loader, get_path, 0, fun() -> erl_prim_loader:get_path() end),
    not_implemented(erts_code_purger, purge, 1, fun() -> erts_code_purger:purge(m) end),
    not_implemented(erts_internal, cmp_term, 2, fun() -> erts_internal:cmp_term(a, b) end),
    not_implemented(init, get_status, 0, fun() -> init:get_status() end),
    not_implemented(persistent_term, info, 0, fun() -> persistent_term:info() end),
    not_implemented(prim_eval, 'receive', 2, fun() ->
        prim_eval:'receive'(fun(_) -> nomatch end, 0)
    end),
    not_implemented(zlib, open, 0, fun() -> zlib:open() end),
    not_implemented(unicode_util, cp, 1, fun() -> unicode_util:cp("a") end),
    not_implemented(lists, keysort, 2, fun() -> lists:keysort(1, []) end),
    not_implemented(maps, merge, 2, fun() -> maps:merge(#{}, #{}) end),
    not_implemented(io, fwrite, 1, fun() -> io:fwrite("x") end),
    not_implemented(file, consult, 1, fun() -> file:consult("x") end),
    not_implemented(gen_tcp, fdopen, 2, fun() -> gen_tcp:fdopen(0, []) end),
    not_implemented(gen_udp, fdopen, 2, fun() -> gen_udp:fdopen(0, []) end),
    not_implemented(inet, getaddr, 2, fun() -> inet:getaddr("localhost", inet) end),
    not_implemented(os, timestamp, 0, fun() -> os:timestamp() end),
    Info = fun persistent_term:info/0,
    not_implemented(persistent_term, info, 0, Info),
    M = persistent_term,
    not_implemented(persistent_term, info, 0, fun() -> M:info() end),
    not_implemented(persistent_term, info, 0, fun() -> apply(M, info, []) end),
    %% Implemented functions and catalog builtins still run.
    [3, 2, 1] = lists:reverse([1, 2, 3]),
    io:format("library_stubs: ok~n").

not_implemented(M, F, A, Call) ->
    {'EXIT', {{not_implemented, F, A}, [{M, F, A, _} | _]}} = (catch Call()),
    ok.
