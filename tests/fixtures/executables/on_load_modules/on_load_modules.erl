%% -on_load functions (plan 21 step MIM9d): each has run, in a process of its own, before its module's functions are
%% called; it needs no export, and module_info(attributes) leaves the attribute out. Clause runs them at startup, as an
%% OTP node booted in embedded mode does, and OTP's interactive mode at a module's first call: the golden prints only
%% what both show. The authored runs make ol_fail's function fail: the program halts as an OTP node whose kernel
%% application cannot start.
-module(on_load_modules).
-export([main/1]).

main(_) ->
    show([ol_counter:next(), ol_counter:next()]),
    {_, Loader} = persistent_term:get(ol_counter),
    show([Loader =/= self(), is_process_alive(Loader)]),
    show([erlang:function_exported(ol_counter, load, 0), erlang:function_exported(ol_keys, known, 0)]),
    show(ol_counter:module_info(attributes)),
    show(ol_keys:keys()),
    show(ol_fail:ready()),
    io:format("on_load_modules: ok~n").

show(Term) -> io:format("~p~n", [Term]).
