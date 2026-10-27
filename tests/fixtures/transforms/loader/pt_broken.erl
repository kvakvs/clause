-module(pt_broken).
-export([parse_transform/2]).

%% Deliberately does not compile: it calls a function it does not define.
parse_transform(Forms, _Options) ->
    undefined_here(Forms).
