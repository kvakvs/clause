%% eldap against a local LDAP server that prints each request as a BER tree (plan 21 step MIM2): filters, options,
%% binds, searches, updates, password changes, message identifiers, errors, timeouts, log calls and TLS, as OTP 29's
%% eldap sends and answers them. Golden generated once from OTP.
-module(eldap_requests).
-export([main/1]).

-include_lib("eldap/include/eldap.hrl").
-include_lib("public_key/include/public_key.hrl").

main(_) ->
    constructors(),
    Port = server(fun answer/1, none),
    opening(Port),
    {ok, H} = eldap:open(["localhost"], [{port, Port}, {timeout, 2000}]),
    binds(H),
    searches(H),
    updates(H),
    closing(H),
    lost(Port),
    timeouts(Port),
    logged(Port),
    tls(),
    io:format("eldap_requests: ok~n").

show(Name, Value) ->
    io:format("~p ~p~n", [Name, Value]).

%% --- Values built without a connection ---

constructors() ->
    show(filters, [
        eldap:equalityMatch("cn", "x"),
        eldap:substrings("cn", [{initial, "a"}, {any, "b"}, {final, "c"}]),
        eldap:present("cn"),
        eldap:approxMatch("a", "b"),
        eldap:greaterOrEqual("a", "1"),
        eldap:lessOrEqual("a", "2"),
        eldap:extensibleMatch("v", [{type, "sn"}, {matchingRule, "r"}]),
        eldap:extensibleMatch("v", [{dnAttributes, true}]),
        eldap:extensibleMatch("v", []),
        eldap:'and'([eldap:present("a")]),
        eldap:'or'([]),
        eldap:'not'(eldap:present("a"))
    ]),
    show(filter_errors, [
        caught(fun() -> eldap:substrings("cn", [{bogus, "x"}]) end),
        caught(fun() -> eldap:extensibleMatch("v", [{bogus, 1}]) end),
        caught(fun() -> eldap:present(x) end)
    ]),
    show(constants, [
        eldap:baseObject(),
        eldap:singleLevel(),
        eldap:wholeSubtree(),
        eldap:neverDerefAliases(),
        eldap:derefInSearching(),
        eldap:derefFindingBaseObj(),
        eldap:derefAlways()
    ]),
    show(mods, [eldap:mod_add("a", ["1"]), eldap:mod_delete("a", []), eldap:mod_replace("a", ["x", "y"])]),
    show(paged, [eldap:paged_result_control(5), eldap:paged_result_control(5, "abc")]),
    show(cookie, [
        eldap:paged_result_cookie(#eldap_search_result{}),
        caught(fun() -> eldap:paged_result_cookie(#eldap_search_result{controls = asn1_NOVALUE}) end)
    ]).

%% An exception's class and reason, without its stack.
caught(Fun) ->
    try Fun() of
        Value -> Value
    catch
        throw:Thrown -> {throw, Thrown};
        error:Reason -> {error, Reason}
    end.

%% --- Opening ---

opening(Port) ->
    Log = fun(Level, Format, Args) -> io:format("log ~p ~ts", [Level, io_lib:format(Format, Args)]) end,
    Refused = refused_port(),
    show(open_options, [
        eldap:open(["localhost"], [{timeout, 0}]),
        eldap:open(["localhost"], [{port, x}]),
        eldap:open(["localhost"], [{version, 2}]),
        eldap:open(["localhost"], [{tcpopts, [{active, true}]}]),
        eldap:open(["localhost"], [{tcpopts, [binary]}]),
        eldap:open(["localhost"], [{tcpopts, x}])
    ]),
    show(open_refused, eldap:open(["localhost"], [{port, Refused}, {log, Log}])),
    show(open_none, eldap:open([], [{port, Port}])),
    {ok, H} = eldap:open(["localhost", "localhost"], [{port, Port}, {timeout, 1000}, {log, x}]),
    show(handle, [is_pid(H), lists:member(H, links(self()))]),
    show(info, maps:get(socket_type, eldap:info(H))),
    show(getopts, [
        mask_port(eldap:getopts(H, [port, timeout, log, ssl])),
        eldap:getopts(H, [active]),
        eldap:getopts(H, [])
    ]),
    Owner = spawn(fun() ->
        receive
            stop -> ok
        end
    end),
    show(controlling_process, eldap:controlling_process(H, Owner)),
    show(links, [lists:member(H, links(self())), lists:member(Owner, links(self())), lists:member(H, links(Owner))]),
    unlink(Owner),
    Owner ! stop,
    Closes = [eldap:close(H), eldap:close(H)],
    timer:sleep(100),
    show(close_twice, Closes),
    show(closed, is_process_alive(H)).

links(Pid) ->
    element(2, process_info(Pid, links)).

mask_port({ok, [{port, _} | Rest]}) -> {ok, [{port, port} | Rest]};
mask_port(Other) -> Other.

%% A local port nothing listens on.
refused_port() ->
    {ok, Listen} = gen_tcp:listen(0, []),
    {ok, Port} = inet:port(Listen),
    gen_tcp:close(Listen),
    Port.

%% --- Binds ---

binds(H) ->
    show(bind, eldap:simple_bind(H, "cn=admin", "secret")),
    show(bind_bad, eldap:simple_bind(H, "cn=admin", "wrong")),
    show(bind_anonymous, eldap:simple_bind(H, "", "")),
    show(bind_no_password, eldap:simple_bind(H, "cn=x", "")),
    show(bind_referral, eldap:simple_bind(H, "cn=referral", "x")),
    show(bind_sasl, eldap:simple_bind(H, "cn=sasl", "x")),
    show(bind_wrong_op, eldap:simple_bind(H, "cn=wrongop", "x")),
    show(bind_controls, eldap:simple_bind(H, "cn=admin", "secret", [{control, "1.2", false, "v"}])).

%% --- Searches ---

searches(H) ->
    F = eldap:'and'([eldap:equalityMatch("uid", "alice"), eldap:substrings("cn", [{any, "li"}])]),
    show(search, eldap:search(H, [{base, "dc=example,dc=com"}, {filter, F}, {attributes, ["cn", "mail"]}])),
    show(search_record, eldap:search(H, #eldap_search{base = "dc=x", filter = eldap:present("objectClass")})),
    show(
        search_options,
        eldap:search(H, [
            {base, "dc=y"},
            {filter, eldap:present("a")},
            {scope, eldap:singleLevel()},
            {deref, eldap:neverDerefAliases()},
            {types_only, true},
            {timeout, 5},
            {size_limit, 7},
            {timeout, 6}
        ])
    ),
    All = [{filter, eldap:'or'([eldap:approxMatch("a", "b"), eldap:'not'(eldap:present("c"))])}],
    show(
        search_filters,
        eldap:search(
            H,
            [{base, "dc=filters"} | All] ++
                [
                    {filter, eldap:extensibleMatch("v", [{type, "sn"}, {matchingRule, "r"}, {dnAttributes, true}])}
                ]
        )
    ),
    show(search_none, eldap:search(H, [{base, "dc=none"}, {filter, eldap:present("a")}])),
    show(search_referrals, eldap:search(H, [{base, "dc=refs"}, {filter, eldap:present("a")}])),
    show(search_referral, eldap:search(H, [{base, "dc=referral"}, {filter, eldap:present("a")}])),
    show(search_size_limit, eldap:search(H, [{base, "code=4"}, {filter, eldap:present("a")}])),
    show(search_codes, [
        eldap:search(H, [{base, "code=" ++ integer_to_list(C)}, {filter, eldap:present("a")}])
     || C <- [1, 3, 11, 80, 99]
    ]),
    show(search_errors, [
        eldap:search(H, [{filter, eldap:present("a")}]),
        eldap:search(H, [{base, "dc=x"}]),
        eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}, {scope, bogus}]),
        eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}, {types_only, x}]),
        eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}, {size_limit, -1}]),
        eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}, {size_limit, x}]),
        eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}, {timeout, -1}]),
        eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}, {attributes, [1]}]),
        eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}, {bogus, 1}]),
        eldap:search(H, #eldap_search{base = "x", filter = x, scope = y}),
        eldap:search(H, #eldap_search{base = "x", filter = x, size_limit = -1, timeout = -1}),
        eldap:search(H, #eldap_search{base = "x", filter = x, types_only = w, attributes = [1]})
    ]),
    Paged = eldap:search(H, [{base, "dc=paged"}, {filter, eldap:present("a")}], [eldap:paged_result_control(10)]),
    show(search_paged, Paged),
    {ok, Result} = Paged,
    show(cookie, eldap:paged_result_cookie(Result)),
    {ok, Cookie} = eldap:paged_result_cookie(Result),
    show(
        search_next_page,
        eldap:search(
            H,
            [{base, "dc=paged"}, {filter, eldap:present("a")}],
            [eldap:paged_result_control(10, Cookie)]
        )
    ).

%% --- Updates ---

updates(H) ->
    show(add, eldap:add(H, "cn=new,dc=x", [{"objectclass", ["person"]}, {"cn", ["new"]}])),
    show(add_exists, eldap:add(H, "cn=exists,dc=x", [{"cn", ["e"]}])),
    show(add_bad, caught(fun() -> eldap:add(H, "cn=new,dc=x", [x]) end)),
    show(add_controls, eldap:add(H, "cn=new,dc=x", [{"a", ["1", "2"]}], [eldap:paged_result_control(1)])),
    show(
        modify,
        eldap:modify(H, "cn=new,dc=x", [
            eldap:mod_replace("mail", ["a@b"]),
            eldap:mod_add("d", ["x"]),
            eldap:mod_delete("e", [])
        ])
    ),
    show(modify_denied, eldap:modify(H, "cn=denied", [eldap:mod_add("a", ["1"])])),
    show(delete, eldap:delete(H, "cn=new,dc=x")),
    show(delete_none, eldap:delete(H, "cn=none,dc=x")),
    show(delete_referral, eldap:delete(H, "cn=referral")),
    show(delete_controls, eldap:delete(H, "cn=x", [{control, "1.2", true, asn1_NOVALUE}])),
    show(modify_dn, eldap:modify_dn(H, "cn=a,dc=x", "cn=b", true, "")),
    show(modify_dn_superior, eldap:modify_dn(H, "cn=a,dc=x", "cn=b", false, "ou=y")),
    show(modify_dn_violation, eldap:modify_dn(H, "cn=violation", "cn=b", true, "")),
    show(passwd, eldap:modify_password(H, "uid=alice", "newpw")),
    show(passwd_generated, eldap:modify_password(H, "uid=alice", "")),
    show(passwd_old, eldap:modify_password(H, "uid=alice", "newpw", "oldpw")),
    show(passwd_self, eldap:modify_password(H, "", "", "")),
    show(passwd_refused, eldap:modify_password(H, "uid=refused", "x")),
    show(passwd_referral, eldap:modify_password(H, "uid=referral", "x")).

%% --- Closing and lost connections ---

closing(H) ->
    closed(H),
    show(alive, is_process_alive(H)).

lost(Port) ->
    {ok, H} = eldap:open(["localhost"], [{port, Port}]),
    show(lost_bind, eldap:simple_bind(H, "cn=close", "x")),
    show(lost_delete, eldap:delete(H, "cn=x")),
    show(lost_search, eldap:search(H, [{base, "dc=x"}, {filter, eldap:present("a")}])),
    show(lost_alive, is_process_alive(H)),
    closed(H).

%% close/1 does not wait: the unbind request prints before the next step.
closed(H) ->
    ok = eldap:close(H),
    timer:sleep(100).

%% A late answer is read as the next request's: OTP's eldap does not match message identifiers. The answers come
%% from earlier requests, so each result waits for the server to print the request it just sent.
timeouts(Port) ->
    {ok, H} = eldap:open(["localhost"], [{port, Port}, {timeout, 1000}]),
    show(slow_search, eldap:search(H, [{base, "dc=slow"}, {filter, eldap:present("a")}])),
    settled(late_bind, eldap:simple_bind(H, "cn=admin", "secret")),
    settled(stale_bind, eldap:simple_bind(H, "cn=admin", "secret")),
    settled(next_bind, eldap:simple_bind(H, "cn=admin", "secret")),
    % The server closes instead of answering, so no answer is left unread at close (which could reset the connection).
    settled(last_bind, eldap:simple_bind(H, "cn=close", "x")),
    closed(H).

settled(Name, Value) ->
    timer:sleep(300),
    show(Name, Value).

logged(Port) ->
    Log = fun(Level, Format, Args) -> io:format("log ~p ~p ~p~n", [Level, Format, Args]) end,
    {ok, H} = eldap:open(["localhost"], [{port, Port}, {log, Log}, {anon_auth, true}]),
    show(log_bind, eldap:simple_bind(H, "", "")),
    show(log_add, eldap:add(H, "cn=x", [{"a", ["1"]}])),
    show(log_modify, eldap:modify(H, "cn=x", [eldap:mod_add("a", ["1"])])),
    show(log_delete, eldap:delete(H, "cn=x")),
    show(log_modify_dn, eldap:modify_dn(H, "cn=x", "cn=y", true, "")),
    show(log_passwd, eldap:modify_password(H, "cn=x", "p")),
    show(log_search, eldap:search(H, [{base, "dc=refs"}, {filter, eldap:present("a")}])),
    closed(H).

%% --- TLS ---

tls() ->
    ok = ssl:start(),
    AltName = #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = [{dNSName, "localhost"}], critical = false},
    Config = public_key:pkix_test_data(#{
        root => [{key, {namedCurve, secp256r1}}, {digest, sha256}],
        intermediates => [],
        peer => [{key, {namedCurve, secp256r1}}, {digest, sha256}, {extensions, [AltName]}]
    }),
    Server = [{cert, proplists:get_value(cert, Config)}, {key, proplists:get_value(key, Config)}, {log_level, none}],
    Client = [{verify, verify_none}, {log_level, none}],
    Port = server(fun(Request) -> tls_answer(Request, Server) end, none),
    {ok, H} = eldap:open(["localhost"], [{port, Port}]),
    show(start_tls_refused, eldap:start_tls(H, Client)),
    show(start_tls, eldap:start_tls(H, Client, 5000)),
    show(start_tls_again, eldap:start_tls(H, Client)),
    show(tls_info, [maps:get(socket_type, eldap:info(H)), eldap:getopts(H, [ssl])]),
    show(tls_bind, eldap:simple_bind(H, "cn=admin", "secret")),
    closed(H),
    LdapsPort = server(fun(Request) -> tls_answer(Request, Server) end, Server),
    {ok, L} = eldap:open(["localhost"], [{port, LdapsPort}, {ssl, true}, {sslopts, Client}]),
    show(ldaps_info, [maps:get(socket_type, eldap:info(L)), eldap:getopts(L, [ssl])]),
    show(ldaps_bind, eldap:simple_bind(L, "cn=admin", "secret")),
    show(ldaps_search, eldap:search(L, [{base, "dc=x"}, {filter, eldap:present("a")}])),
    show(ldaps_start_tls, eldap:start_tls(L, Client)),
    closed(L).

