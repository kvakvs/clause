-module(strict).
-export([tag/0, idle/0]).

% Strict liveness (docs/compile.md#strict-liveness): strict makes an atom at run time while main has a dynamic call,
% yet promises its atoms never name called code, so the other modules' unreached exports are still left out. Its own
% name, used as a value, keeps its own exports, as error tuples name a module for format_error/1.
-compile({clause_liveness, strict}).

tag() -> {?MODULE, list_to_atom("made")}.

idle() -> idle.
