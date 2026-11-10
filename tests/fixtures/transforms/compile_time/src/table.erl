%% pt_eval replaces the calls with their values while compiling: data, rows and extra run in the transform, not here.
-module(table).
-compile({parse_transform, pt_eval}).
-export([size/0]).

size() -> count() + extra().

count() -> pt_eval:term(length(data:items())).

extra() -> pt_eval:reported(extra:value()).
