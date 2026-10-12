%% A remote type resolves through the batch.
%% with: remote_owner.erl
%% warning: remote.erl:6:1: inferred result contradicts specification for f: declared remote_owner:name(), inferred 1
-module(remote).
-export([f/0]).
-spec f() -> remote_owner:name().
f() -> 1.