tls_answer({seq, [{int, Id}, {app, 23, [{ctx, 0, "1.3.6.1.4.1.1466.20037"}]} | _]}, Server) ->
    case get(start_tls) of
        undefined ->
            put(start_tls, refused),
            [message(Id, extended(2, none, none))];
        refused ->
            [message(Id, extended(0, "1.3.6.1.4.1.1466.20037", none)), {tls, Server}]
    end;
tls_answer(Request, _) ->
    answer(Request).

%% --- The server's answers, by request ---

answer({seq, [{int, Id}, Op | _]}) ->
    answer(Id, Op);
answer(_) ->
    [].

answer(Id, {app, 0, [_, "cn=admin", {ctx, 0, "secret"}]}) -> [message(Id, result(1, 0, ""))];
answer(Id, {app, 0, [_, "cn=admin", {ctx, 0, _}]}) -> [message(Id, result(1, 49, "bad password"))];
answer(Id, {app, 0, [_, "cn=referral" | _]}) -> [message(Id, result(1, 10, "", referral(["ldap://a/", "ldap://b/"])))];
answer(Id, {app, 0, [_, "cn=sasl" | _]}) -> [message(Id, result(1, 14, ""))];
answer(Id, {app, 0, [_, "cn=wrongop" | _]}) -> [message(Id, result(7, 0, ""))];
answer(_, {app, 0, [_, "cn=close" | _]}) -> [close];
answer(Id, {app, 0, _}) -> [message(Id, result(1, 0, ""))];
answer(Id, {app, 3, [Base | _]}) -> search_answer(Id, Base);
answer(Id, {app, 8, ["cn=exists,dc=x" | _]}) -> [message(Id, result(9, 68, "exists"))];
answer(Id, {app, 8, _}) -> [message(Id, result(9, 0, ""))];
answer(Id, {app, 6, ["cn=denied" | _]}) -> [message(Id, result(7, 50, "no access"))];
answer(Id, {app, 6, _}) -> [message(Id, result(7, 0, ""))];
answer(Id, {app, 10, "cn=none,dc=x"}) -> [message(Id, result(11, 32, ""))];
answer(Id, {app, 10, "cn=referral"}) -> [message(Id, result(11, 10, "", referral(["ldap://d/"])))];
answer(Id, {app, 10, _}) -> [message(Id, result(11, 0, ""))];
answer(Id, {app, 12, ["cn=violation" | _]}) -> [message(Id, result(13, 64, ""))];
answer(Id, {app, 12, _}) -> [message(Id, result(13, 0, ""))];
answer(Id, {app, 23, [_, {ctx, 1, Value}]}) -> passwd_answer(Id, Value);
answer(_, _) -> [].

