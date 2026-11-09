%% One httpc connection (plan 21 step MIM4, docs/libraries/inets.md#httpc): it connects over gen_tcp or ssl, sends
%% the requests its manager queues on it one at a time, parses responses, has redirects sent, delivers results to
%% receivers and stays a keep-alive session until the server closes or the keep-alive time passes.
-module(clause_httpc_handler).
-behaviour(gen_server).
-export([start_link/3, address/2, init/1, handle_continue/2, handle_call/3, handle_cast/2, handle_info/2]).

% Redirects followed for one request before the last redirect response is the result.
-define(MAX_REDIRECTS, 5).

start_link(Request, Options, Manager) ->
    gen_server:start_link(?MODULE, {Request, Options, Manager}, []).

% State: the manager, the profile's options, the connection, the request in progress with its parser and timer, the
% requests queued behind it, whether the connection served an earlier request (a reused session is retried once
% when the server closed it).
init({Request, Options, Manager}) ->
    State = #{
        manager => Manager,
        options => Options,
        transport => undefined,
        socket => undefined,
        request => Request,
        queue => [],
        parser => undefined,
        timer => undefined,
        idle_timer => undefined,
        reused => false,
        received => false
    },
    {ok, State, {continue, connect}}.

handle_continue(connect, #{request := Request} = State) ->
    case connect(Request, State) of
        {ok, Connected} ->
            send(Connected);
        {error, Reason} ->
            deliver(Request, {error, Reason}),
            leave(State)
    end.

handle_call(_, _From, State) ->
    {reply, ok, State}.

% A request from the manager: sent at once when idle, else queued behind the one in progress.
handle_cast({request, Request}, #{request := undefined} = State) ->
    send(cancel_idle(State#{request := Request, reused := true}));
handle_cast({request, Request}, #{queue := Queue} = State) ->
    {noreply, State#{queue := Queue ++ [Request]}};
handle_cast({cancel, Id}, #{request := #{id := Id}} = State) ->
    leave(State);
handle_cast({cancel, Id}, #{queue := Queue, manager := Manager} = State) ->
    case [R || #{id := I} = R <- Queue, I =/= Id] of
        Queue ->
            {noreply, State};
        Kept ->
            gen_server:cast(Manager, {done, self()}),
            {noreply, State#{queue := Kept}}
    end.

handle_info({Tag, Socket, Data}, #{socket := Socket, request := #{method := Method}, parser := Parser} = State) when
    Tag =:= tcp; Tag =:= ssl
->
    case clause_httpc_message:parse(Data, Method, Parser) of
        {more, Parser1} -> {noreply, active(State#{parser := Parser1, received := true})};
        {response, Response, _Rest} -> response(Response, State);
        {error, Reason} -> fail(Reason, State)
    end;
handle_info({Tag, Socket}, #{socket := Socket} = State) when Tag =:= tcp_closed; Tag =:= ssl_closed ->
    closed(State);
handle_info({Tag, Socket, _Reason}, #{socket := Socket} = State) when Tag =:= tcp_error; Tag =:= ssl_error ->
    closed(State);
handle_info({timeout, Timer, request}, #{timer := Timer} = State) ->
    fail(timeout, State);
handle_info({timeout, Timer, detached}, #{idle_timer := Timer} = State) ->
    {stop, normal, close(State)};
handle_info({timeout, Timer, idle}, #{idle_timer := Timer} = State) ->
    leave(State);
handle_info(_, State) ->
    {noreply, State}.

% Close and stop: the manager first stops sending requests, and those it sent or queued here go back to it.
leave(State) ->
    {stop, normal, close(release(State))}.

release(#{manager := Manager, queue := Queue} = State) ->
    ok = gen_server:call(Manager, leaving, infinity),
    [gen_server:cast(Manager, {redirect, Request}) || Request <- Queue ++ sent()],
    State#{queue := []}.

sent() ->
    receive
        {'$gen_cast', {request, Request}} -> [Request | sent()]
    after 0 -> []
    end.

%% --- Connections ---

% A connection to the request's server, or to the proxy carrying it.
connect(#{scheme := Scheme} = Request, #{options := Options} = State) ->
    {Host, Port, Proxy} = address(Request, Options),
    Family = family(Options),
    Timeout = maps:get(connect_timeout, Request),
    Base = [binary, {packet, 0}, {active, false}],
    case open(Scheme, Host, Port, Base ++ Family ++ socket_options(Request, Options), Request, Timeout) of
        {ok, Transport, Socket} ->
            {ok, State#{transport := Transport, socket := Socket, request := Request#{proxy => Proxy}}};
        {error, Reason} ->
            {error, {failed_connect, [{to_address, {Host, Port}}, {inet, Family, Reason}]}}
    end.

open(http, Host, Port, Options, _Request, Timeout) ->
    opened(gen_tcp, gen_tcp:connect(Host, Port, Options, Timeout));
open(https, Host, Port, Options, #{ssl := undefined}, Timeout) ->
    opened(ssl, ssl:connect(Host, Port, Options ++ httpc:ssl_verify_host_options(true), Timeout));
open(https, Host, Port, Options, #{ssl := Ssl}, Timeout) ->
    opened(ssl, ssl:connect(Host, Port, Options ++ Ssl, Timeout)).

opened(Transport, {ok, Socket}) -> {ok, Transport, Socket};
opened(_, {error, _} = Error) -> Error.

% The address to connect to: the http proxy unless the host is one it leaves out (sessions are kept by it too).
address(#{scheme := http, host := Host, port := Port}, Options) ->
    case proplists:get_value(proxy, Options) of
        {{ProxyHost, ProxyPort}, NoProxy} ->
            case lists:any(fun(Pattern) -> no_proxy(Pattern, Host) end, NoProxy) of
                true -> {Host, Port, false};
                false -> {ProxyHost, ProxyPort, true}
            end;
        _ ->
            {Host, Port, false}
    end;
address(#{host := Host, port := Port}, _Options) ->
    {Host, Port, false}.

no_proxy("*" ++ Suffix, Host) -> lists:suffix(Suffix, Host);
no_proxy(Pattern, Host) -> Pattern =:= Host.

% The address family and local address options, as OTP lists them in connection errors.
family(Options) ->
    Family =
        case proplists:get_value(ipfamily, Options) of
            inet6 -> inet6;
            _ -> inet
        end,
    [Family] ++ [{ip, IP} || {ip, IP} <- Options, IP =/= default] ++
        [{port, P} || {port, P} <- Options, P =/= default].

socket_options(#{socket_opts := undefined}, Options) -> proplists:get_value(socket_opts, Options);
socket_options(#{socket_opts := Own}, _Options) -> Own.

close(#{socket := undefined} = State) ->
    State;
close(#{transport := Transport, socket := Socket} = State) ->
    _ = Transport:close(Socket),
    State#{socket := undefined}.

active(#{transport := gen_tcp, socket := Socket} = State) ->
    ok = inet:setopts(Socket, [{active, once}]),
    State;
active(#{transport := ssl, socket := Socket} = State) ->
    ok = ssl:setopts(Socket, [{active, once}]),
    State.

%% --- Requests ---

% Send the request in progress and wait for its response within its timeout.
send(#{transport := Transport, socket := Socket, request := Request} = State) ->
    Sending = State#{parser := clause_httpc_message:new_parser(), received := false},
    case Transport:send(Socket, clause_httpc_message:request(Request)) of
        ok ->
            ok = send_fun_body(Transport, Socket, Request),
            {noreply, active(Sending#{timer := timer(Request)})};
        {error, _} ->
            closed(Sending)
    end.

% A body produced by a fun: each part as it comes, in chunks when asked for.
send_fun_body(Transport, Socket, #{body := {Fun, Acc}}) ->
    fun_body(Transport, Socket, Fun, Acc, fun(Data) -> Data end);
send_fun_body(Transport, Socket, #{body := {chunkify, Fun, Acc}}) ->
    ok = fun_body(Transport, Socket, Fun, Acc, fun chunk/1),
    Transport:send(Socket, "0\r\n\r\n");
send_fun_body(_, _, _) ->
    ok.

fun_body(Transport, Socket, Fun, Acc, Wrap) ->
    case Fun(Acc) of
        {ok, Data, Acc1} ->
            ok = Transport:send(Socket, Wrap(Data)),
            fun_body(Transport, Socket, Fun, Acc1, Wrap);
        eof ->
            ok
    end.

chunk(Data) ->
    [integer_to_list(iolist_size(Data), 16), "\r\n", Data, "\r\n"].

timer(#{timeout := infinity}) -> undefined;
timer(#{timeout := Timeout}) -> erlang:start_timer(Timeout, self(), request).

cancel_timer(#{timer := undefined} = State) ->
    State;
cancel_timer(#{timer := Timer} = State) ->
    _ = erlang:cancel_timer(Timer),
    State#{timer := undefined}.

cancel_idle(#{idle_timer := undefined} = State) ->
    State;
cancel_idle(#{idle_timer := Timer} = State) ->
    _ = erlang:cancel_timer(Timer),
    State#{idle_timer := undefined}.

% The server closed the connection: the end of a body read until the close, a reused session's request sent again
% on a new connection, or the request's failure.
closed(#{request := undefined} = State) ->
    leave(State#{socket := undefined});
closed(#{request := #{method := Method} = Request, parser := Parser, reused := Reused, received := Received} = State) ->
    case clause_httpc_message:finish(Parser, Method) of
        {ok, Response} ->
            response(Response, State#{socket := undefined});
        error when Reused, not Received ->
            retry(Request, cancel_timer(State#{socket := undefined, reused := false}));
        error ->
            fail(socket_closed_remotely, State#{socket := undefined})
    end.

retry(Request, State) ->
    case connect(Request, State) of
        {ok, Connected} ->
            send(Connected);
        {error, Reason} ->
            deliver(Request, {error, Reason}),
            leave(State)
    end.

fail(Reason, #{request := Request} = State) ->
    deliver(Request, {error, Reason}),
    leave(cancel_timer(State)).

% A complete response: a redirect to send, or the request's result; then the session closes, goes on with its queue
% or waits idle.
response(Response, #{request := Request} = State0) ->
    State = cancel_timer(State0),
    case redirect(Request, Response) of
        {ok, Next} ->
            forward(Next, Response, State);
        none ->
            deliver(Request, {ok, Response}),
            after_response(ending(State, Response), State)
    end.

% A redirect goes through the manager, which may queue it here; a connection that served only this request is then
% left unused, as OTP's.
forward(Next, Response, #{manager := Manager, reused := Reused} = State) ->
    gen_server:cast(Manager, {redirect, maps:remove(proxy, Next)}),
    case {ending(State, Response), Reused} of
        {keep, false} -> after_response(detach, State);
        {Ending, _} -> after_response(Ending, State)
    end.

% After a response the connection closes, takes its next request, or is left unused until its keep-alive time passes
% (OTP's sessions after a close asked by the user or a redirect from a new connection).
after_response(close, State) ->
    leave(State);
after_response(keep, #{manager := Manager} = State) ->
    gen_server:cast(Manager, {done, self()}),
    next(State#{request := undefined});
after_response(detach, State) ->
    Released = release(State),
    {noreply, active(Released#{request := undefined, idle_timer := idle_timer(State, detached)})}.

next(#{queue := [Request | Queue]} = State) ->
    send(State#{request := Request, queue := Queue, reused := true});
next(State) ->
    {noreply, active(State#{idle_timer := idle_timer(State, idle)})}.

idle_timer(#{options := Options}, Tag) ->
    case proplists:get_value(keep_alive_timeout, Options) of
        infinity -> undefined;
        Timeout -> erlang:start_timer(Timeout, self(), Tag)
    end.

% How the connection goes on after this response: closed by either side, a server error, no keep-alive or a
% request of its own close it; a connection: close header of the user's leaves it unused.
ending(#{socket := undefined}, _) ->
    close;
ending(#{request := Request}, #{status := {Version, Code, _}, fields := Fields}) ->
    Connection = proplists:get_value("connection", Fields, ""),
    Closes =
        Code >= 500 orelse connection_close(Connection) orelse maps:get(close, Request) orelse
            (Version =:= "HTTP/1.0" andalso not keep_alive(Connection)),
    case {Closes, user_closes(Request)} of
        {true, _} -> close;
        {false, true} -> detach;
        {false, false} -> keep
    end.

user_closes(#{headers_as_is := true}) -> false;
user_closes(#{headers := Headers}) -> connection_close(header("connection", Headers)).

header(Name, Headers) ->
    case lists:keyfind(Name, 1, lists:reverse(Headers)) of
        {_, Value} -> Value;
        false -> ""
    end.

connection_close(Value) -> string:find(string:lowercase(Value), "close") =/= nomatch.
keep_alive(Value) -> string:find(string:lowercase(Value), "keep-alive") =/= nomatch.

%% --- Redirects ---

% The request a redirect response asks for: OTP's methods per status, at most five times, to a Location resolved
% against the request's URI.
redirect(#{autoredirect := false}, _) ->
    none;
redirect(#{redirects := Count}, _) when Count >= ?MAX_REDIRECTS ->
    none;
redirect(#{method := Method} = Request, #{status := {_, Code, _}, fields := Fields}) ->
    case {redirect_method(Code, Method), lists:keyfind("location", 1, Fields)} of
        {none, _} -> none;
        {_, false} -> none;
        {Next, {_, Location}} -> target(Request, Next, Location)
    end.

redirect_method(300, Method) -> Method;
redirect_method(Code, post) when Code >= 301, Code =< 303 -> {get, empty};
redirect_method(Code, Method) when Code >= 301, Code =< 303 -> same(Method, [get, head, options]);
redirect_method(Code, Method) when Code =:= 307; Code =:= 308 -> same(Method, [get, head, post, options]);
redirect_method(_, _) -> none.

same(Method, Methods) ->
    case lists:member(Method, Methods) of
        true -> Method;
        false -> none
    end.

target(Request, Next, Location) ->
    case uri_string:normalize(uri_string:resolve(Location, uri(Request)), [return_map]) of
        #{scheme := Scheme} = Uri when Scheme =:= "http"; Scheme =:= "https" ->
            {ok, redirected(Next, Request#{redirects := maps:get(redirects, Request) + 1}, Uri)};
        _ ->
            none
    end.

redirected({Method, empty}, Request, Uri) ->
    redirected(Method, Request#{content_type := undefined, content_length := "0", body := []}, Uri);
redirected(Method, Request, Uri) ->
    clause_httpc_request:at(Uri, Request#{method := Method}).

uri(#{scheme := Scheme, path := Path, query := Query} = Request) ->
    Q =
        case Query of
            [] -> [];
            _ -> "?" ++ Query
        end,
    lists:flatten([atom_to_list(Scheme), "://", clause_httpc_message:host_header(Request), Path, Q]).

%% --- Results ---

% Deliver a request's result to its receiver in OTP's forms: a status line, headers and body, or the status code
% and body, in the body format asked for; streams to self get a start, the body and an end.
deliver(#{stream := self, sync := false} = Request, {ok, #{status := {_, Code, _}} = Response}) when
    Code =:= 200; Code =:= 206
->
    send_reply(Request, {stream_start, clause_httpc_message:result_headers(maps:get(received_fields, Response))}),
    send_reply(Request, {stream, maps:get(body, Response)}),
    send_reply(Request, {stream_end, clause_httpc_message:result_headers(maps:get(fields, Response))});
deliver(Request, {ok, Response}) ->
    send_reply(Request, result(Request, Response));
deliver(Request, {error, _} = Error) ->
    send_reply(Request, Error).

result(#{full_result := false} = Request, #{status := {_, Code, _}, body := Body}) ->
    {Code, body(Request, Body)};
result(Request, #{status := Status, fields := Fields, body := Body}) ->
    {Status, clause_httpc_message:result_headers(Fields), body(Request, Body)}.

body(#{body_format := binary}, Body) -> Body;
body(#{body_format := string}, Body) -> binary_to_list(Body).

send_reply(#{id := Id, receiver := Receiver}, Reply) ->
    Message =
        case Reply of
            {Tag, Part} when Tag =:= stream_start; Tag =:= stream; Tag =:= stream_end -> {Id, Tag, Part};
            _ -> {Id, Reply}
        end,
    receive_reply(Receiver, Message),
    ok.

% A receiver's failure is ignored, as OTP's.
receive_reply(Receiver, Message) ->
    try
        call_receiver(Receiver, Message)
    catch
        _:_ -> ok
    end.

call_receiver(Pid, Message) when is_pid(Pid) -> Pid ! {http, Message};
call_receiver(Fun, Message) when is_function(Fun, 1) -> Fun(Message);
call_receiver({Module, Function, Args}, Message) -> apply(Module, Function, [Message | Args]).
