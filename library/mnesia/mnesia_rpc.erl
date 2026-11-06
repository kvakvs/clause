%% mnesia's rpc server, registered as mnesia_rpc as in OTP (plan 21 step MIM1H3,
%% docs/libraries/mnesia-protocol.md#remote-queries-mim1h3): other nodes' mnesia sends it {apply, M, F, Args} calls in
%% place of rpc:call/4. A read of a RAM or disc_copies table is answered at once, any other apply by a process of its
%% own; a throw answers its value, a failure {badrpc, {'EXIT', Reason}}.
-module(mnesia_rpc).
-behaviour(gen_server).
-export([start/0, call/4]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2, code_change/3]).

-include("clause_mnesia.hrl").

%% The table reads answered by this server itself when the table is in ets.
-define(IS_LIB_READ(F),
    (F =:= db_get orelse F =:= db_last orelse F =:= db_first orelse F =:= db_next_key orelse
        F =:= db_prev_key)
).

start() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [self()], [{timeout, infinity}]).

% M:F(Args) on Node: through its mnesia_rpc when the nodes agreed on a protocol after {8,4}, else rpc:call/4.
call(Node, M, F, Args) ->
    case gvar({protocol, Node}) of
        {Version, _} when Version > {8, 4} ->
            try
                gen_server:call({?MODULE, Node}, {apply, M, F, Args}, infinity)
            catch
                _:Reason -> {badrpc, {'EXIT', Reason}}
            end;
        _ ->
            rpc:call(Node, M, F, Args)
    end.

init([_Parent]) ->
    {ok, #{}}.

handle_call({apply, mnesia_lib, Func, Args}, From, State) when ?IS_LIB_READ(Func) ->
    apply_lib(Func, Args, From, State);
handle_call({apply, Mod, Func, Args}, From, State) ->
    apply_spawned(Mod, Func, Args, From, State);
handle_call(Msg, _From, State) ->
    unexpected("~p got unexpected call: ~tp~n", Msg),
    {reply, badop, State}.

handle_cast(Msg, State) ->
    unexpected("~p got unexpected cast: ~tp~n", Msg),
    {noreply, State}.

handle_info(Msg, State) ->
    unexpected("~p got unexpected info: ~tp~n", Msg),
    {noreply, State}.

terminate(_Reason, _State) ->
    ok.

code_change(_OldVsn, State, _Extra) ->
    {ok, State}.

% A read of an ets table here at once, of any other table by a process of its own.
apply_lib(Func, [Tab | _] = Args, From, State) ->
    try gvar({Tab, storage_type}) of
        Ram when Ram =:= ram_copies; Ram =:= disc_copies ->
            {reply, apply(mnesia_lib, Func, [Ram | Args]), State};
        _ ->
            apply_spawned(mnesia_lib, Func, Args, From, State)
    catch
        throw:Res -> {reply, Res, State};
        _:Reason -> {reply, {badrpc, {'EXIT', Reason}}, State}
    end.

% A global variable, or undefined when it or mnesia_gvar is missing.
gvar(Name) ->
    try
        ets:lookup_element(?GVAR, Name, 2)
    catch
        error:badarg -> undefined
    end.

apply_spawned(Mod, Func, Args, From, State) ->
    _ = spawn_link(fun() -> gen_server:reply(From, result(Mod, Func, Args)) end),
    {noreply, State}.

result(Mod, Func, Args) ->
    try
        apply(Mod, Func, Args)
    catch
        throw:Res -> Res;
        _:Reason -> {badrpc, {'EXIT', Reason}}
    end.

% An error report through mnesia's event handler, as OTP's.
unexpected(Format, Msg) ->
    clause_mnesia_subscr:report_system_event({mnesia_error, Format, [?MODULE, Msg]}).
