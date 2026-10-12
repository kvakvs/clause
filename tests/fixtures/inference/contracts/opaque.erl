%% Inside its module an opaque or nominal type is read by its definition.
%% warning: opaque.erl:8:1: inferred result contradicts specification for f: declared opaque:secret(), inferred ok
%% warning: opaque.erl:11:1: inferred result contradicts specification for g: declared opaque:name(), inferred 1
-module(opaque).
-export([f/0, g/0]).
-export_type([secret/0, name/0]).
-opaque secret() :: integer().
-spec f() -> secret().
f() -> ok.
-nominal name() :: atom().
-spec g() -> name().
g() -> 1.
