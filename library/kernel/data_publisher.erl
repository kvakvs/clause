%% Clause's data_publisher (plan 21 step LIB25, docs/libraries/library.md): OTP 29's behaviour for replicated data stores, as
%% a gen_server registered under its scope. A Clause program is a single node, so a scope has no peers: updates go
%% to the local data and the global view, whose module notifies subscribers, and node events are ignored.
-module(data_publisher).
-behaviour(gen_server).
-export([start/2, start_link/2, start/3, start_link/3, update/2, subscribe/2, unsubscribe/2]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2]).

%% Created for LIB25: the scope and its callback module with their options and version, the module's global view
%% and local data, and the subscriptions by subscription and by monitor reference.
-record(state, {scope, module, options, version, global_view, local_data, subscriptions = #{}, subscribe_refs = #{}}).

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

init({Scope, Module, Options}) ->
    {ok, #state{
        scope = Scope,
        module = Module,
        options = Options,
        version = Module:version(),
        global_view = Module:init_global_view(Scope, Options),
        local_data = Module:init_local_data(Options)
    }}.

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

handle_cast(Message, State) -> handle_info(Message, State).

handle_info({{'DOWN', subscribe}, Ref, process, _, _}, State) ->
    {noreply, remove_subscribe(Ref, State)};
handle_info({Event, _}, State) when Event =:= nodeup; Event =:= nodedown ->
    {noreply, State};
handle_info(Message, #state{module = Module} = State) ->
    Translated =
        erlang:function_exported(Module, translate_message, 2) andalso
            Module:translate_message(Message, State#state.global_view),
    case Translated of
        {drop, View} -> {noreply, State#state{global_view = View}};
        {update, Update, View} -> {noreply, do_update(Update, State#state{global_view = View})};
        _ -> {noreply, State}
    end.

terminate(_, #state{module = Module, global_view = View}) -> Module:stop_global_view(View).

do_update(Update, #state{module = Module} = State) ->
    View = Module:update_global_view_and_notify(node(), Update, State#state.subscriptions, State#state.global_view),
    State#state{global_view = View, local_data = Module:update_local_data(Update, State#state.local_data)}.

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
