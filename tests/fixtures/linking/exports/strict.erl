-module(strict).
-export([tag/0, idle/0]).

% Strict liveness (docs/compile.md#strict-liveness): strict names itself as a value and makes an atom at run time
% while main has a dynamic call, yet promises its atoms never name called code, so idle/0 and module_info/0,1 are
% still left out.
-compile({clause_liveness, strict}).

tag() -> {?MODULE, list_to_atom("made")}.

idle() -> idle.
