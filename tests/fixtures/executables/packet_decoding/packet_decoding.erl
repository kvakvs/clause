-module(packet_decoding).
-export([main/1]).

%% erlang:decode_packet/3 (plan 21 step LIB9C): every packet type's length rule, the options, the http request,
%% response and header lines (lists and binaries), URIs, ssl_tls records and the error and more answers.
main(_) ->
    lengths(),
    lines(),
    options(),
    http(),
    headers(),
    uris(),
    ssl(),
    errors(),
    io:format("packet_decoding: ok~n").

d(Type, Bin) -> erlang:decode_packet(Type, Bin, []).

lengths() ->
    show(lengths, [
        d(raw, <<"abc">>),
        d(0, <<>>),
        d(1, <<3, "abcde">>),
        d(1, <<5, "ab">>),
        d(1, <<>>),
        d(2, <<0, 2, "xyz">>),
        d(2, <<0>>),
        d(4, <<0, 0, 0, 1, "q">>),
        d(4, <<0, 0, 1, 0, "short">>),
        d(sunrm, <<128, 0, 0, 2, "hi!">>),
        d(asn1, <<16#30, 3, 1, 2, 3, 4>>),
        d(asn1, <<16#30, 16#81, 2, 9, 9, 7>>),
        d(asn1, <<16#30, 16#82, 0, 1, 5>>),
        d(asn1, <<16#1F, 16#81, 16#01, 2, 7, 7>>),
        d(asn1, <<16#30, 16#85, 0, 0, 0, 0, 1>>),
        d(asn1, <<16#30>>),
        d(asn1, <<16#30, 16#84, 0>>),
        d(cdr, <<"GIOP", 1, 2, 0, 0, 0, 0, 0, 2, "ab", "z">>),
        d(cdr, <<"GIOP", 1, 2, 1, 0, 2, 0, 0, 0, "ab">>),
        d(cdr, <<"GIOX", 1, 2, 0, 0, 0, 0, 0, 0>>),
        d(fcgi, <<1, 1, 0, 1, 0, 3, 2, 0, "abc", 0, 0, "rest">>),
        d(fcgi, <<2, 1, 0, 1, 0, 3, 2, 0>>),
        d(tpkt, <<3, 0, 0, 6, "ab", "x">>),
        d(tpkt, <<3, 0, 0, 2>>),
        d(tpkt, <<4, 0, 0, 6, "ab">>)
    ]).

lines() ->
    show(lines, [
        d(line, <<"one\ntwo\n">>),
        d(line, <<"no newline">>),
        d(line, <<"\n">>),
        erlang:decode_packet(line, <<"abcdef\n">>, [{line_length, 3}]),
        erlang:decode_packet(line, <<"ab">>, [{line_length, 2}]),
        erlang:decode_packet(line, <<"abcdef\n">>, [{packet_size, 4}]),
        erlang:decode_packet(line, <<"abcdef">>, [{packet_size, 4}]),
        erlang:decode_packet(line, <<"a;b;c">>, [{line_delimiter, $;}]),
        erlang:decode_packet(line, <<"abc">>, [{line_length, 0}, {packet_size, 0}])
    ]).

options() ->
    show(options, [
        erlang:decode_packet(1, <<3, "abcde">>, [{packet_size, 2}]),
        erlang:decode_packet(1, <<3, "abcde">>, [{packet_size, 3}]),
        erlang:decode_packet(2, <<0, 9>>, [{packet_size, 8}]),
        catching(fun() -> erlang:decode_packet(1, <<>>, [{packet_size, -1}]) end),
        catching(fun() -> erlang:decode_packet(1, <<>>, [{line_delimiter, $;}]) end),
        catching(fun() -> erlang:decode_packet(line, <<>>, [{line_delimiter, 256}]) end),
        catching(fun() -> erlang:decode_packet(1, <<>>, [{other, 1}]) end),
        catching(fun() -> erlang:decode_packet(1, <<>>, [bad]) end),
        catching(fun() -> erlang:decode_packet(1, <<>>, [{packet_size, 1} | id(tail)]) end),
        catching(fun() -> erlang:decode_packet(1, <<>>, id(notalist)) end),
        catching(fun() -> erlang:decode_packet(1, <<>>, [{packet_size, 1 bsl 32}]) end)
    ]).

http() ->
    show(http, [
        d(http, <<"GET /index.html HTTP/1.1\r\nHost: x\r\n">>),
        d(http_bin, <<"GET /index.html HTTP/1.1\r\n">>),
        d(http, <<"POST http://example.com:8080/a?b HTTP/1.0\r\n">>),
        d(http, <<"get / HTTP/1.1\r\n">>),
        d(http, <<"OPTIONS * HTTP/1.1\r\n">>),
        d(http, <<"GET /old\r\n">>),
        d(http, <<"BREW /pot HTTP/2.0\r\n">>),
        d(http, <<"HTTP/1.1 200 OK\r\n">>),
        d(http_bin, <<"HTTP/1.0 404 Not Found\r\n">>),
        d(http, <<"HTTP/1.1 204\r\n">>),
        d(http, <<"HTTP/1.1 301 Moved Permanently\nrest">>),
        d(http, <<"GET /partial">>),
        d(http, <<"\r\n">>),
        d(http, <<"garbage line\r\n">>),
        d(http, <<"HTTP/x.1 200 OK\r\n">>),
        d(http, <<"GET / HTTP/1\r\n">>),
        d(http_bin, <<"bad\r\n">>),
        erlang:decode_packet(http, <<"GET /very/long/path HTTP/1.1">>, [{packet_size, 10}]),
        erlang:decode_packet(http, <<"GET /a HTTP/1.1\r\n">>, [{line_length, 5}])
    ]).

headers() ->
    show(headers, [
        d(httph, <<"Content-Type: text/html\r\n">>),
        d(httph, <<"content-length:  12\r\n\r\n">>),
        d(httph_bin, <<"X-Custom-HEADER: value\r\n">>),
        d(httph, <<"x-forwarded-for: 1.2.3.4\r\n">>),
        d(httph, <<"Set-Cookie: a=1\r\n continued\r\nNext: 2\r\n">>),
        d(httph, <<"Set-Cookie: a=1\r\n continued\r\n">>),
        d(httph, <<"\r\n">>),
        d(httph, <<"\n">>),
        d(httph, <<"No-Colon-Here\r\n">>),
        d(httph, <<"Bad Name: x\r\n">>),
        d(httph, <<": empty name\r\n">>),
        d(httph_bin, <<"WWW-AUTHENTICATE: Basic\r\n">>),
        d(httph, <<(binary:copy(<<"a">>, 60))/binary, ": long\r\n">>),
        d(httph, <<"Host: x">>)
    ]).

uris() ->
    U = fun(Uri) ->
        {ok, {http_request, _, Parsed, _}, _} = d(http, <<"GET ", Uri/binary, " HTTP/1.1\r\n">>),
        Parsed
    end,
    show(uris, [
        U(<<"/path">>),
        U(<<"x">>),
        U(<<"http://host">>),
        U(<<"HTTP://Host:80/p">>),
        U(<<"https://h:0/p">>),
        U(<<"http://h:12a/p">>),
        U(<<"http://[::1]:4000/p">>),
        U(<<"http://[::1]/p">>),
        U(<<"http://[::1]x/p">>),
        U(<<"mailto:me@there">>),
        U(<<"plain">>)
    ]).

ssl() ->
    show(ssl, [
        d(ssl_tls, <<22, 3, 1, 0, 2, "hi", "rest">>),
        d(ssl_tls, <<128, 7, 1, 3, 0, "abcd">>),
        d(ssl_tls, <<22, 3, 1>>),
        d(ssl_tls, <<22, 3, 1, 0, 5, "ab">>)
    ]).

errors() ->
    show(errors, [
        catching(fun() -> d(id(3), <<>>) end),
        catching(fun() -> d(id(nope), <<>>) end),
        catching(fun() -> d(raw, id(notabinary)) end),
        catching(fun() -> d(raw, id(<<1:3>>)) end),
        catching(fun() -> erlang:decode_packet(id(nope), <<>>, id(x)) end)
    ]).

catching(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Values) -> io:format("~s: ~w~n", [Label, Values]).

id(X) -> X.
