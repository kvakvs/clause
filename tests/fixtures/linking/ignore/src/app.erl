-module(app).
-export([main/1]).
-import(telemetry, [span/2]).

%% legacy is skipped from source_dirs, telemetry is named by ignore_modules, legacy_stats matches its legacy_*
%% pattern and dep_tests is found by search only under an ignored directory; helper and dep load from the search
%% paths as usual.
main(_) ->
    {module_ignored, legacy} = legacy:run(1),
    {module_ignored, telemetry} = span(a, b),
    {module_ignored, telemetry} = telemetry:emit(),
    Run = fun legacy:run/1,
    {module_ignored, legacy} = Run(2),
    {module_ignored, dep_tests} = apply(dep_tests, all, []),
    {module_ignored, legacy_stats} = legacy_stats:count(),
    3 = helper:value() + dep:value(),
    io:format("~p~n", [legacy:run(3)]),
    io:format("ignore: ok~n").
