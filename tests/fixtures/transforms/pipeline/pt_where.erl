-module(pt_where).
-export([parse_transform/2]).

%% Says where it runs: Clause's transform host links clause_interp, the host Erlang/OTP does not (plan 21 LIB77D).
parse_transform(Forms, _Options) ->
    io:format("pt_where: ~p~n", [erlang:module_loaded(clause_interp)]),
    Forms.
