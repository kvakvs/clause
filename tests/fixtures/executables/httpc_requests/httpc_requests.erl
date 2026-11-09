%% inets' HTTP client (plan 21 step MIM4, docs/libraries/inets.md#httpc) against local servers: the request bytes
%% httpc writes, its results and errors, keep-alive sessions, redirects, retries, timeouts, asynchronous receivers,
%% streams, cancellation, options and profiles, a proxy, TLS, http_uri and inets' services.
-module(httpc_requests).
-export([main/1, log/2, receiver/2]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    uris(),
    io:format("before start ~p~n", [httpc:get_option(max_sessions)]),
    ok = logger:remove_handler(default),
    ok = logger:add_handler(reports, ?MODULE, #{level => all}),
    services(),
    options(),
    Port = server(listen(), true),
    requests(url(Port)),
    redirects(url(Port)),
    failures(url(Port)),
    asynchronous(url(Port)),
    cancel(url(server(listen(), false))),
    proxy(Port),
    tls(),
    io:format("stop ~p~n", [inets:stop()]),
    io:format("after stop ~p~n", [httpc:get_option(max_sessions)]),
    io:format("httpc_requests: ok~n").

% httpc's reports of ignored options, as text.
log(#{msg := {report, #{report := Report}}}, _) ->
    Text = lists:flatten(Report),
    case lists:prefix("Invalid option", Text) of
        true -> io:format("  report ~p~n", [Text]);
        false -> ok
    end;
log(_, _) ->
    ok.

uris() ->
    Try = fun(F, X) ->
        try F(X) of
            R -> R
        catch
            C:R -> {C, R}
        end
    end,
    Encoded = ["a b", "a/b?c=d&e#f", "é", <<"a b/">>, <<"ü"/utf8>>, <<252>>, "~-._!*'()", "%+", "", [16#20AC]],
    io:format("encode ~p~n", [[Try(fun http_uri:encode/1, X) || X <- Encoded]]),
    Decoded = ["a%20b", "%2F%2f", "%", "%zz", "+", <<"a%20b">>, "%C3%A9", "%e9", <<"%e9">>, "%2", "%E2%82%AC"],
    io:format("decode ~p~n", [[Try(fun http_uri:decode/1, X) || X <- Decoded]]).

services() ->
    io:format("start ~p~n", [inets:start()]),
    io:format("start again ~p~n", [inets:start()]),
    {ok, Foo} = inets:start(httpc, [{profile, foo}]),
    io:format("profile again ~p~n", [inets:start(httpc, [{profile, foo}]) =:= {error, {already_started, Foo}}]),
    io:format("no profile ~p~n", [inets:start(httpc, [])]),
    io:format("registered ~p~n", [[is_pid(whereis(N)) || N <- [httpc_manager, httpc_foo]]]),
    io:format("services ~p~n", [[S || {S, _} <- inets:services()]]),
    io:format("services_info ~p~n", [[{S, Info} || {S, _, Info} <- inets:services_info()]]),
    io:format("service_names ~p~n", [inets:service_names()]),
    io:format("profile option ~p ~p~n", [httpc:get_option(max_sessions, foo), httpc:get_option(max_sessions, bar)]),
    io:format("profile names ~p~n", [[httpc:profile_name(default), httpc:profile_name(foo), httpc:default_profile()]]),
    io:format("children ~p~n", [[Id || {Id, _, _, _} <- supervisor:which_children(httpc_profile_sup)]]),
    io:format("stop profile ~p ~p~n", [inets:stop(httpc, foo), inets:stop(httpc, foo)]),
    {ok, Alone} = inets:start(httpc, [{profile, alone}], stand_alone),
    io:format("stand-alone ~p ~p~n", [whereis(httpc_alone), httpc:get_option(max_sessions, Alone)]),
    unlink(Alone),
    io:format("stop stand-alone ~p~n", [inets:stop(stand_alone, Alone)]).

options() ->
    io:format("options ~p~n", [httpc:get_options(all)]),
    io:format("get list ~p~n", [httpc:get_options([max_sessions, keep_alive_timeout])]),
    io:format("get bad ~p~n", [httpc:get_options([max_sessions, bogus])]),
    Set = [
        {max_sessions, 4},
        {max_sessions, -1},
        {max_sessions, infinity},
        {keep_alive_timeout, infinity},
        {keep_alive_timeout, -1},
        {pipeline_timeout, infinity},
        {max_pipeline_length, infinity},
        {cookies, x},
        {verbose, true},
        {ipfamily, x},
        {ipfamily, local},
        {unix_socket, "/tmp/x"},
        {ip, default},
        {port, default},
        {socket_opts, x},
        {max_connections_open, x},
        {proxy, {undefined, []}},
        {proxy, {"h", []}},
        {https_proxy, {undefined, []}},
        {bogus, 1}
    ],
    [io:format("set ~p ~p~n", [O, httpc:set_option(N, V)]) || {N, V} = O <- Set],
    io:format("set_options atomic ~p ~p~n", [
        httpc:set_options([{max_sessions, 7}, {max_sessions, x}]), httpc:get_option(max_sessions)
    ]),
    Reset = [{max_sessions, 2}, {keep_alive_timeout, 120000}, {pipeline_timeout, 0}],
    io:format("set_options ~p~n", [httpc:set_options(Reset)]),
    io:format("verify options ~p ~p~n", [
        [element(1, O) || O <- httpc:ssl_verify_host_options(true)],
        [element(1, O) || O <- httpc:ssl_verify_host_options(false)]
    ]).

url(Port) ->
    fun(Path) -> "http://localhost:" ++ integer_to_list(Port) ++ Path end.

requests(U) ->
    show(get, httpc:request(U("/ok"))),
    show(reused, httpc:request(U("/ok"))),
    Headers = [
        {"X-Z", "z"},
        {"User-Agent", "ua"},
        {"Accept", "*/*"},
        {"Authorization", "a"},
        {"Te", "t"},
        {"Content-Encoding", "gzip"},
        {"Cache-Control", "no"},
        {"Connection", "keep-alive"},
        {"Date", "d"},
        {"Pragma", "p"},
        {"Trailer", "tr"},
        {"Transfer-Encoding", "identity"},
        {"Upgrade", "u"},
        {"Via", "v"},
        {"Warning", "w"},
        {"Accept-Charset", "c"},
        {"Accept-Encoding", "e"},
        {"Accept-Language", "l"},
        {"Expect", "x"},
        {"From", "f"},
        {"Host", "h"},
        {"If-Match", "1"},
        {"If-Modified-Since", "2"},
        {"If-None-Match", "3"},
        {"If-Range", "4"},
        {"If-Unmodified-Since", "5"},
        {"Max-Forwards", "6"},
        {"Proxy-Authorization", "7"},
        {"Range", "8"},
        {"Referer", "9"},
        {"Allow", "al"},
        {"Content-Language", "cl"},
        {"Content-Location", "clo"},
        {"Content-MD5", "md"},
        {"Content-Range", "cr"},
        {"Expires", "ex"},
        {"Last-Modified", "lm"},
        {"X-A", "a"},
        {"x-a", "a2"},
        {"content-length", "99"}
    ],
    show(request_headers, httpc:request(put, {U("/ok"), Headers, "a/b", "q"}, [], [])),
    show(repeated_fields, httpc:request(get, {U("/ok"), [{"user-agent", "1"}, {"User-Agent", "2"}]}, [], [])),
    show(binary_value, httpc:request(get, {U("/ok"), [{"x", <<"b">>}, {"content-type", "t"}]}, [], [])),
    show(post, httpc:request(post, {U("/ok"), [], "text/plain", "hello"}, [], [])),
    show(post_binary, httpc:request(post, {U("/ok"), [], "a/b", <<"h", 233>>}, [], [])),
    show(post_iolist, httpc:request(post, {U("/ok"), [], "a/b", ["a", <<"b">>, $c]}, [], [])),
    show(post_empty, httpc:request(post, {U("/ok"), [{"Content-Type", "x/y"}], "a/b", <<>>}, [], [])),
    show(post_fun, httpc:request(post, {U("/ok"), [{"content-length", "2"}], "a/b", {fun body/1, 2}}, [], [])),
    show(patch, httpc:request(patch, {U("/ok"), [], "a/b", "p"}, [], [])),
    show(put_no_content, httpc:request(put, {U("/ok"), []}, [], [])),
    show(delete, httpc:request(delete, {U("/ok"), []}, [], [])),
    show(delete_content, httpc:request(delete, {U("/ok"), [], "a/b", "d"}, [], [])),
    [show(M, httpc:request(M, {U("/ok"), []}, [], [])) || M <- [head, options, trace]],
    show(te, httpc:request(get, {U("/ok"), [{"te", "trailers"}]}, [], [])),
    show(user, httpc:request("http://u%40x:p@localhost:" ++ port(U) ++ "/ok")),
    show(user_alone, httpc:request("http://user@localhost:" ++ port(U) ++ "/ok")),
    show(normalized, httpc:request(U("/a/../%7e%41?"))),
    show(query, httpc:request(U("/ok?a=1&b=%20x#frag"))),
    show(no_path, httpc:request(list_to_binary("http://localhost:" ++ port(U)))),
    show(as_is, httpc:request(post, {U("/ok"), [{"X-Y", "1"}, {"Host", "h"}], "a/b", ""}, [], [{headers_as_is, true}])),
    show(response_headers, httpc:request(U("/headers"))),
    show(repeated_headers, httpc:request(U("/repeated"))),
    show(chunked, httpc:request(U("/chunked"))),
    show(chunked_binary, httpc:request(get, {U("/chunked"), []}, [], [{body_format, binary}])),
    show(continue, httpc:request(U("/continue"))),
    show(no_content, httpc:request(get, {U("/empty"), []}, [], [{body_format, binary}])),
    show(head_binary, httpc:request(head, {U("/ok"), []}, [], [{body_format, binary}])),
    show(full_result, httpc:request(get, {U("/404"), []}, [], [{full_result, false}])),
    show(big, element(1, element(2, httpc:request(get, {U("/big"), []}, [], [{body_format, binary}])))),
    show(error_400, httpc:request(U("/400"))),
    show(after_400, httpc:request(U("/ok"))),
    show(error_500, httpc:request(U("/500"))),
    show(after_500, httpc:request(U("/ok"))),
    show(error_503, httpc:request(U("/503"))),
    show(close_asked, httpc:request(get, {U("/ok"), [{"connection", "close"}]}, [], [])),
    show(after_close_asked, httpc:request(U("/ok"))),
    show(close_answered, httpc:request(U("/close"))),
    show(until_close, httpc:request(U("/until_close"))),
    show(http10, httpc:request(get, {U("/ok"), []}, [{version, "HTTP/1.0"}], [])),
    show(http10_response, httpc:request(U("/http10"))),
    show(own_socket, httpc:request(get, {U("/ok"), []}, [], [{socket_opts, [{nodelay, true}]}])),
    show(options_ignored, httpc:request(get, {U("/ok"), []}, [{bogus, 1}, {timeout, -1}], [bogus, {body_format, xml}])).

port(U) ->
    lists:takewhile(fun(C) -> C =/= $/ end, lists:nthtail(length("http://localhost:"), U(""))).

body(0) -> eof;
body(N) -> {ok, integer_to_list(N), N - 1}.

redirects(U) ->
    show(redirect_new_connection, httpc:request(U("/r301"))),
    show(redirect_reused, httpc:request(U("/r302"))),
    [
        show({Code, Method}, httpc:request(Method, request(Method, U("/r" ++ integer_to_list(Code))), [], []))
     || Code <- [300, 301, 303, 307, 308, 304], Method <- [get, head, post, put]
    ],
    show(redirect_off, httpc:request(get, {U("/r301"), []}, [{autoredirect, false}], [])),
    show(redirect_other_server, httpc:request(U("/absolute"))),
    show(redirect_loop, httpc:request(U("/loop"))),
    show(redirect_no_location, httpc:request(U("/no_location"))).

request(Method, Url) when Method =:= post; Method =:= put -> {Url, [], "a/b", "x"};
request(_, Url) -> {Url, []}.

failures(U) ->
    show(dropped, httpc:request(U("/drop"))),
    show(garbage, httpc:request(U("/garbage"))),
    show(invalid_version, httpc:request(U("/invalid_version"))),
    show(invalid_status, httpc:request(U("/invalid_status"))),
    show(no_reason, httpc:request(U("/no_reason"))),
    show(timeout, httpc:request(get, {U("/slow"), []}, [{timeout, 300}], [])),
    show(after_timeout, httpc:request(U("/ok"))),
    show(refused, httpc:request("http://localhost:1/x")),
    show(no_scheme, httpc:request("foo")),
    show(relative, httpc:request("//localhost/x")),
    show(bad_scheme, httpc:request("ftp://localhost/x")),
    show(invalid_uri, httpc:request(U("/a b"))),
    show(invalid_port, httpc:request("http://localhost:abc/")),
    [show({method, M}, httpc:request(M, {U("/ok"), []}, [], [])) || M <- [connect, 'GET', "get"]],
    show(content_for_get, httpc:request(get, {U("/ok"), [], "a/b", "x"}, [], [])),
    show(no_content_for_post, httpc:request(post, {U("/ok"), []}, [], [])),
    show(binary_type, httpc:request(post, {U("/ok"), [], <<"a/b">>, "x"}, [], [])),
    show(integer_body, httpc:request(post, {U("/ok"), [], "a/b", 1}, [], [])),
    show(not_request, httpc:request(get, U("/ok"), [], [])),
    show(integer_value, httpc:request(get, {U("/ok"), [{"x", 1}]}, [], [])),
    show(binary_name, httpc:request(get, {U("/ok"), [{<<"x">>, "1"}]}, [], [])),
    show(method_first, httpc:request(foo, {"foo", [{x, 1}]}, [], [])),
    show(stream_sync, httpc:request(get, {U("/ok"), []}, [], [{stream, self}])).

asynchronous(U) ->
    Self = self(),
    Async = fun(Label, Url, Options) ->
        {ok, Id} = httpc:request(get, {Url, []}, [], [{sync, false} | Options]),
        receive
            {http, {Id, Result}} -> show(Label, Result);
            {got, Via, {Id, Result}} -> show({Label, Via}, Result)
        after 5000 -> show(Label, no_message)
        end
    end,
    Async(async, U("/ok"), []),
    Async(async_string, U("/ok"), [{body_format, string}]),
    Async(async_full, U("/404"), [{full_result, false}]),
    Async(async_error, "http://localhost:1/", []),
    Async(async_fun, U("/ok"), [{receiver, fun(Reply) -> Self ! {got, 'fun', Reply} end}]),
    Async(async_mfa, U("/ok"), [{receiver, {?MODULE, receiver, [Self]}}]),
    {ok, Failing} = httpc:request(get, {U("/ok"), []}, [], [{sync, false}, {receiver, {?MODULE, nothing, []}}]),
    % Nothing printed between: the server tells both requests in turn.
    show(after_failing_receiver, {is_reference(Failing), httpc:request(U("/ok"))}),
    {ok, Stream} = httpc:request(get, {U("/chunked"), []}, [], [{sync, false}, {stream, self}]),
    show(stream, [Part || Part <- stream(Stream)]).

receiver(Reply, Pid) -> Pid ! {got, mfa, Reply}.

stream(Id) ->
    receive
        {http, {Id, stream_end, Headers}} -> [{stream_end, Headers}];
        {http, {Id, Tag, Part}} -> [{Tag, Part} | stream(Id)]
    after 5000 -> [no_message]
    end.

cancel(U) ->
    {ok, Id} = httpc:request(get, {U("/slow"), []}, [], [{sync, false}]),
    show(cancel, {httpc:cancel_request(Id), httpc:cancel_request(Id), httpc:cancel_request(make_ref())}),
    receive
        {http, {Id, _}} -> show(cancelled, message)
    after 1500 -> show(cancelled, no_message)
    end.

% Requests through a profile whose proxy is the local server.
proxy(Port) ->
    {ok, _} = inets:start(httpc, [{profile, proxied}]),
    ok = httpc:set_option(proxy, {{"localhost", Port}, ["localhost"]}, proxied),
    show(proxied, httpc:request(get, {"http://example.com/x?y", []}, [{proxy_auth, {"u", "p"}}], [], proxied)),
    show(proxied_port, httpc:request(get, {"http://example.com:8080/x", []}, [], [], proxied)),
    show(proxied_http10, httpc:request(get, {"http://example.com/x", []}, [{version, "HTTP/1.0"}], [], proxied)),
    show(not_proxied, httpc:request(get, {"http://localhost:" ++ integer_to_list(Port) ++ "/ok", []}, [], [], proxied)).

tls() ->
    ok = ssl:start(),
    AltName = #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = [{dNSName, "localhost"}], critical = false},
    Config = public_key:pkix_test_data(#{
        root => [{key, {namedCurve, secp256r1}}, {digest, sha256}],
        intermediates => [],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}, {extensions, [AltName]}]
    }),
    Root = lists:last(proplists:get_value(cacerts, Config)),
    Server = [{cert, proplists:get_value(cert, Config)}, {key, proplists:get_value(key, Config)}, {log_level, none}],
    Url = fun() ->
        {ok, Listen} = ssl:listen(0, [binary, {active, false}, {reuseaddr, true} | Server]),
        {ok, {_, Port}} = ssl:sockname(Listen),
        Self = self(),
        spawn_link(fun() -> tls_acceptor(Listen, Port, Self) end),
        "https://localhost:" ++ integer_to_list(Port) ++ "/ok"
    end,
    Unverified = Url(),
    show(tls, httpc:request(get, {Unverified, []}, [{ssl, [{verify, verify_none}, {log_level, none}]}], [])),
    show(tls_reused, httpc:request(get, {Unverified, []}, [{ssl, [{verify, verify_none}]}], [])),
    show(tls_verified, httpc:request(get, {Url(), []}, [{ssl, [{cacerts, [Root]}, {log_level, none}]}], [])),
    show(tls_unknown_ca, alert(httpc:request(get, {Url(), []}, [{ssl, [{log_level, none}]}], []))),
    show(tls_default, alert(httpc:request(Url()))),
    show(tls_refused, httpc:request(get, {"https://localhost:1/", []}, [{ssl, [{verify, verify_none}]}], [])).

% A connection error with the server's port masked, a TLS alert without its description text.
alert({error, {failed_connect, [{to_address, {Host, _}}, {inet, Family, {tls_alert, {Alert, _}}}]}}) ->
    {error, {failed_connect, [{to_address, {Host, port}}, {inet, Family, {tls_alert, Alert}}]}};
alert({error, {failed_connect, [{to_address, {Host, _}}, Reason]}}) ->
    {error, {failed_connect, [{to_address, {Host, port}}, Reason]}};
alert(Other) ->
    Other.

tls_acceptor(Listen, Port, Parent) ->
    {ok, Transport} = ssl:transport_accept(Listen),
    Pid = spawn(fun() ->
        receive
            go -> ok
        end,
        case ssl:handshake(Transport, 5000) of
            {ok, Socket} -> tls_connection(Socket, Port);
            {error, _} -> ok
        end
    end),
    ok = ssl:controlling_process(Transport, Pid),
    Pid ! go,
    tls_acceptor(Listen, Port, Parent).

tls_connection(Socket, Port) ->
    case ssl:recv(Socket, 0) of
        {ok, Data} ->
            io:format("  [tls] ~p~n", [masked(Data, Port)]),
            ok = ssl:send(Socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"),
            tls_connection(Socket, Port);
        {error, _} ->
            ok
    end.

show(Label, Result) ->
    io:format("~p -> ~p~n", [Label, normal(Result)]).

normal({Id, Result}) when is_reference(Id) -> {id, Result};
normal(Result) -> Result.

%% --- The local HTTP server ---

listen() ->
    {ok, Listen} = gen_tcp:listen(0, [binary, {active, false}, {reuseaddr, true}, {ip, {127, 0, 0, 1}}]),
    Listen.

% A server telling the connections it accepts and the requests it reads (Verbose), answering by path.
server(Listen, Verbose) ->
    {ok, Port} = inet:port(Listen),
    spawn_link(fun() -> acceptor(Listen, Port, Verbose, 1) end),
    Port.

acceptor(Listen, Port, Verbose, N) ->
    {ok, Socket} = gen_tcp:accept(Listen),
    tell(Verbose, "  [connection ~p]~n", [N]),
    Pid = spawn(fun() ->
        receive
            go -> connection(Socket, Port, Verbose, N, <<>>)
        end
    end),
    ok = gen_tcp:controlling_process(Socket, Pid),
    Pid ! go,
    acceptor(Listen, Port, Verbose, N + 1).

tell(true, Format, Args) -> io:format(Format, Args);
tell(false, _, _) -> ok.

connection(Socket, Port, Verbose, N, Buffer) ->
    case binary:split(Buffer, <<"\r\n\r\n">>) of
        [Head, Rest] ->
            {Body, Rest1} = read_body(Socket, Rest, content_length(Head)),
            tell(Verbose, "  [~p] ~p~n", [N, masked(<<Head/binary, "\r\n\r\n", Body/binary>>, Port)]),
            [Line | _] = binary:split(Head, <<"\r\n">>),
            [_, Path | _] = binary:split(Line, <<" ">>, [global]),
            case answer(Socket, Port, Path) of
                close -> gen_tcp:close(Socket);
                keep -> connection(Socket, Port, Verbose, N, Rest1)
            end;
        [_] ->
            case gen_tcp:recv(Socket, 0) of
                {ok, Data} -> connection(Socket, Port, Verbose, N, <<Buffer/binary, Data/binary>>);
                {error, _} -> ok
            end
    end.

masked(Data, Port) ->
    binary:replace(Data, integer_to_binary(Port), <<"PORT">>, [global]).

content_length(Head) ->
    case re:run(Head, "(?i)content-length: *([0-9]+)", [{capture, [1], list}]) of
        {match, [Length]} -> list_to_integer(Length);
        nomatch -> 0
    end.

read_body(_Socket, Buffer, Length) when byte_size(Buffer) >= Length ->
    <<Body:Length/binary, Rest/binary>> = Buffer,
    {Body, Rest};
read_body(Socket, Buffer, Length) ->
    {ok, Data} = gen_tcp:recv(Socket, 0),
    read_body(Socket, <<Buffer/binary, Data/binary>>, Length).

answer(Socket, Port, Path) ->
    case response(Path, Port) of
        {sleep, Time} ->
            timer:sleep(Time),
            close;
        {Close, Response} ->
            ok = gen_tcp:send(Socket, Response),
            Close;
        close ->
            close
    end.

response(<<"/ok", _/binary>>, _) ->
    {keep, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Type: text/plain\r\n\r\nhi"};
response(<<"/headers">>, _) ->
    {keep, [
        "HTTP/1.1 200 OK\r\n",
        "X-Other: o\r\nLast-Modified: lm\r\nExpires: ex\r\nContent-Type: ct\r\nContent-Range: cr\r\n",
        "Content-MD5: md\r\nContent-Location: clo\r\nContent-Language: cl\r\nContent-Encoding: identity\r\n",
        "Allow: al\r\nWWW-Authenticate: wa\r\nVary: va\r\nServer: se\r\nRetry-After: ra\r\n",
        "Proxy-Authenticate: pa\r\nLocation: lo\r\nEtag: et\r\nAge: ag\r\nAccept-Ranges: ar\r\n",
        "Warning: wa\r\nVia: vi\r\nUpgrade: up\r\nTrailer: tr\r\nPragma: pr\r\nDate: da\r\n",
        "Connection: keep-alive\r\nCache-Control: cc\r\nContent-Length: 0\r\nA-First: f\r\n\r\n"
    ]};
response(<<"/repeated">>, _) ->
    {keep, [
        "HTTP/1.1 200 OK\r\nContent-Type: a\r\nContent-Type: b\r\nServer: s1\r\nserver: s2\r\nX-M: 1\r\n",
        "x-m: 2\r\nNoSpace:x\r\nX-Blank:   v  \r\ncontent-LENGTH: 0\r\n\r\n"
    ]};
response(<<"/chunked">>, _) ->
    {keep, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2;x=y\r\nde\r\n0\r\nX-T: 1\r\n\r\n"};
response(<<"/continue">>, _) ->
    {keep, "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nc"};
response(<<"/empty">>, _) ->
    {keep, "HTTP/1.1 204 No Content\r\n\r\n"};
response(<<"/big">>, _) ->
    {keep, ["HTTP/1.1 200 OK\r\nContent-Length: 200000\r\n\r\n", binary:copy(<<"0123456789">>, 20000)]};
response(<<"/404">>, _) ->
    {keep, "HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nnf!"};
response(<<"/400">>, _) ->
    {keep, "HTTP/1.1 400 Bad Request\r\nContent-Length: 1\r\n\r\nx"};
response(<<"/500">>, _) ->
    {keep, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 1\r\n\r\nx"};
response(<<"/503">>, _) ->
    {keep, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 1\r\n\r\nx"};
response(<<"/close">>, _) ->
    {close, "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nConnection: close\r\n\r\nc"};
response(<<"/until_close">>, _) ->
    {close, "HTTP/1.1 200 OK\r\n\r\nuntil close"};
response(<<"/http10">>, _) ->
    {close, "HTTP/1.0 200 OK\r\n\r\nten"};
response(<<"/r", Code:3/binary>>, _) ->
    {keep, ["HTTP/1.1 ", Code, " R\r\nLocation: /ok\r\nContent-Length: 0\r\n\r\n"]};
response(<<"/absolute">>, Port) ->
    {keep, [
        "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:", integer_to_list(Port), "/ok\r\nContent-Length: 0\r\n\r\n"
    ]};
response(<<"/loop">>, _) ->
    {keep, "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n"};
response(<<"/no_location">>, _) ->
    {keep, "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n"};
response(<<"/drop">>, _) ->
    close;
response(<<"/garbage">>, _) ->
    {close, "garbage\r\n\r\n"};
response(<<"/invalid_version">>, _) ->
    {close, "HTTX/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"};
response(<<"/invalid_status">>, _) ->
    {close, "HTTP/1.1 2x0 OK\r\nContent-Length: 0\r\n\r\n"};
response(<<"/no_reason">>, _) ->
    {keep, "HTTP/1.1 200\r\nContent-Length: 0\r\n\r\n"};
response(<<"/slow">>, _) ->
    {sleep, 1000};
response(_, _) ->
    {keep, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"}.
