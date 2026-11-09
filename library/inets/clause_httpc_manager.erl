%% A profile's httpc manager (plan 21 steps MIM6 and MIM4, docs/libraries/inets.md#httpc), registered as OTP's:
%% httpc_manager for the default profile, httpc_<Profile> for others, none when stand-alone. It keeps the profile's
%% options, hands requests to keep-alive sessions (queued behind one in progress) or new handlers, and cancels requests.
-module(clause_httpc_manager).
-behaviour(gen_server).
-export([start_link/3, child_spec/1, name/1, init/1, handle_call/3, handle_cast/2, handle_info/2]).

% The profile's options with their defaults, in OTP's order.
-define(DEFAULTS, [
    {proxy, {undefined, []}},
    {https_proxy, {undefined, []}},
    {pipeline_timeout, 0},
    {max_pipeline_length, 2},
    {max_keep_alive_length, 5},
    {keep_alive_timeout, 120000},
    {max_sessions, 2},
    {cookies, disabled},
    {verbose, false},
    {ipfamily, inet},
    {ip, default},
    {port, default},
    {socket_opts, []},
    {unix_socket, undefined},
    {max_connections_open, infinity}
]).

% The manager of Profile under httpc_profile_sup, or stand-alone; the cookie mode is OTP's argument, unused.
start_link(Profile, _Cookies, stand_alone) ->
    gen_server:start_link(?MODULE, Profile, []);
start_link(Profile, _Cookies, inets) ->
    gen_server:start_link({local, name(Profile)}, ?MODULE, Profile, []).

% The child of httpc_profile_sup serving Profile, as OTP's.
child_spec(Profile) ->
    % The literal fun links into programs the server the child spec names only as data.
    _ = fun ?MODULE:start_link/3,
    #{
        id => child_id(Profile),
        start => {?MODULE, start_link, [Profile, only_session_cookies, inets]},
        shutdown => 4000,
        modules => [httpc_manager]
    }.

child_id(default) -> httpc_manager;
child_id(Profile) -> {httpc, Profile}.

% The registered name of Profile's manager.
name(default) -> httpc_manager;
name(Profile) -> list_to_atom("httpc_" ++ atom_to_list(Profile)).

% State: the profile, its options, the sessions (handlers) with their server, state and number of requests, and the
% handler serving each request.
init(Profile) ->
    process_flag(trap_exit, true),
    {ok, #{profile => Profile, options => ?DEFAULTS, sessions => #{}, requests => #{}}}.

handle_call({request, Request}, _From, State) ->
    Id = make_ref(),
    {reply, {ok, Id}, dispatch(Request#{id => Id}, State)};
handle_call({cancel_request, Id}, _From, #{requests := Requests} = State) ->
    case Requests of
        #{Id := Handler} -> gen_server:cast(Handler, {cancel, Id});
        #{} -> ok
    end,
    {reply, ok, State#{requests := maps:remove(Id, Requests)}};
handle_call({get_options, Names}, _From, #{options := Options} = State) ->
    {reply, [lists:keyfind(Name, 1, Options) || Name <- Names], State};
handle_call(get_options, _From, #{options := Options} = State) ->
    {reply, Options, State};
handle_call(profile, _From, #{profile := Profile} = State) ->
    {reply, Profile, State};
% A handler about to close takes no more requests; those sent to it before this reply it hands back.
handle_call(leaving, {Handler, _}, State) ->
    {reply, ok, update(Handler, fun(Session) -> Session#{state := gone} end, State)}.

handle_cast({set_options, New}, #{options := Options} = State) ->
    Set = lists:foldl(fun({Name, _} = Option, Acc) -> lists:keystore(Name, 1, Acc, Option) end, Options, New),
    {noreply, State#{options := Set}};
% A redirect, or a request handed back by a closing session, sent again under the same request id.
handle_cast({redirect, Request}, State) ->
    {noreply, dispatch(Request, State)};
% A session finished a request; a new session takes requests from then on.
handle_cast({done, Handler}, State) ->
    {noreply, update(Handler, fun done/1, State)}.

done(#{state := gone, queue := Queue} = Session) -> Session#{queue := Queue - 1};
done(#{queue := Queue} = Session) -> Session#{state := available, queue := Queue - 1}.

handle_info({'DOWN', _, process, Handler, _}, #{sessions := Sessions, requests := Requests} = State) ->
    Requests1 = maps:filter(fun(_, Pid) -> Pid =/= Handler end, Requests),
    {noreply, State#{sessions := maps:remove(Handler, Sessions), requests := Requests1}};
handle_info({'EXIT', _, _}, State) ->
    {noreply, State}.

update(Handler, Fun, #{sessions := Sessions} = State) ->
    case Sessions of
        #{Handler := Session} -> State#{sessions := Sessions#{Handler := Fun(Session)}};
        #{} -> State
    end.

% A request to the session of its server with the fewest requests, when it has fewer than max_keep_alive_length,
% else to a new handler, as OTP's; requests closing their connection always get their own.
dispatch(#{id := Id} = Request, #{sessions := Sessions, requests := Requests, options := Options} = State) ->
    Key = key(Request, Options),
    Max = proplists:get_value(max_keep_alive_length, Options),
    Candidates = lists:sort([
        {Queue, Order, Pid}
     || {Pid, #{key := K, state := available, queue := Queue, order := Order}} <- maps:to_list(Sessions),
        K =:= Key,
        Queue < Max,
        not maps:get(close, Request)
    ]),
    case Candidates of
        [{_, _, Handler} | _] ->
            gen_server:cast(Handler, {request, Request}),
            Queued = update(Handler, fun(#{queue := Q} = S) -> S#{queue := Q + 1} end, State),
            Queued#{requests := Requests#{Id => Handler}};
        [] ->
            Handler = start_handler(Request, State),
            Session = #{key => Key, state => new, queue => 1, order => erlang:unique_integer([monotonic])},
            State#{sessions := Sessions#{Handler => Session}, requests := Requests#{Id => Handler}}
    end.

% Sessions are kept by server and by the address connected to, which a proxy may be.
key(#{scheme := Scheme, host := Host, port := Port} = Request, Options) ->
    {Scheme, Host, Port, clause_httpc_handler:address(Request, Options)}.

% A handler under httpc_handler_sup, as OTP's, or linked to a stand-alone manager.
start_handler(Request, #{options := Options}) ->
    % The literal fun links into programs the handler httpc_handler_sup names only as data.
    _ = fun clause_httpc_handler:start_link/3,
    {ok, Handler} =
        case whereis(httpc_handler_sup) of
            undefined -> clause_httpc_handler:start_link(Request, Options, self());
            _ -> supervisor:start_child(httpc_handler_sup, [Request, Options, self()])
        end,
    erlang:monitor(process, Handler),
    Handler.
