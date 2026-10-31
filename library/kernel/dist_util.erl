%% Clause's dist_util (plan 30 step DIST4b2a, docs/distribution.md#carriers): the distribution handshake of protocol 6
%% that carriers run through #hs_data{}, written from OTP's documentation (erl_dist_protocol's handshake, alt_dist's
%% interface), and the connection supervisor loop that ticks an up connection. The handshake process talks to
%% net_kernel with {self(), {accept_pending, ...}} and {self(), {nodeup, ...}}; net_kernel ticks it with {Kernel, tick}.
-module(dist_util).
-export([
    cancel_timer/1,
    handshake_other_started/1,
    handshake_we_started/1,
    is_allowed/2,
    is_node_name/1,
    net_ticker_spawn_options/0,
    rejectable_flags/0,
    reset_timer/1,
    setup_timer/2,
    shutdown/3,
    shutdown/4,
    split_node/1,
    start_timer/1,
    strict_order_flags/0
]).

-include_lib("kernel/include/dist.hrl").
-include_lib("kernel/include/dist_util.hrl").

% An up connection's ticks: the packets read and written at the last tick, the ticks since input last came, and how
% many such ticks end the connection (net_kernel's tick intensity).
-record(tick, {read = -1, write = -1, quiet = 0, intensity}).

%% The initiator's handshake (setup/5): name, status, the other node's challenge, this node's reply and the ack.
handshake_we_started(#hs_data{request_type = Type, other_node = Node} = HSData0) ->
    HSData1 = offer(HSData0#hs_data{other_started = false}, Type =/= hidden andalso publish(Node)),
    send(HSData1, name_message(HSData1)),
    recv_status(HSData1),
    {Challenge, HSData} = recv_challenge(HSData1),
    Own = challenge(),
    send(HSData, <<$r, Own:32, (digest(Challenge, cookie(Node)))/binary>>),
    recv_challenge_ack(HSData, Own),
    connection(HSData).

%% The acceptor's handshake (accept_connection/5): the other node's name, net_kernel's status, this node's challenge,
%% the reply's digest and the ack.
handshake_other_started(#hs_data{} = HSData0) ->
    HSData1 = recv_name(HSData0#hs_data{other_started = true}),
    Node = HSData1#hs_data.other_node,
    HSData = offer(HSData1, publish(Node)),
    mark_pending(HSData),
    Own = challenge(),
    send(HSData, challenge_message(HSData, Own)),
    Challenge = recv_challenge_reply(HSData, Own),
    send(HSData, <<$a, (digest(Challenge, cookie(Node)))/binary>>),
    connection(HSData).

% Whether this node is published to Node, as net_kernel answers.
publish(Node) -> net_kernel:publish_on_node(Node).

% This node's offer: the runtime's flags with the carrier's additions and without its rejections, and its creation.
offer(#hs_data{add_flags = Add, reject_flags = Reject} = HSData, Published) ->
    {erts_dflags, Default, _, Addable, Rejectable, _} = erts_internal:get_dflags(),
    Flags = (Default bor (optional(Add) band Addable)) band bnot (optional(Reject) band Rejectable),
    Publish =
        case Published of
            true -> ?DFLAG_PUBLISHED;
            false -> 0
        end,
    HSData#hs_data{this_flags = Flags bor Publish, this_creation = erts_internal:get_creation()}.

optional(undefined) -> 0;
optional(Flags) -> Flags.

% The new send_name message of protocol 6.
name_message(#hs_data{this_flags = Flags, this_creation = Creation, this_node = Node}) ->
    Name = atom_to_binary(Node),
    <<$N, Flags:64, Creation:32, (byte_size(Name)):16, Name/binary>>.

challenge_message(#hs_data{this_flags = Flags, this_creation = Creation, this_node = Node}, Challenge) ->
    Name = atom_to_binary(Node),
    <<$N, Flags:64, Challenge:32, Creation:32, (byte_size(Name)):16, Name/binary>>.

send(#hs_data{f_send = Send, socket = Socket, other_node = Node}, Data) ->
    case Send(Socket, Data) of
        ok -> ok;
        _ -> ?shutdown(Node)
    end.

% The next handshake packet as a binary; each one resets the setup timer.
recv(#hs_data{f_recv = Recv, socket = Socket, timer = Timer, other_node = Node}) ->
    case Recv(Socket, 0, infinity) of
        {ok, Data} ->
            reset_timer(Timer),
            iolist_to_binary(Data);
        _ ->
            ?shutdown(Node)
    end.

% The acceptor's status: ok continues; alive means it still holds an earlier connection of this node, which this one
% replaces, since net_kernel only sets up a connection it does not hold.
recv_status(#hs_data{other_node = Node} = HSData) ->
    case recv(HSData) of
        <<"sok">> -> ok;
        <<"sok_simultaneous">> -> ok;
        <<"salive">> -> send(HSData, <<"strue">>);
        Status -> ?shutdown2(Node, {recv_status_failed, Status})
    end.

recv_challenge(#hs_data{other_node = Node} = HSData) ->
    Name = atom_to_binary(Node),
    case recv(HSData) of
        <<$N, Flags:64, Challenge:32, Creation:32, Length:16, Name:Length/binary, _/binary>> ->
            {Challenge, accept_flags(HSData#hs_data{other_creation = Creation}, Flags)};
        _ ->
            ?shutdown2(Node, recv_challenge_failed)
    end.

recv_challenge_ack(#hs_data{other_node = Node} = HSData, Own) ->
    Expected = digest(Own, cookie(Node)),
    case recv(HSData) of
        <<$a, Expected:16/binary>> -> ok;
        _ -> ?shutdown2(Node, {recv_challenge_ack_failed, bad_cookie})
    end.

recv_name(HSData) ->
    case recv(HSData) of
        <<$N, Flags:64, Creation:32, Length:16, Name:Length/binary, _/binary>> ->
            other_node(HSData#hs_data{other_creation = Creation}, unicode:characters_to_list(Name), Flags);
        _ ->
            ?shutdown(no_node)
    end.

% The other node of a received name, with its flags accepted.
other_node(HSData, Name, Flags) ->
    case is_node_name(Name) of
        true ->
            accept_flags(HSData#hs_data{other_node = list_to_atom(Name)}, Flags);
        false ->
            error_logger:error_msg("** Connection attempt from node with illegal name ~ts **~n", [Name]),
            ?shutdown(Name)
    end.

% The other node's flags, the OTP 25 shorthand standing for its flags; a node lacking a mandatory or required flag is
% refused.
accept_flags(#hs_data{require_flags = Require, other_node = Node} = HSData, Flags0) ->
    {erts_dflags, _, Mandatory, _, _, _} = erts_internal:get_dflags(),
    Flags =
        case Flags0 band ?DFLAG_MANDATORY_25_DIGEST of
            0 -> Flags0;
            _ -> Flags0 bor ?MANDATORY_DFLAGS_25
        end,
    case (Mandatory bor optional(Require)) band bnot Flags of
        0 ->
            HSData#hs_data{other_flags = Flags};
        Missing ->
            error_logger:error_msg(
                "** ~w: Connection attempt ~s node ~w rejected since it cannot handle flags 16#~.16B.**~n",
                [node(), direction(HSData), Node, Missing]
            ),
            ?shutdown2(Node, {missing_flags, Missing})
    end.

direction(#hs_data{other_started = true}) -> "from";
direction(_) -> "to".

% Ask net_kernel whether the connection may continue and tell the other node.
mark_pending(#hs_data{kernel_pid = Kernel, other_node = Node, socket = Socket, f_address = Address} = HSData) ->
    Kernel ! {self(), {accept_pending, Node, Address(Socket, Node), type(HSData)}},
    receive
        {Kernel, {accept_pending, ok}} ->
            send(HSData, <<"sok">>);
        {Kernel, {accept_pending, ok_pending}} ->
            send(HSData, <<"sok_simultaneous">>);
        {Kernel, {accept_pending, Status}} ->
            send(HSData, <<"snok">>),
            ?shutdown2(Node, {accept_pending, Status})
    end.

recv_challenge_reply(#hs_data{other_node = Node} = HSData, Own) ->
    Expected = digest(Own, cookie(Node)),
    case recv(HSData) of
        <<$r, Challenge:32, Expected:16/binary>> ->
            Challenge;
        <<$r, _:32, _:16/binary>> ->
            error_logger:error_msg("** Connection attempt from node ~w rejected. Invalid challenge reply. **~n", [Node]),
            ?shutdown2(Node, {recv_challenge_reply_failed, bad_cookie});
        _ ->
            ?shutdown2(Node, recv_challenge_reply_failed)
    end.

% A random 32-bit challenge, and the MD5 digest of a cookie and a challenge as text.
challenge() -> rand:uniform(16#100000000) - 1.

digest(Challenge, Cookie) -> erlang:md5([atom_to_list(Cookie), integer_to_list(Challenge)]).

cookie(Node) -> auth:get_cookie(Node).

% The flags both nodes use, and whether the connection is visible: both nodes published.
flags(#hs_data{this_flags = This, other_flags = Other}) -> This band Other.

type(HSData) ->
    case flags(HSData) band ?DFLAG_PUBLISHED of
        0 -> hidden;
        _ -> normal
    end.

%% The handshake is done: the controller takes the connection up under setnode/3, net_kernel marks it up, and this
%% process supervises it.
connection(#hs_data{other_node = Node, socket = Socket, timer = Timer} = HSData) ->
    cancel_timer(Timer),
    setopts(HSData, HSData#hs_data.f_setopts_pre_nodeup),
    DHandle = setnode(HSData),
    Intensity = mark_nodeup(HSData, (HSData#hs_data.f_address)(Socket, Node)),
    setopts(HSData, HSData#hs_data.f_setopts_post_nodeup),
    handshake_complete(HSData, DHandle),
    con_loop(HSData, #tick{intensity = Intensity}).

setopts(#hs_data{socket = Socket, other_node = Node}, SetOptions) ->
    case SetOptions(Socket) of
        ok -> ok;
        _ -> ?shutdown2(Node, setopts_failed)
    end.

setnode(#hs_data{other_node = Node, socket = Socket, f_getll = GetLl, other_creation = Creation} = HSData) ->
    case GetLl(Socket) of
        {ok, Controller} ->
            try
                erlang:setnode(Node, Controller, {flags(HSData), Creation})
            catch
                error:badarg -> ?shutdown2(Node, setnode_failed)
            end;
        _ ->
            ?shutdown2(Node, getll_failed)
    end.

% Tell net_kernel the connection is up; its answer holds the tick intensity.
mark_nodeup(#hs_data{kernel_pid = Kernel, other_node = Node} = HSData, Address) ->
    Kernel ! {self(), {nodeup, Node, Address, type(HSData)}},
    receive
        {Kernel, {inserted, Intensity}} -> Intensity
    end.

handshake_complete(#hs_data{f_handshake_complete = undefined}, _) ->
    ok;
handshake_complete(#hs_data{f_handshake_complete = Complete, socket = Socket, other_node = Node} = HSData, DHandle) when
    is_function(Complete, 4)
->
    Complete(Socket, Node, DHandle, #{creation => HSData#hs_data.other_creation, flags => flags(HSData)});
handshake_complete(#hs_data{f_handshake_complete = Complete, socket = Socket, other_node = Node}, DHandle) ->
    Complete(Socket, Node, DHandle).

% The connection supervisor: net_kernel's ticks check the connection; anything else is dropped.
con_loop(#hs_data{kernel_pid = Kernel} = HSData, Tick) ->
    receive
        {Kernel, tick} -> con_loop(HSData, tick(HSData, Tick));
        _ -> con_loop(HSData, Tick)
    end.

% A tick: Intensity ticks without input end the connection; a tick without output since the last one sends one.
tick(#hs_data{mf_getstat = GetStat, mf_tick = SendTick, socket = Socket, other_node = Node}, Tick) ->
    case GetStat(Socket) of
        {ok, Read, Write, Pending} ->
            Quiet = quiet(Read, Tick),
            Quiet >= Tick#tick.intensity andalso not_responding(Node),
            case Write =:= Tick#tick.write andalso (Pending =:= 0 orelse Pending =:= false) of
                true -> SendTick(Socket);
                false -> ok
            end,
            Tick#tick{read = Read, write = Write, quiet = Quiet};
        _ ->
            ?shutdown2(Node, get_status_failed)
    end.

quiet(Read, #tick{read = Read, quiet = Quiet}) -> Quiet + 1;
quiet(_, _) -> 0.

not_responding(Node) ->
    error_logger:error_msg("** Node ~p not responding **~n** Removing (timedout) connection **~n", [Node]),
    ?shutdown2(Node, net_tick_timeout).

%% The setup timer: a linked process that ends the handshake process with setup_timer_timeout when no reset comes
%% within Timeout milliseconds.
start_timer(Timeout) -> spawn_link(?MODULE, setup_timer, [self(), Timeout * ?trace_factor]).

setup_timer(Pid, Timeout) ->
    receive
        {Pid, reset} -> setup_timer(Pid, Timeout)
    after Timeout ->
        ?shutdown2({timer, Pid}, setup_timer_timeout)
    end.

reset_timer(Timer) ->
    Timer ! {self(), reset},
    ok.

cancel_timer(Timer) ->
    unlink(Timer),
    exit(Timer, cancel_setup_timer).

shutdown(Module, Line, Data) -> shutdown(Module, Line, Data, shutdown).

shutdown(_Module, _Line, _Data, Reason) -> exit(Reason).

% The spawn options of connection supervisors: linked at max priority, with the kernel parameter
% net_ticker_spawn_options less the options it may not change.
net_ticker_spawn_options() ->
    Options = application:get_env(kernel, net_ticker_spawn_options, []),
    [link, {priority, max} | [Option || Option <- Options, kept_spawn_option(Option)]].

kept_spawn_option(link) -> false;
kept_spawn_option(monitor) -> false;
kept_spawn_option({Name, _}) -> Name =/= priority andalso Name =/= monitor;
kept_spawn_option(_) -> true.

strict_order_flags() -> element(6, erts_internal:get_dflags()).

rejectable_flags() -> element(5, erts_internal:get_dflags()).

% Whether Node is a name part and a host part around one @, both not empty.
is_node_name(Node) ->
    case string:split(listify(Node), "@", all) of
        [[_ | _], [_ | _]] -> true;
        _ -> false
    end.

% A node name, a host name or a name part as {node, Name, Host}, {host, Host} or {name, Name}; empty parts give the
% split list, more than one @ an error.
split_node(Node) ->
    case string:split(listify(Node), "@", all) of
        [[_ | _] = Name, [_ | _] = Host] -> {node, Name, Host};
        [[_ | _] = Name, []] -> {name, Name};
        [[_ | _] = Host] -> {host, Host};
        [_] = Split -> Split;
        [[], _] = Split -> Split;
        _ -> {error, {invalid_node, Node}}
    end.

listify(Atom) when is_atom(Atom) -> atom_to_list(Atom);
listify(Text) -> Text.

% Whether Node matches an entry of Allowed: a node name, a host or a name part; an empty list allows none.
is_allowed(_, []) ->
    false;
is_allowed(Node, [Node | _]) ->
    true;
is_allowed(Node, [Entry | Allowed]) ->
    allowed_by(split_node(Entry), split_node(Node)) orelse is_allowed(Node, Allowed).

allowed_by({node, Name, Host}, {node, Name, Host}) -> true;
allowed_by({host, Host}, {node, _, Host}) -> true;
allowed_by({host, Host}, {host, Host}) -> true;
allowed_by({name, Name}, {node, Name, _}) -> true;
allowed_by({name, Name}, {name, Name}) -> true;
allowed_by(_, _) -> false.