search_answer(Id, "dc=none") ->
    [message(Id, result(5, 32, "no such"))];
search_answer(Id, "dc=refs") ->
    [
        message(Id, entry("cn=a", [{"x", ["1"]}])),
        message(Id, refs(["ldap://r/", "ldap://s/"])),
        message(Id, refs(["ldap://t/"])),
        message(Id, result(5, 0, ""))
    ];
search_answer(Id, "dc=referral") ->
    [message(Id, result(5, 10, "", referral(["ldap://e/"])))];
search_answer(Id, "code=" ++ Code) ->
    [message(Id, entry("cn=e", [])), message(Id, result(5, list_to_integer(Code), "d"))];
search_answer(Id, "dc=paged") ->
    Value = seq([int(0), octets("cookie1")]),
    Control = seq([octets("1.2.840.113556.1.4.319"), octets(Value)]),
    [message(Id, entry("cn=p", [])), message(Id, result(5, 0, ""), [Control])];
search_answer(Id, "dc=slow") ->
    [{sleep, 1500}, message(Id, result(5, 0, ""))];
search_answer(Id, _) ->
    [
        message(Id, entry("uid=alice,dc=example,dc=com", [{"cn", ["Alice", "Ali"]}, {"mail", ["a@x"]}])),
        message(Id, entry("uid=bob,dc=example,dc=com", [])),
        message(Id, result(5, 0, ""))
    ].

