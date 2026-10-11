-module(hidden).
-export([fail/0, dead/0]).

% fail/0 is called directly; nothing names dead/0, so an executable leaves it out.

fail() -> erlang:error(boom).

dead() -> dead.
