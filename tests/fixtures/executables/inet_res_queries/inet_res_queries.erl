-module(inet_res_queries).
-moduledoc "inet_res against a DNS server of the program's own on the loopback (plan 21 LIB60B).".
-export([main/1]).

main(_) ->
    {Port, Server} = start_server(),
    Options = [{nameservers, [{{127, 0, 0, 1}, Port}]}, {timeout, 1000}],
    resolves(Options),
    errors(Options),
    hosts(Options),
    lookups(Options),
    configuration(),
    Server ! stop,
    io:format("inet_res_queries: ok~n").

resolves(Options) ->
    show(a, inet_res:resolve("a.test", in, a, Options)),
    show(aaaa, answers(inet_res:resolve("six.test", in, aaaa, Options))),
    show(mx, answers(inet_res:resolve("mx.test", in, mx, Options))),
    show(txt, answers(inet_res:resolve("txt.test", in, txt, Options))),
    show(srv, answers(inet_res:resolve("srv.test", in, srv, Options))),
    show(soa, answers(inet_res:resolve("soa.test", in, soa, Options))),
    show(hinfo, answers(inet_res:resolve("hinfo.test", in, hinfo, Options))),
    show(naptr, answers(inet_res:resolve("naptr.test", in, naptr, Options))),
    show(caa, answers(inet_res:resolve("caa.test", in, caa, Options))),
    show(uri, answers(inet_res:resolve("uri.test", in, uri, Options))),
    show(loc, answers(inet_res:resolve("loc.test", in, loc, Options))),
    show(raw_type, answers(inet_res:resolve("raw.test", in, 65280, Options))),
    show(compressed, answers(inet_res:resolve("alias.test", in, a, Options))),
    show(case_insensitive, answers(inet_res:resolve("case.test", in, a, Options))),
    show(bad_id_skipped, answers(inet_res:resolve("badid.test", in, a, Options))),
    show(truncated_tcp, answers(inet_res:resolve("trunc.test", in, a, Options))),
    show(usevc, answers(inet_res:resolve("a.test", in, a, [usevc | Options]))),
    show(
        edns, inet_res:resolve("edns.test", in, a, [{edns, 0}, {udp_payload_size, 1400} | Options])
    ),
    show(empty, inet_res:resolve("empty.test", in, a, Options)),
    show(atom_name, answers(inet_res:resolve('a.test', in, a, Options))),
    show(address_name, answers(inet_res:resolve({10, 0, 0, 1}, in, ptr, Options))),
    show(address_string, answers(inet_res:resolve("10.0.0.1", in, ptr, Options))),
    show(dns_msg, inet_res:dns_msg(element(2, inet_res:resolve("mx.test", in, mx, Options)))).

errors(Options) ->
    show(nxdomain, inet_res:resolve("missing.test", in, a, Options)),
    show(
        nxdomain_reply,
        kind(inet_res:resolve("missing.test", in, a, [{nxdomain_reply, true} | Options]))
    ),
    show(refused, kind(inet_res:resolve("refused.test", in, a, Options))),
    show(formerr, kind(inet_res:resolve("formerr.test", in, a, Options))),
    show(servfail, kind(inet_res:resolve("fail.test", in, a, [{retry, 1} | Options]))),
    show(timeout, inet_res:resolve("silent.test", in, a, [{retry, 1}, {timeout, 200} | Options])),
    show(nslookup_nxdomain, inet_res:nnslookup("missing.test", in, a, element(2, hd(Options)))),
    show(nslookup_formerr, inet_res:nnslookup("formerr.test", in, a, element(2, hd(Options)))),
    show(nslookup_ok, answers(inet_res:nnslookup("a.test", in, a, element(2, hd(Options)), 1000))),
    show(nslookup_servers, answers(inet_res:nslookup("a.test", in, a, element(2, hd(Options))))),
    show(no_servers, inet_res:resolve("a.test", in, a, [{nameservers, []}])),
    show(bad_name, inet_res:resolve("a b", in, a, Options)),
    show(bad_address, inet_res:resolve({300, 0, 0, 1}, in, ptr, Options)),
    [
        show(
            {bad_option, Bad},
            catch_error(fun() -> inet_res:resolve("a.test", in, a, [Bad | Options]) end)
        )
     || Bad <- [
            {retry, 0}, bogus, {timeout, -1}, {nameservers, [x]}, {edns, 1}, {udp_payload_size, 10}
        ]
    ],
    show(
        no_option,
        catch_error(fun() -> inet_res:resolve("a.test", in, a, [norecurse | Options]) end) =/= error
    ).

