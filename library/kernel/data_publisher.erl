%% Clause's data_publisher (plan 21 step LIB25, plan 30 step DIST9, docs/libraries/library.md): OTP 29's behaviour for
%% replicated data stores, as a gen_server registered under its scope. Scope servers of the same name on connected
%% nodes discover each other at start and nodeup, send each other their local data, relay updates as diffs, and a
%% peer's end (noconnection with its node) removes its data, as OTP's protocol on the wire. Peers of version 0 (OTP
%% before 29) get the callback module's plain messages.
-module(data_publisher).
-behaviour(gen_server).
-export([start/2, start_link/2, start/3, start_link/3, update/2, subscribe/2, unsubscribe/2]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2]).

%% Created for LIB25: the scope and its callback module with their options and version, the module's global view
%% and local data, the peers' monitors, versions and local data (DIST9), and the subscriptions by subscription and by
%% monitor reference.
-record(state, {
    scope,
    module,
    options,
    version,
    global_view,
    local_data,
    peers = #{},
    subscriptions = #{},
    subscribe_refs = #{}
}).

-callback version() -> term().
-callback init_global_view(atom(), map()) -> term().
-callback init_local_data(map()) -> term().
-callback stop_global_view(term()) -> term().
-callback update_local_data(term(), term()) -> term().
-callback update_global_view_and_notify(node(), term(), map(), term()) -> term().
-callback data_diff(Old :: term(), New :: term()) -> term().
-callback new_subscription(term(), term()) -> term().
-callback translate_update(MyVersion :: term(), PeerVersion :: term(), term()) -> term().
-callback translate_local_data(MyVersion :: term(), PeerVersion :: term(), term()) -> term().
-callback translate_message(term(), term()) -> {update, term(), term()} | {drop, term()} | tuple().
-optional_callbacks([translate_message/2]).

