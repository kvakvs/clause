%% pt_eval replaces the calls with their values while compiling: data, rows, extra and more run in the transform.
-module(table).
-compile({parse_transform, pt_eval}).
-export([size/0]).

size() -> count() + extra() + more().

count() -> pt_eval:term(length(data:items())).

extra() -> pt_eval:reported(extra:value()).

more() -> pt_eval:formed(more:value()).
