-module(lists).

%% Deliberately does not compile: lists is a sticky host module, so the loader must not compile this source.
broken() -> undefined_here().
