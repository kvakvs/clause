-module(socket_ops).
-moduledoc "socket over the loopback and gen_sctp without SCTP (plan 21 LIB62).".
-export([main/1]).

-define(LOOPBACK, #{family => inet, addr => loopback, port => 0}).

main(["extra"]) ->
    % recvmsg, sendmsg, sendfile and datagram truncation (OTP raises notsup on Windows): authored.
    {Listen, Client, Accepted} = pair(),
    show(sendmsg, socket:sendmsg(Client, #{iov => [<<"ab">>, <<"cd">>]})),
    show(recvmsg, socket:recvmsg(Accepted, 4, 0, [], 1000)),
    ok = file:write_file("socket_ops.txt", <<"file data">>),
    {ok, File} = file:open("socket_ops.txt", [read, raw, binary]),
    show(sendfile, socket:sendfile(Client, File, 5, 4)),
    file:close(File),
    show(recv_sendfile, socket:recv(Accepted, 4, 1000)),
    {Receiver, Address} = datagram_receiver(),
    {ok, Sender} = socket:open(inet, dgram),
    ok = socket:sendto(Sender, <<"truncate me">>, Address),
    show(recv_truncated, socket:recv(Receiver, 4, 1000)),
    ok = socket:sendmsg(Sender, #{addr => Address, iov => [<<"a">>, <<"b">>]}),
    {ok, Msg} = socket:recvmsg(Receiver, 1000),
    show(
        recvmsg_dgram,
        lists:sort(maps:to_list(Msg#{addr := maps:remove(port, maps:get(addr, Msg))}))
    ),
    [socket:close(S) || S <- [Listen, Client, Accepted, Receiver, Sender]],
    io:format("socket_ops: ok~n");
main(_) ->
    streams(),
    datagrams(),
    options(),
    registry(),
    sctp(),
    io:format("socket_ops: ok~n").

% A listening socket, a client connected to it and the accepted end.
pair() ->
    {ok, Listen} = socket:open(inet, stream, tcp),
    ok = socket:bind(Listen, ?LOOPBACK),
    ok = socket:listen(Listen),
    {ok, #{port := Port}} = socket:sockname(Listen),
    {ok, Client} = socket:open(inet, stream),
    ok = socket:bind(Client, ?LOOPBACK),
    ok = socket:connect(Client, loopback(Port)),
    {ok, Accepted} = socket:accept(Listen, 1000),
    {Listen, Client, Accepted}.

streams() ->
    {ok, Listen} = socket:open(inet, stream),
    show(socket_term, {element(1, Listen), is_reference(element(2, Listen))}),
    show(info_keys, lists:sort(maps:keys(socket:info(Listen)))),
    show(info_new, states(Listen)),
    show(counters, lists:sort(maps:to_list(maps:get(counters, socket:info(Listen))))),
    show(sockname_unbound, socket:sockname(Listen)),
    show(peername_unbound, socket:peername(Listen)),
    show(bind, socket:bind(Listen, ?LOOPBACK)),
    show(bind_again, socket:bind(Listen, ?LOOPBACK)),
    {ok, #{port := Port} = Name} = socket:sockname(Listen),
    show(sockname, maps:remove(port, Name)),
    show(listen, socket:listen(Listen, 3)),
    show(info_listen, states(Listen)),
    show(accept_timeout, socket:accept(Listen, 50)),
    {ok, Client} = socket:open(inet, stream, tcp),
    ok = socket:bind(Client, ?LOOPBACK),
    show(
        info_client,
        lists:sort(maps:to_list(maps:with([protocol, type, domain], socket:info(Client))))
    ),
    show(connect, socket:connect(Client, loopback(Port))),
    show(info_connected, states(Client)),
    {ok, Accepted} = socket:accept(Listen),
    show(accepted_owner, maps:get(owner, socket:info(Accepted)) =:= self()),
    {ok, Peer} = socket:peername(Client),
    show(peername, maps:remove(port, Peer)),
    show(send, socket:send(Client, <<"hello">>)),
    show(recv_any, socket:recv(Accepted, 0)),
    show(send_iolist, socket:send(Client, [<<"ab">>, "cd", [$e]])),
    show(recv_two, socket:recv(Accepted, 2)),
    show(recv_two_more, socket:recv(Accepted, 2, 1000)),
    show(recv_rest, socket:recv(Accepted, 1, [], 1000)),
    show(recv_timeout, socket:recv(Accepted, 1, 50)),
    show(sendv, socket:sendv(Client, [<<"xy">>, <<"z">>])),
    show(recv_partial, socket:recv(Accepted, 5, 200)),
    show(rest_iov, socket:rest_iov(3, [<<"ab">>, <<"cdef">>])),
    nowait(Client, Accepted),
    nowait_accept(Listen, Port),
    show(shutdown, socket:shutdown(Client, write)),
    show(recv_closed, socket:recv(Accepted, 0, 500)),
    show(close, socket:close(Client)),
    show(close_again, socket:close(Client)),
    show(send_closed, socket:send(Client, <<"x">>)),
    show(info_closed, states(Client)),
    show(listen_dgram, element(1, listen_dgram())),
    [socket:close(S) || S <- [Listen, Accepted]].

% recv with nowait: OTP answers with completion messages on Windows and select messages elsewhere.
nowait(Client, Accepted) ->
    Pending = socket:recv(Accepted, 0, nowait),
    show(
        nowait_kind, lists:member(element(1, element(2, Pending)), [completion_info, select_info])
    ),
    show(nowait_quiet, receive_any(50)),
    ok = socket:send(Client, <<"later">>),
    show(nowait_result, await(Accepted, Pending, fun() -> socket:recv(Accepted, 0, nowait) end)),
    Cancelled = socket:recv(Accepted, 0, nowait),
    show(cancel, socket:cancel(Accepted, element(2, Cancelled))),
    show(cancel_again, socket:cancel(Accepted, element(2, Cancelled))),
    show(cancelled_quiet, receive_any(50)),
    ok = socket:send(Client, <<"after">>),
    show(recv_after_cancel, socket:recv(Accepted, 0, 1000)).

nowait_accept(Listen, Port) ->
    Pending = socket:accept(Listen, nowait),
    {ok, Client} = socket:open(inet, stream),
    ok = socket:bind(Client, ?LOOPBACK),
    ok = socket:connect(Client, loopback(Port)),
    {ok, Accepted} = await(Listen, Pending, fun() -> socket:accept(Listen, nowait) end),
    ok = socket:send(Client, <<"!">>),
    show(nowait_accepted, socket:recv(Accepted, 1, 1000)),
    [socket:close(S) || S <- [Client, Accepted]].

await(Socket, {completion, {completion_info, _, Handle}}, _) ->
    receive
        {'$socket', Socket, completion, {Handle, Result}} -> Result
    after 2000 -> no_completion
    end;
await(Socket, {select, {select_info, _, Handle}}, Retry) ->
    receive
        {'$socket', Socket, select, Handle} -> Retry()
    after 2000 -> no_select
    end.

receive_any(Timeout) ->
    receive
        Message -> {message, Message}
    after Timeout -> none
    end.

listen_dgram() ->
    {ok, S} = socket:open(inet, dgram),
    Result = socket:listen(S),
    socket:close(S),
    Result.

datagram_receiver() ->
    {ok, Receiver} = socket:open(inet, dgram, udp),
    ok = socket:bind(Receiver, ?LOOPBACK),
    {ok, Address} = socket:sockname(Receiver),
    {Receiver, Address}.

datagrams() ->
    {Receiver, Address} = datagram_receiver(),
    show(dgram_states, states(Receiver)),
    {ok, Sender} = socket:open(inet, dgram),
    show(sendto, socket:sendto(Sender, <<"dgram">>, Address)),
    {ok, {Source, Data}} = socket:recvfrom(Receiver, 0, 1000),
    show(recvfrom, {maps:remove(port, Source), Data}),
    show(sender_bound, states(Sender)),
    ok = socket:sendto(Sender, [<<"two">>], Address, []),
    show(recv_dgram, socket:recv(Receiver, 0, 1000)),
    show(recv_dgram_timeout, socket:recv(Receiver, 0, 50)),
    show(connect_dgram, socket:connect(Sender, Address)),
    show(send_connected, socket:send(Sender, <<"three">>)),
    show(recv_connected, socket:recv(Receiver, 0, 1000)),
    show(recvfrom_timeout, socket:recvfrom(Receiver, 0, [], 50)),
    [socket:close(S) || S <- [Receiver, Sender]].

options() ->
    {_, Client, Accepted} = Pair = pair(),
    show(getopt_type, socket:getopt(Client, {socket, type})),
    show(getopt_acceptconn, {
        socket:getopt(element(1, Pair), {socket, acceptconn}),
        socket:getopt(Client, socket, acceptconn)
    }),
    show(setopt_nodelay, socket:setopt(Client, {tcp, nodelay}, true)),
    show(getopt_nodelay, socket:getopt(Client, {tcp, nodelay})),
    show(setopt_reuseaddr, socket:setopt(Client, socket, reuseaddr, true)),
    show(getopt_reuseaddr, socket:getopt(Client, {socket, reuseaddr})),
    show(getopt_keepalive, socket:getopt(Client, {socket, keepalive})),
    show(setopt_linger, socket:setopt(Client, {socket, linger}, #{onoff => true, linger => 3})),
    show(getopt_linger, socket:getopt(Client, {socket, linger})),
    show(getopt_unknown, socket:getopt(Client, {socket, bogus})),
    show(setopt_bad_value, socket:setopt(Client, {socket, reuseaddr}, 7)),
    show(setopt_unknown, socket:setopt(Client, {nowhere, bogus}, 1)),
    show(getopt_owner, socket:getopt(Client, {otp, controlling_process}) =:= {ok, self()}),
    show(getopt_meta, socket:getopt(Client, {otp, meta})),
    show(setopt_meta, socket:setopt(Client, {otp, meta}, hello)),
    show(getopt_meta_set, socket:getopt(Client, {otp, meta})),
    show(ioctl, socket:ioctl(Client, gifconf)),
    Self = self(),
    Other = spawn(fun() ->
        Self ! {other, socket:setopt(Client, {otp, controlling_process}, self())},
        receive
            stop -> ok
        end
    end),
    show(
        not_owner,
        receive
            {other, R} -> R
        end
    ),
    show(give_away, socket:setopt(Client, {otp, controlling_process}, Other)),
    show(new_owner, maps:get(owner, socket:info(Client)) =:= Other),
    Mref = socket:monitor(Accepted),
    Other ! stop,
    show(
        owner_exit_closes,
        receive
            {'DOWN', Mref, socket, Accepted, _} -> unexpected
        after 100 -> socket:recv(Accepted, 0, 1000)
        end
    ),
    show(
        monitor_down,
        receive
            {'DOWN', Mref, Kind, Object, Info} -> {Kind, Object =:= Accepted, Info}
        after 100 -> none
        end
    ),
    [socket:close(S) || S <- tuple_to_list(Pair)].

registry() ->
    Before = socket:number_of(),
    {ok, A} = socket:open(inet, stream, tcp),
    {ok, B} = socket:open(inet6, dgram),
    show(number_of, socket:number_of() - Before),
    show(which_sockets, lists:sort([lists:member(S, socket:which_sockets()) || S <- [A, B]])),
    show(which_tcp, [lists:member(S, socket:which_sockets(tcp)) || S <- [A, B]]),
    show(which_inet6, [lists:member(S, socket:which_sockets(inet6)) || S <- [A, B]]),
    show(which_dgram, [lists:member(S, socket:which_sockets(dgram)) || S <- [A, B]]),
    show(which_owner, [lists:member(S, socket:which_sockets(self())) || S <- [A, B]]),
    show(which_fun, [
        lists:member(S, socket:which_sockets(fun(#{type := T}) -> T =:= stream end))
     || S <- [A, B]
    ]),
    Mref = socket:monitor(B),
    socket:close(B),
    show(
        monitor,
        receive
            {'DOWN', Mref, Kind, Object, Info} -> {Kind, Object =:= B, Info}
        after 500 -> none
        end
    ),
    Closed = socket:monitor(B),
    show(
        monitor_closed,
        receive
            {'DOWN', Closed, Kind2, _, Info2} -> {Kind2, Info2}
        after 500 -> none
        end
    ),
    show(cancel_monitor, socket:cancel_monitor(socket:monitor(A))),
    show(cancel_monitor_unknown, socket:cancel_monitor(make_ref())),
    show(use_registry, socket:use_registry(true)),
    show(
        info_library,
        lists:all(fun(K) -> maps:is_key(K, socket:info()) end, [counters, debug, use_registry])
    ),
    show(supports_keys, [K || {K, _} <- socket:supports()]),
    show(is_supported, [socket:is_supported(K) || K <- [ipv6, sctp, netns]]),
    show(is_supported_option, socket:is_supported(options, {socket, reuseaddr})),
    show(is_supported_protocol, socket:is_supported(protocols, tcp)),
    show(open_mismatch, socket:open(inet, stream, udp)),
    show(open_bad_domain, socket:open(bogus, stream)),
    show(open_bad_type, socket:open(inet, bogus)),
    show(open_bad_protocol, socket:open(inet, stream, bogus)),
    show(info_bad, catch_error(fun() -> socket:info(id(x)) end)),
    show(peeloff, catch_error(fun() -> socket:peeloff(A, 1) end)),
    show(bindx, catch_error(fun() -> socket:bind(A, [?LOOPBACK], add) end)),
    show(open_fd, catch_error(fun() -> socket:open(id(0)) end)),
    socket:close(A).

sctp() ->
    show(sctp_open, [
        gen_sctp:open(),
        gen_sctp:open(0),
        gen_sctp:open([binary]),
        gen_sctp:open(0, [{active, false}])
    ]),
    show(sctp_open_bad, [
        catch_error(fun() -> gen_sctp:open(id(Bad)) end)
     || Bad <- [bad, -1, [bogus]]
    ]),
    show(sctp_error_strings, [gen_sctp:error_string(N) || N <- [0, 1, 5, 13, 99]]),
    show(sctp_error_string_bad, catch_error(fun() -> gen_sctp:error_string(id(x)) end)),
    {ok, U} = gen_udp:open(0),
    ok = gen_udp:close(U),
    show(sctp_closed, [
        gen_sctp:listen(U, true),
        gen_sctp:connect(U, {127, 0, 0, 1}, 1, []),
        gen_sctp:send(U, 1, 0, <<>>),
        gen_sctp:recv(U, 0),
        gen_sctp:peeloff(U, 1)
    ]),
    show(sctp_abort_bad, catch_error(fun() -> gen_sctp:abort(U, id(x)) end)),
    show(sctp_close, gen_sctp:close(U)).

% A socket's read and write states.
states(Socket) ->
    #{rstates := R, wstates := W} = socket:info(Socket),
    {R, W}.

show(Label, Value) -> io:format("~p ~p~n", [Label, Value]).

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(X) -> X.

loopback(Port) -> #{family => inet, addr => loopback, port => Port}.