hosts(Options) ->
    show(gethostbyname, inet_res:gethostbyname("a.test", inet, Options, 1000)),
    show(gethostbyname_alias, inet_res:gethostbyname("alias.test", inet, Options, 1000)),
    show(gethostbyname_dot, inet_res:gethostbyname("dot.test.", inet, Options, 1000)),
    show(gethostbyname6, inet_res:gethostbyname("six.test", inet6, Options, 1000)),
    show(gethostbyname_family, inet_res:gethostbyname("a.test", local, Options, 1000)),
    show(gethostbyname_empty, inet_res:gethostbyname("empty.test", inet, Options, 1000)),
    show(gethostbyname_missing, inet_res:gethostbyname("missing.test", inet, Options, 1000)),
    show(gethostbyname_bad, inet_res:gethostbyname("bad name", inet, Options, 1000)),
    show(getbyname_mx, inet_res:getbyname("mx.test", mx, Options, 1000)),
    show(getbyname_txt, inet_res:getbyname("txt.test", txt, Options, 1000)),
    show(getbyname_bad_type, inet_res:getbyname("mx.test", bogus, Options, 1000)),
    show(getbyname_atom, inet_res:getbyname('mx.test', mx, Options, 1000)),
    show(getbyname_number, inet_res:getbyname(42, mx, Options, 1000)),
    show(gethostbyaddr, inet_res:gethostbyaddr({10, 0, 0, 1}, Options, 1000)),
    show(gethostbyaddr_string, inet_res:gethostbyaddr("10.0.0.1", Options, 1000)),
    show(
        gethostbyaddr_mapped,
        inet_res:gethostbyaddr({0, 0, 0, 0, 0, 16#ffff, 16#0a00, 1}, Options, 1000)
    ),
    show(gethostbyaddr_missing, inet_res:gethostbyaddr({10, 0, 0, 9}, Options, 1000)),
    show(gethostbyaddr_bad, inet_res:gethostbyaddr("not an address", Options, 1000)).

lookups(Options) ->
    show(lookup, inet_res:lookup("a.test", in, a, Options)),
    show(lookup_any_type, inet_res:lookup("alias.test", in, any, Options)),
    show(lookup_other_class, inet_res:lookup("a.test", in, mx, Options)),
    show(lookup_error, inet_res:lookup("missing.test", in, a, Options)),
    show(lookup_timeout, inet_res:lookup("a.test", in, a, Options, 1000)).

configuration() ->
    Rc = inet:get_rc(),
    show(get_rc_lookup, lists:keyfind(lookup, 1, Rc)),
    show(get_rc_list, lists:all(fun is_tuple/1, Rc)).

% The type, class and data of a reply's answers.
answers({ok, Message}) ->
    [{element(3, Rr), element(4, Rr), element(7, Rr)} || Rr <- element(4, Message)];
answers(Other) ->
    Other.

% The error of a reply that carries its message.
kind({error, {Reason, Message}}) when is_tuple(Message) -> {error, Reason, element(1, Message)};
kind(Other) -> Other.

show(Label, Value) -> io:format("~p ~p~n", [Label, Value]).

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% --- The server: UDP and TCP on one loopback port, answering each test name its way ---

start_server() ->
    Self = self(),
    Pid = spawn(fun() -> server(Self) end),
    receive
        {Pid, Port} -> {Port, Pid}
    end.

server(Parent) ->
    {ok, Udp} = gen_udp:open(0, [binary, {active, true}, {ip, {127, 0, 0, 1}}]),
    {ok, Port} = inet:port(Udp),
    {ok, Listen} = gen_tcp:listen(Port, [
        binary, {active, false}, {packet, 2}, {ip, {127, 0, 0, 1}}, {reuseaddr, true}
    ]),
    Self = self(),
    spawn(fun() -> tcp_server(Listen, Self) end),
    Parent ! {self(), Port},
    udp_loop(Udp).

udp_loop(Udp) ->
    receive
        {udp, Udp, IP, Port, Query} ->
            [gen_udp:send(Udp, IP, Port, Reply) || Reply <- replies(udp, Query)],
            udp_loop(Udp);
        stop ->
            ok
    end.

tcp_server(Listen, Owner) ->
    case gen_tcp:accept(Listen, 2000) of
        {ok, Socket} ->
            {ok, Query} = gen_tcp:recv(Socket, 0, 2000),
            [gen_tcp:send(Socket, Reply) || Reply <- replies(tcp, Query)],
            gen_tcp:close(Socket),
            tcp_server(Listen, Owner);
        {error, timeout} ->
            case is_process_alive(Owner) of
                true -> tcp_server(Listen, Owner);
                false -> ok
            end
    end.

% The replies to a query: none for silent.test, a wrong id first for badid.test.
replies(Transport, <<Id:16, _:7, RD:1, _:8, 1:16, _:32, ArCount:16, Rest/binary>>) ->
    {Labels, <<Type:16, Class:16, _/binary>>} = labels(Rest, []),
    Name = string:lowercase(lists:join(".", Labels)),
    Question = <<(name(lists:join(".", Labels)))/binary, Type:16, Class:16>>,
    answer(Transport, Name, Type, {Id, RD, ArCount, Question}).

labels(<<0, Rest/binary>>, Labels) ->
    {lists:reverse(Labels), Rest};
labels(<<Size, Label:Size/binary, Rest/binary>>, Labels) ->
    labels(Rest, [binary_to_list(Label) | Labels]).

answer(_, "silent.test", _, _) ->
    [];
answer(_, "badid.test", _, {Id, _, _, _} = Q) ->
    [
        message(Q, #{id => (Id + 1) band 16#ffff, an => [rr("badid.test", 1, <<9, 9, 9, 9>>)]}),
        message(Q, #{an => [rr("badid.test", 1, <<10, 0, 0, 8>>)]})
    ];
answer(udp, "trunc.test", _, Q) ->
    [message(Q, #{tc => 1})];
answer(_, Name, _, Q) ->
    [message(Q, response(Name, Q))].

response("a.test", _) ->
    #{an => [rr("a.test", 1, <<10, 0, 0, 1>>), rr("a.test", 1, <<10, 0, 0, 2>>)]};
response("dot.test", _) ->
    #{an => [rr("dot.test", 1, <<10, 0, 0, 7>>)]};
response("trunc.test", _) ->
    #{an => [rr("trunc.test", 1, <<10, 0, 0, 3>>)]};
response("six.test", _) ->
    #{an => [rr("six.test", 28, <<16#fe80:16, 0:80, 1:32>>)]};
response("mx.test", _) ->
    #{an => [rr("mx.test", 15, <<10:16, (name("mail.test"))/binary>>)]};
response("txt.test", _) ->
    #{an => [rr("txt.test", 16, <<5, "hello", 5, "world", 0>>)]};
response("srv.test", _) ->
    #{an => [rr("srv.test", 33, <<1:16, 2:16, 8080:16, (name("host.test"))/binary>>)]};
response("soa.test", _) ->
    #{
        an => [
            rr("soa.test", 6, <<
                (name("ns.test"))/binary,
                (name("admin.test"))/binary,
                7:32,
                3600:32,
                600:32,
                86400:32,
                -1:32
            >>)
        ]
    };
response("hinfo.test", _) ->
    #{an => [rr("hinfo.test", 13, <<3, "CPU", 2, "OS">>)]};
response("naptr.test", _) ->
    #{
        an => [
            rr(
                "naptr.test",
                35,
                <<100:16, 10:16, 1, "U", 7, "E2U+SIP", 3, "!x!", (name("next.test"))/binary>>
            )
        ]
    };
response("caa.test", _) ->
    #{an => [rr("caa.test", 257, <<0, 5, "ISSUE", "ca.test">>)]};
response("uri.test", _) ->
    #{an => [rr("uri.test", 256, <<10:16, 1:16, "https://x.test/">>)]};
response("loc.test", _) ->
    #{
        an => [
            rr(
                "loc.test",
                29,
                <<0, 16#12, 16#16, 16#13, 2147483648:32, 2149483648:32, 10000500:32>>
            )
        ]
    };
response("raw.test", _) ->
    #{an => [rr("raw.test", 65280, <<1, 2, 3>>)]};
response("alias.test", {_, _, _, Question}) ->
    #{raw_an => alias_answers(12 + byte_size(Question))};
response("case.test", _) ->
    #{qname => "CASE.Test", an => [rr("CASE.Test", 1, <<10, 0, 0, 5>>)]};
response("edns.test", {_, _, ArCount, _}) ->
    #{
        an => [rr("edns.test", 1, <<10, 0, 0, 6>>)],
        ar => [<<0, 41:16, 4096:16, 0, 0, 16#8000:16, 0:16>> || ArCount > 0]
    };
response("empty.test", _) ->
    #{};
response("missing.test", _) ->
    #{
        rcode => 3,
        ns => [
            rr("test", 6, <<
                (name("ns.test"))/binary, (name("admin.test"))/binary, 1:32, 2:32, 3:32, 4:32, 5:32
            >>)
        ]
    };
response("refused.test", _) ->
    #{rcode => 5};
response("formerr.test", _) ->
    #{rcode => 1};
response("fail.test", _) ->
    #{rcode => 2};
response("1.0.0.10.in-addr.arpa", _) ->
    #{an => [rr("1.0.0.10.in-addr.arpa", 12, name("a.test"))]};
response(_, _) ->
    #{rcode => 3}.

% The answers of alias.test: a CNAME to "a.test" whose name the A record then points at.
alias_answers(Offset) ->
    Cname = <<16#c00c:16, 5:16, 1:16, 60:32, 8:16, 1, "a", 4, "test", 0>>,
    % "a.test" starts after the CNAME's owner pointer and fixed fields: Offset + 2 + 10.
    A = <<3:2, (Offset + 12):14, 1:16, 1:16, 60:32, 4:16, 10, 0, 0, 1>>,
    [Cname, A].

message({Id, RD, _, Question0}, Fields) ->
    Question =
        case Fields of
            #{qname := QName} ->
                <<(name(QName))/binary, (binary:part(Question0, byte_size(Question0), -4))/binary>>;
            _ ->
                Question0
        end,
    An = maps:get(raw_an, Fields, maps:get(an, Fields, [])),
    Ns = maps:get(ns, Fields, []),
    Ar = maps:get(ar, Fields, []),
    Flags =
        <<1:1, 0:4, 1:1, (maps:get(tc, Fields, 0)):1, RD:1, 1:1, 0:3,
            (maps:get(rcode, Fields, 0)):4>>,
    iolist_to_binary([
        <<(maps:get(id, Fields, Id)):16>>,
        Flags,
        <<1:16, (length(An)):16, (length(Ns)):16, (length(Ar)):16>>,
        Question,
        An,
        Ns,
        Ar
    ]).

rr(Name, Type, Data) ->
    <<(name(Name))/binary, Type:16, 1:16, 300:32, (byte_size(Data)):16, Data/binary>>.

name(Name) -> iolist_to_binary([[length(L), L] || L <- string:split(Name, ".", all)] ++ [0]).
