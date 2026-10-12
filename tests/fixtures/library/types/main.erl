%% Specs name an exported type of every Clause library module that has one (plan step 41K1). The funs in
%% modules/0 bring each module into the batch; every type then resolves to its observed header's declaration.
-module(main).
-export([main/1, describe/1]).

-type all() :: {
    file:filename(),
    io:format(),
    erlang:time_unit(),
    inet:ip_address(),
    maps:iterator_order(),
    gen_tcp:socket(),
    gen_udp:option(),
    os:env_var_name(),
    zlib:zlevel(),
    atomics:atomics_ref(),
    counters:counters_ref()
}.

-spec main([string()]) -> ok.
main(_) ->
    11 = length(modules()),
    io:format("library_types: ok~n").

-spec modules() -> [fun()].
modules() ->
    [
        fun file:read_file/1,
        fun io:fwrite/1,
        fun erlang:convert_time_unit/3,
        fun inet:getaddr/2,
        fun maps:iterator/2,
        fun gen_tcp:close/1,
        fun gen_udp:close/1,
        fun os:getenv/1,
        fun zlib:open/0,
        fun atomics:new/2,
        fun counters:new/2
    ].

-spec describe(all()) -> {file:filename(), io:format(), erlang:time_unit()}.
describe({File, Format, Unit, _, _, _, _, _, _, _, _}) -> {File, Format, Unit}.