%% A generated password unless a new one was given ([2] in the request value).
passwd_answer(Id, Value) ->
    case {string:find(Value, "refused"), string:find(Value, "referral"), lists:member(16#82, Value)} of
        {nomatch, nomatch, false} -> [message(Id, extended(0, none, seq([tlv(16#80, "genpw")])))];
        {nomatch, nomatch, true} -> [message(Id, extended(0, none, none))];
        {_, nomatch, _} -> [message(Id, extended(53, none, none))];
        _ -> [message(Id, tlv(16#78, [enum(10), octets(""), octets(""), referral(["ldap://p/"])]))]
    end.

%% --- BER answers ---

tlv(Tag, Parts) ->
    Content = iolist_to_binary(Parts),
    [Tag, length_bytes(byte_size(Content)), Content].

length_bytes(Length) when Length < 128 -> Length;
length_bytes(Length) ->
    Bytes = binary:encode_unsigned(Length),
    [128 + byte_size(Bytes), Bytes].

int(I) -> tlv(2, binary:encode_unsigned(I)).
enum(I) -> tlv(10, [I]).
octets(S) -> tlv(4, S).
seq(Parts) -> tlv(16#30, Parts).
set(Parts) -> tlv(16#31, Parts).

message(Id, Op) -> iolist_to_binary(seq([int(Id), Op])).
message(Id, Op, Controls) -> iolist_to_binary(seq([int(Id), Op, tlv(16#a0, Controls)])).

%% An LDAPResult under the application tag of its response.
result(App, Code, Diagnostic) -> tlv(16#60 + App, [enum(Code), octets(""), octets(Diagnostic)]).
result(App, Code, Diagnostic, Extra) -> tlv(16#60 + App, [enum(Code), octets(""), octets(Diagnostic), Extra]).

referral(Uris) -> tlv(16#a3, [octets(U) || U <- Uris]).
entry(Dn, Attrs) -> tlv(16#64, [octets(Dn), seq([seq([octets(T), set([octets(V) || V <- Vs])]) || {T, Vs} <- Attrs])]).
refs(Uris) -> tlv(16#73, [octets(U) || U <- Uris]).

extended(Code, Name, Value) ->
    Optional = [tlv(16#8a, Name) || Name =/= none] ++ [tlv(16#8b, Value) || Value =/= none],
    tlv(16#78, [enum(Code), octets(""), octets(""), Optional]).

%% --- The server ---

%% Listens on a free port; each connection prints its requests, numbered by connection, and sends Answer(Request)'s
%% messages. Requests print before their answers are sent, so the output keeps the client's order.
server(Answer, Tls) ->
    {ok, Listen} = gen_tcp:listen(0, [binary, {active, false}, {reuseaddr, true}]),
    {ok, Port} = inet:port(Listen),
    Pid = spawn(fun() -> accept(Listen, Answer, Tls, 1) end),
    ok = gen_tcp:controlling_process(Listen, Pid),
    Port.

accept(Listen, Answer, Tls, N) ->
    {ok, Socket} = gen_tcp:accept(Listen),
    Pid = spawn(fun() ->
        receive
            go -> start_connection(Socket, Answer, Tls, N)
        end
    end),
    ok = gen_tcp:controlling_process(Socket, Pid),
    Pid ! go,
    accept(Listen, Answer, Tls, N + 1).

start_connection(Socket, Answer, none, N) ->
    connection({gen_tcp, Socket}, Answer, N, <<>>);
start_connection(Socket, Answer, Tls, N) ->
    {ok, TlsSocket} = ssl:handshake(Socket, Tls, 5000),
    connection({ssl, TlsSocket}, Answer, N, <<>>).

connection({Transport, Socket} = T, Answer, N, Buffer) ->
    case erlang:decode_packet(asn1, Buffer, []) of
        {ok, Message, Rest} ->
            [Tree] = tree(Message),
            io:format("[~p] ~p~n", [N, Tree]),
            connection(act(Answer(Tree), T), Answer, N, Rest);
        {more, _} ->
            case Transport:recv(Socket, 0) of
                {ok, Data} -> connection(T, Answer, N, <<Buffer/binary, Data/binary>>);
                {error, _} -> ok
            end
    end.

act([], T) ->
    T;
act([close | _], {Transport, Socket}) ->
    Transport:close(Socket),
    {Transport, Socket};
act([{sleep, Ms} | More], T) ->
    timer:sleep(Ms),
    act(More, T);
act([{tls, Options} | _], {gen_tcp, Socket}) ->
    {ok, TlsSocket} = ssl:handshake(Socket, Options, 5000),
    {ssl, TlsSocket};
act([Bytes | More], {Transport, Socket} = T) ->
    ok = Transport:send(Socket, Bytes),
    act(More, T).

%% BER values as a readable tree.
tree(<<>>) ->
    [];
tree(Bytes) ->
    {ok, Value, Rest} = erlang:decode_packet(asn1, Bytes, []),
    [tree_node(Value) | tree(Rest)].

tree_node(Value) ->
    <<Class:2, Constructed:1, Number:5, _/binary>> = Value,
    tree_node(Class, Constructed, Number, content(Value)).

content(<<_, Length, Content:Length/binary>>) when Length < 128 ->
    Content;
content(<<_, 1:1, Size:7, Rest/binary>>) ->
    <<Length:Size/unit:8, Content:Length/binary>> = Rest,
    Content.

tree_node(0, 0, 1, <<V>>) -> {bool, V =/= 0};
tree_node(0, 0, 2, C) -> {int, binary:decode_unsigned(C)};
tree_node(0, 0, 4, C) -> binary_to_list(C);
tree_node(0, 0, 5, <<>>) -> null;
tree_node(0, 0, 10, C) -> {enum, binary:decode_unsigned(C)};
tree_node(0, 1, 16, C) -> {seq, tree(C)};
tree_node(0, 1, 17, C) -> {set, tree(C)};
tree_node(1, 1, N, C) -> {app, N, tree(C)};
tree_node(1, 0, N, C) -> {app, N, binary_to_list(C)};
tree_node(2, 1, N, C) -> {ctx, N, tree(C)};
tree_node(2, 0, N, C) -> {ctx, N, binary_to_list(C)}.
