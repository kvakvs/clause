-module(callbacks).
-behaviour(service).
-export([init/0, spare/0]).

% Its name escapes as a value, so every export stays callable.

init() -> started.

spare() -> spare.