start(Scope, Module) -> start(Scope, Module, #{}).

start_link(Scope, Module) -> start_link(Scope, Module, #{}).

start(Scope, Module, Options) -> gen_server:start({local, Scope}, ?MODULE, {Scope, Module, Options}, []).

start_link(Scope, Module, Options) -> gen_server:start_link({local, Scope}, ?MODULE, {Scope, Module, Options}, []).

update(Scope, Update) -> gen_server:call(Scope, {update, Update}, infinity).

subscribe(Scope, Subscription) -> gen_server:call(Scope, {subscribe, Subscription}, infinity).

unsubscribe(Scope, Ref) -> gen_server:call(Scope, {unsubscribe, Ref}, infinity).

%% A scope that is not standalone watches nodes and asks the scope of every connected node to discover it.
init({Scope, Module, Options}) ->
    Standalone = maps:get(standalone, Options, false),
    Standalone orelse (ok = net_kernel:monitor_nodes(true)),
    State = #state{
        scope = Scope,
        module = Module,
        options = Options,
        version = Module:version(),
        global_view = Module:init_global_view(Scope, Options),
        local_data = Module:init_local_data(Options)
    },
    _ = Standalone orelse [do_send({Scope, Node}, {discover, self(), State#state.version}) || Node <- nodes()],
    {ok, State}.

handle_call({update, Update}, _, State) ->
    {reply, ok, do_update(Update, State)};
handle_call({subscribe, Subscription}, {Pid, _}, #state{module = Module, subscriptions = Subscriptions} = State) ->
    Ref = erlang:monitor(process, Pid, [{tag, {'DOWN', subscribe}}]),
    Result = Module:new_subscription(Subscription, State#state.global_view),
    Subscribers = maps:get(Subscription, Subscriptions, #{}),
    {reply, {Ref, Result}, State#state{
        subscriptions = Subscriptions#{Subscription => Subscribers#{Ref => Pid}},
        subscribe_refs = (State#state.subscribe_refs)#{Ref => Subscription}
    }};
handle_call({unsubscribe, Ref}, _, State) ->
    {reply, ok, remove_subscribe(Ref, State)}.

%% Peers cast their local data.
handle_cast(Message, State) -> handle_info(Message, State).

handle_info({discover, Peer, _}, #state{options = #{standalone := true}} = State) when is_pid(Peer) ->
    {noreply, State};
handle_info({discover, Peer, Version}, State) ->
    {noreply, discovered(Peer, Version, State)};
handle_info({local_data, Peer, Version, Local}, State) ->
    {noreply, peer_data(Peer, Version, Local, State)};
handle_info({update, Peer, Update}, State) ->
    {noreply, peer_update(Peer, Update, State)};
handle_info({nodeup, Node}, State) when Node =:= node() ->
    {noreply, State};
handle_info({nodeup, Node}, State) ->
    _ = do_send({State#state.scope, Node}, {discover, self(), State#state.version}),
    {noreply, State};
handle_info({nodedown, _}, State) ->
    {noreply, State};
handle_info({{'DOWN', subscribe}, Ref, process, _, _}, State) ->
    {noreply, remove_subscribe(Ref, State)};
handle_info({{'DOWN', peer}, Ref, process, Peer, _}, State) ->
    {noreply, peer_down(Peer, Ref, State)};
handle_info(Message, #state{module = Module} = State) ->
    Translated =
        erlang:function_exported(Module, translate_message, 2) andalso
            Module:translate_message(Message, State#state.global_view),
    translated(Translated, State).

terminate(_, #state{module = Module, global_view = View}) -> Module:stop_global_view(View).

%% A callback module's translation of another message: an update, or one of the legacy peer messages.
translated({drop, View}, State) ->
    {noreply, State#state{global_view = View}};
translated({update, Update, View}, State) ->
    {noreply, do_update(Update, State#state{global_view = View})};
translated({update, Peer, Update, View}, State) ->
    handle_info({update, Peer, Update}, State#state{global_view = View});
translated({local_data, Peer, Version, Local, View}, State) ->
    handle_info({local_data, Peer, Version, Local}, State#state{global_view = View});
translated({discover, Peer, Version, View}, State) ->
    handle_info({discover, Peer, Version}, State#state{global_view = View});
translated(_, State) ->
    {noreply, State}.

%% A peer asked for this scope's local data: send it, and discover the peer back the first time (DIST9).
discovered(Peer, Version, #state{module = Module} = State) ->
    gen_server:cast(Peer, translate_local_data(State#state.local_data, Version, State)),
    case State#state.peers of
        #{Peer := _} ->
            State;
        Peers ->
            Ref = erlang:monitor(process, Peer, [{tag, {'DOWN', peer}}]),
            _ = do_send(Peer, {discover, self(), State#state.version}),
            State#state{peers = Peers#{Peer => {Ref, Version, Module:init_local_data(State#state.options)}}}
    end.

%% A peer's whole local data: the difference to what it had before goes to the global view (DIST9).
peer_data(Peer, Version, Local, #state{module = Module, peers = Peers} = State) ->
    {Ref, Old} =
        case Peers of
            #{Peer := {Known, _, Previous}} -> {Known, Previous};
            _ -> {erlang:monitor(process, Peer, [{tag, {'DOWN', peer}}]), Module:init_local_data(State#state.options)}
        end,
    View = notify(node(Peer), Module:data_diff(Old, Local), State),
    State#state{global_view = View, peers = Peers#{Peer => {Ref, Version, Local}}}.

%% A known peer's update; an unknown peer's is dropped (DIST9).
peer_update(Peer, Update, #state{module = Module, peers = Peers} = State) ->
    case Peers of
        #{Peer := {Ref, Version, Local}} ->
            View = notify(node(Peer), Update, State),
            State#state{
                global_view = View, peers = Peers#{Peer => {Ref, Version, Module:update_local_data(Update, Local)}}
            };
        _ ->
            State
    end.

%% An ended or disconnected peer takes its local data out of the global view (DIST9).
peer_down(Peer, Ref, #state{module = Module} = State) ->
    case maps:take(Peer, State#state.peers) of
        {{Ref, _, Local}, Peers} ->
            View = notify(node(Peer), Module:data_diff(Local, Module:init_local_data(State#state.options)), State),
            State#state{global_view = View, peers = Peers};
        _ ->
            State
    end.

notify(Node, Update, #state{module = Module} = State) ->
    Module:update_global_view_and_notify(Node, Update, State#state.subscriptions, State#state.global_view).

%% An update in the peer's version, or the legacy plain messages of a version 0 peer.
translate_update(Update, Version, #state{module = Module} = State) ->
    case Module:translate_update(State#state.version, Version, Update) of
        {'$plain_messages', Messages} -> Messages;
        Translated -> {update, self(), Translated}
    end.

translate_local_data(Local, Version, #state{module = Module} = State) ->
    case Module:translate_local_data(State#state.version, Version, Local) of
        {'$plain_message', Message} -> Message;
        Translated -> {local_data, self(), Version, Translated}
    end.

%% Peer messages never set up connections (OTP sends without nosuspend too, so no update is lost).
do_send(Dest, Messages) when is_list(Messages) -> [do_send(Dest, Message) || Message <- Messages];
do_send(Dest, Message) -> erlang:send(Dest, Message, [noconnect]).

do_update(Update, #state{module = Module} = State) ->
    View = notify(node(), Update, State),
    Local = Module:update_local_data(Update, State#state.local_data),
    _ = [do_send(Peer, translate_update(Update, Version, State)) || Peer := {_, Version, _} <- State#state.peers],
    State#state{global_view = View, local_data = Local}.

remove_subscribe(Ref, #state{subscriptions = Subscriptions} = State) ->
    case maps:take(Ref, State#state.subscribe_refs) of
        {Subscription, Refs} ->
            erlang:demonitor(Ref),
            Remaining = maps:remove(Ref, maps:get(Subscription, Subscriptions)),
            Kept =
                case map_size(Remaining) of
                    0 -> maps:remove(Subscription, Subscriptions);
                    _ -> Subscriptions#{Subscription => Remaining}
                end,
            State#state{subscriptions = Kept, subscribe_refs = Refs};
        error ->
            State
    end.
