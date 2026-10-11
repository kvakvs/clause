-module(service).
-export([start/1]).

% A behaviour calling its callback module by a runtime name.
-callback init() -> term().

start(Module) -> Module:init().
