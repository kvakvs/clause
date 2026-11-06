%% mnesia's recovery server, registered as mnesia_recover as in OTP (plan 21 step MIM1H3,
%% docs/libraries/mnesia.md#processes), where other nodes' mnesia finds it. Step MIM1H4 gives it the connections between
%% nodes and the exchange of transaction decisions.
-module(clause_mnesia_recover).
-behaviour(gen_server).
-export([start_link/0]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

start_link() ->
    gen_server:start_link({local, mnesia_recover}, ?MODULE, [], [{timeout, infinity}]).

init([]) ->
    {ok, []}.

handle_call(_Request, _From, State) ->
    {reply, badop, State}.

handle_cast(_Request, State) ->
    {noreply, State}.

handle_info(_Message, State) ->
    {noreply, State}.
