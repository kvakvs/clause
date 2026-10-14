-module(code_module).
-export([main/1]).

%% The code module without a code server (plan step 41Z): loaded-module queries
%% for modules the program has and has not, and argument errors. Answers that
%% name files on OTP (is_loaded/1 and which/1 of non-preloaded modules) differ
%% by design and are checked for shape only.
main([]) ->
    io:format("~w~n", [
        [
            code:ensure_loaded(lists),
            code:ensure_loaded(code_module),
            code:ensure_loaded(no_such_module),
            code:ensure_modules_loaded([lists, code_module]),
            code:ensure_modules_loaded([lists, no_such_module, maps]),
            code:is_loaded(erlang),
            code:is_loaded(no_such_module),
            element(1, code:is_loaded(code_module)),
            code:which(erlang),
            code:which(no_such_module),
            code:module_status(erlang),
            code:module_status(no_such_module),
            code:is_sticky(no_such_module),
            lists:keymember(code_module, 1, code:all_loaded()),
            is_list(code:module_status()),
            outcome(fun() -> code:ensure_loaded(1) end),
            outcome(fun() -> code:is_loaded("lists") end),
            outcome(fun() -> code:which(1) end),
            outcome(fun() -> code:ensure_modules_loaded(x) end)
        ]
    ]),
    io:format("code_module: ok~n").

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
