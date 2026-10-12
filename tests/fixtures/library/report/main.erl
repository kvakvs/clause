%% Calls the library ledger report classifies (plan step 41J): an unknown module and an undefined function.
-module(main).
-export([f/0, g/1]).

f() ->
    {ok, File} = clause_report_absent:open("x", [read]),
    clause_report_absent:close(File).

g(X) -> lists:clause_report_absent(X).
