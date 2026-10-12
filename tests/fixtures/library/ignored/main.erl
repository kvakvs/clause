%% Names a type of a module the project ignores (plan step 41K2): a warning, and the program still builds.
-module(main).
-export([main/1]).

-spec main([string()]) -> ok.
main(_) ->
    ok = check(helper:value()),
    io:format("library_ignored: ok~n").

-spec check(helper:result() | {module_ignored, helper}) -> ok.
check({module_ignored, helper}) -> ok.
