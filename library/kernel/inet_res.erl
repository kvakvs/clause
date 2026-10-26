-module(inet_res).
-export([
    dns_msg/1,
    getbyname/2,
    getbyname/3,
    getbyname/4,
    getbyname_tm/3,
    getbyname_tm/4,
    gethostbyaddr/1,
    gethostbyaddr/2,
    gethostbyaddr/3,
    gethostbyaddr_tm/2,
    gethostbyaddr_tm/3,
    gethostbyname/1,
    gethostbyname/2,
    gethostbyname/3,
    gethostbyname/4,
    gethostbyname_tm/3,
    gethostbyname_tm/4,
    lookup/3,
    lookup/4,
    lookup/5,
    nnslookup/4,
    nnslookup/5,
    nslookup/3,
    nslookup/4,
    resolve/3,
    resolve/4,
    resolve/5
]).
-include("inet_res.types.hrl").

% Clause's inet_res (plan 21 step LIB60B, docs/libraries/library.md): OTP's DNS client in Erlang over gen_udp and gen_tcp, its
% messages encoded and decoded by the clause_net DNS codec into OTP's #dns_rec{} records. Name servers come from
% the resolver configuration (clause_inet_config) or the nameservers option; answers are not cached.

-record(dns_header, {id = 0, qr = 0, opcode = 0, aa = 0, tc = 0, rd = 0, ra = 0, pr = 0, rcode = 0}).
-record(dns_rec, {header, qdlist = [], anlist = [], nslist = [], arlist = []}).
-record(dns_query, {domain, type, class, unicast_response = false}).
-record(dns_rr, {domain = "", type = any, class = in, cnt = 0, ttl = 0, data = [], tm, bm = "", func = false}).
-record(dns_rr_opt, {
    domain = "", type = opt, udp_payload_size = 1280, ext_rcode = 0, version = 0, z = 0, data = [], do = false
}).

% A UDP query without EDNS fits in 512 bytes.
-define(PACKET_SIZE, 512).

%% --- Resolver options ---

% The options of a call over the configured defaults, as a map; badarg for an unknown or invalid one. An atom Opt
% is {Opt, true}, noOpt {Opt, false}; the nameservers option alone leaves no alternative name servers.
options(Options0) ->
    Options1 = [expand_option(Option) || Option <- Options0],
    Options2 =
        case {lists:keymember(nameservers, 1, Options1), lists:keymember(alt_nameservers, 1, Options1)} of
            {true, false} -> [{alt_nameservers, []} | Options1];
            _ -> Options1
        end,
    Options = lists:ukeysort(1, Options2),
    case lists:all(fun valid_option/1, Options) of
        true -> maps:merge(defaults(), maps:from_list(Options));
        false -> erlang:error(badarg, [Options0])
    end.

expand_option(Option) when is_atom(Option) ->
    case atom_to_list(Option) of
        "no" ++ Name -> {list_to_atom(Name), false};
        _ -> {Option, true}
    end;
expand_option(Option) ->
    Option.

defaults() ->
    #{nameservers := Servers} = clause_inet_config:config(),
    #{
        alt_nameservers => [],
        dnssec_ok => false,
        edns => false,
        inet6 => false,
        nameservers => Servers,
        nxdomain_reply => false,
        random => true,
        recurse => true,
        retry => 3,
        servfail_retry_timeout => 1500,
        timeout => 2000,
        udp_payload_size => 1280,
        usevc => false,
        verbose => false
    }.

valid_option({Name, Servers}) when Name =:= nameservers; Name =:= alt_nameservers ->
    is_list(Servers) andalso lists:all(fun valid_server/1, Servers);
valid_option({recurse, R}) ->
    is_boolean(R) orelse R =:= 0 orelse R =:= 1;
valid_option({retry, N}) ->
    is_integer(N) andalso N > 0;
valid_option({servfail_retry_timeout, T}) ->
    is_integer(T) andalso T >= 0;
valid_option({timeout, T}) ->
    is_integer(T) andalso T > 0;
valid_option({edns, V}) ->
    V =:= false orelse V =:= 0;
valid_option({udp_payload_size, S}) ->
    is_integer(S) andalso S >= 512;
valid_option({Name, Flag}) when
    Name =:= inet6; Name =:= usevc; Name =:= random; Name =:= dnssec_ok; Name =:= nxdomain_reply; Name =:= verbose
->
    is_boolean(Flag);
valid_option(_) ->
    false.

valid_server({{A, B, C, D}, Port}) -> valid_bytes([A, B, C, D], 255) andalso valid_port(Port);
valid_server({{_, _, _, _, _, _, _, _} = IP, Port}) -> valid_bytes(tuple_to_list(IP), 65535) andalso valid_port(Port);
valid_server(_) -> false.

valid_bytes(Parts, Max) -> lists:all(fun(P) -> is_integer(P) andalso P >= 0 andalso P =< Max end, Parts).

valid_port(Port) -> is_integer(Port) andalso Port band 65535 =:= Port.

%% --- Queries ---

resolve(Name, Class, Type) -> resolve(Name, Class, Type, [], infinity).

resolve(Name, Class, Type, Options) -> resolve(Name, Class, Type, Options, infinity).

%% The reply of the name servers to a query: {ok, Msg}, or an error with the reply that carried it.
resolve(Name, Class, Type, Options, Timeout) ->
    case query_name(Name) of
        {ok, Domain} ->
            Timer = inet:start_timer(Timeout),
            Result = query(Domain, Class, Type, options(Options), Timer),
            _ = inet:stop_timer(Timer),
            Result;
        {error, _} = Error ->
            Error
    end.

lookup(Name, Class, Type) -> lookup(Name, Class, Type, []).

lookup(Name, Class, Type, Options) -> lookup(Name, Class, Type, Options, infinity).

%% The data of the answers of Class and Type; [] on any error.
lookup(Name, Class, Type, Options, Timeout) ->
    case resolve(Name, Class, Type, Options, Timeout) of
        {ok, #dns_rec{anlist = Answers}} ->
            [
                Data
             || #dns_rr{class = C, type = T, data = Data} <- Answers,
                Class =:= any orelse C =:= Class,
                Type =:= any orelse T =:= Type
            ];
        {error, _} ->
            []
    end.

nslookup(Name, Class, Type) -> plain_lookup(Name, Class, Type, [], infinity).

nslookup(Name, Class, Type, Timeout) when is_integer(Timeout), Timeout >= 0; Timeout =:= infinity ->
    plain_lookup(Name, Class, Type, [], Timeout);
nslookup(Name, Class, Type, Servers) ->
    nnslookup(Name, Class, Type, Servers).

nnslookup(Name, Class, Type, Servers) -> nnslookup(Name, Class, Type, Servers, infinity).

nnslookup(Name, Class, Type, Servers, Timeout) ->
    plain_lookup(Name, Class, Type, [{nameservers, Servers}], Timeout).

plain_lookup(Name, Class, Type, Options, Timeout) ->
    plain_error(resolve(Name, Class, Type, Options, Timeout)).

% Server errors without their message; a query format error is einval.
plain_error({error, {qfmterror, _}}) -> {error, einval};
plain_error({error, {Reason, _}}) -> {error, Reason};
plain_error(Result) -> Result.

% The domain of a query: a name, or the reverse domain of an address.
query_name(Name) when is_atom(Name) ->
    query_name(atom_to_list(Name));
query_name(Name) when is_list(Name) ->
    case visible(Name) of
        true ->
            case inet:parse_address(Name) of
                {ok, IP} -> reverse_domain(IP);
                _ -> {ok, Name}
            end;
        false ->
            {error, formerr}
    end;
query_name(IP) ->
    reverse_domain(IP).

visible(Name) -> lists:all(fun(C) -> is_integer(C) andalso C >= 16#21 andalso C =< 16#7e end, Name).

% in-addr.arpa and ip6.arpa domains of an address.
reverse_domain({_, _, _, _} = IP) ->
    case valid_bytes(tuple_to_list(IP), 255) of
        true ->
            {ok, lists:append([integer_to_list(X) ++ "." || X <- lists:reverse(tuple_to_list(IP))]) ++ "in-addr.arpa"};
        false ->
            {error, formerr}
    end;
reverse_domain({_, _, _, _, _, _, _, _} = IP) ->
    case valid_bytes(tuple_to_list(IP), 16#ffff) of
        true ->
            Nibbles = lists:append([[(W bsr S) band 15 || S <- [12, 8, 4, 0]] || W <- tuple_to_list(IP)]),
            {ok, lists:append([[hex(N), $.] || N <- lists:reverse(Nibbles)]) ++ "ip6.arpa"};
        false ->
            {error, formerr}
    end;
reverse_domain(_) ->
    {error, formerr}.

hex(N) when N < 10 -> $0 + N;
hex(N) -> $a + N - 10.

% Ask the name servers, then the alternative ones when the answer is nxdomain or empty.
query(Domain, Class, Type, #{nameservers := Servers, alt_nameservers := Alternatives} = Options, Timer) ->
    case Servers =:= [] andalso Alternatives =:= [] of
        true ->
            {error, nxdomain};
        false ->
            Query = {Domain, Class, Type},
            case ask(Query, Servers, Options, Timer) of
                {error, nxdomain} = Error -> ask_alternatives(Query, Alternatives, Options, Timer, Error);
                {error, {nxdomain, _}} = Error -> ask_alternatives(Query, Alternatives, Options, Timer, Error);
                {ok, #dns_rec{anlist = []}} = Reply -> ask_alternatives(Query, Alternatives, Options, Timer, Reply);
                Reply -> Reply
            end
    end.

ask_alternatives(_, [], _, _, Result) -> Result;
ask_alternatives(Query, Servers, Options, Timer, _) -> ask(Query, Servers, Options, Timer).

% Each name server in turn, Retry rounds; a failure is a time-out unless a server says otherwise.
ask(_, [], _, _) ->
    {error, nxdomain};
ask(Query, Servers, Options, Timer) ->
    lists:all(fun(Server) -> tuple_size(Server) =:= 2 end, Servers) orelse
        erlang:error(badarg, [Query, Servers, Timer]),
    round(Query, Servers, Options, Timer, 0, timeout).

round(_, [], Options, _, _, Reason) ->
    final_error(Reason, Options);
round(_, _, #{retry := Retry} = Options, _, Round, Reason) when Round =:= Retry ->
    final_error(Reason, Options);
round(Query, Servers, Options, Timer, Round, Reason) ->
    servers(Query, Servers, Options, Timer, Round, Reason, []).

% Ask the servers of this round; Again collects the servers worth asking in the next one.
servers(Query, [], Options, Timer, Round, Reason, Again) ->
    round(Query, lists:reverse(Again), Options, Timer, Round + 1, Reason);
servers(Query, [Server0 | Servers], Options, Timer, Round, Reason, Again) ->
    Server = wait_servfail(Server0),
    case ask_server(Query, Server, Options, Timer, Round) of
        {ok, _} = Reply ->
            Reply;
        timeout ->
            final_error(Reason, Options);
        {error, {nxdomain, _}} = Error ->
            final_error(element(2, Error), Options);
        {error, {E, _} = NewReason} when
            E =:= qfmterror; E =:= notimp; E =:= refused; E =:= badvers; E =:= unknown
        ->
            servers(Query, Servers, Options, Timer, Round, NewReason, Again);
        {error, E} when E =:= formerr; E =:= enetunreach; E =:= econnrefused ->
            servers(Query, Servers, Options, Timer, Round, E, Again);
        {error, timeout} ->
            servers(Query, Servers, Options, Timer, Round, Reason, [Server | Again]);
        {error, {servfail, _} = NewReason} ->
            servers(Query, Servers, Options, Timer, Round, NewReason, servfail_again(Server, Options, Timer, Again));
        {error, NewReason} ->
            servers(Query, Servers, Options, Timer, Round, NewReason, [Server | Again])
    end.

% A server that failed is asked again after servfail_retry_timeout, if the call has that much time left.
servfail_again(Server, #{servfail_retry_timeout := Wait}, Timer, Again) ->
    case inet:timeout(Wait, Timer) of
        Wait -> [{servfail_retry, erlang:monotonic_time(millisecond) + Wait, Server} | Again];
        _ -> Again
    end.

wait_servfail({servfail_retry, Deadline, Server}) ->
    receive
    after max(0, Deadline - erlang:monotonic_time(millisecond)) -> Server
    end;
wait_servfail(Server) ->
    Server.

final_error({nxdomain, _}, #{nxdomain_reply := false}) -> {error, nxdomain};
final_error(Reason, _) -> {error, Reason}.

% One server: EDNS first when enabled, plain DNS again when the server rejects EDNS.
ask_server(Query, Server, #{edns := false} = Options, Timer, Round) ->
    send_query(message(Query, Options, false), Server, Options, Timer, Round, ?PACKET_SIZE);
ask_server(Query, Server, #{edns := Version, udp_payload_size := Size} = Options, Timer, Round) ->
    case send_query(message(Query, Options, Version), Server, Options, Timer, Round, Size) of
        {error, {E, _}} when E =:= qfmterror; E =:= notimp; E =:= servfail; E =:= badvers ->
            send_query(message(Query, Options, false), Server, Options, Timer, Round, ?PACKET_SIZE);
        Result ->
            Result
    end.

% The query message and its encoding, with the next transaction id.
message({Domain, Class, Type}, #{recurse := Recurse} = Options, Edns) ->
    Additional =
        case Edns of
            false ->
                [];
            _ ->
                #{udp_payload_size := Size, dnssec_ok := DnssecOk} = Options,
                [#dns_rr_opt{udp_payload_size = Size, version = Edns, do = DnssecOk}]
        end,
    Message = #dns_rec{
        header = #dns_header{
            id = next_id(),
            qr = false,
            opcode = 'query',
            rd = Recurse =:= true orelse Recurse =:= 1,
            rcode = 0
        },
        qdlist = [#dns_query{domain = Domain, type = Type, class = Class}],
        arlist = Additional
    },
    verbose(Options, "Query: ~p~n", [dns_msg(Message)]),
    {Message, clause_net:dns_encode(Message)}.

% Transaction ids count up from 1 and wrap at 16 bits, as OTP's resolver numbers queries without the crypto
% application.
next_id() ->
    Counter =
        case persistent_term:get({?MODULE, ids}, undefined) of
            undefined ->
                New = atomics:new(1, [{signed, false}]),
                persistent_term:put({?MODULE, ids}, New),
                New;
            Existing ->
                Existing
        end,
    atomics:add_get(Counter, 1, 1) rem 65536.

% UDP unless usevc is set or the query is too large; a truncated UDP reply is asked again over TCP.
send_query(
    {_, Bytes} = Query,
    Server,
    #{usevc := UseVC, timeout := Timeout, retry := Retry} = Options,
    Timer,
    Round,
    Size
) ->
    case UseVC orelse byte_size(Bytes) > Size of
        true ->
            tcp_query(Query, Server, inet:timeout(Timeout * 5, Timer), Options);
        false ->
            case udp_query(Query, Server, inet:timeout((Timeout * (1 bsl Round)) div Retry, Timer), Options) of
                {ok, #dns_rec{header = #dns_header{tc = true}}} ->
                    tcp_query(Query, Server, inet:timeout(Timeout * 5, Timer), Options);
                Result ->
                    Result
            end
    end.

udp_query(_, _, 0, _) ->
    timeout;
udp_query({Message, Bytes}, {IP, Port}, Timeout, Options) ->
    verbose(Options, "Try UDP server : ~p:~p (timeout=~w)~n", [IP, Port, Timeout]),
    case gen_udp:open(0, [{active, false}, binary, family(IP)]) of
        {ok, Socket} ->
            try
                case gen_udp:connect(Socket, IP, Port) of
                    ok -> udp_exchange(Socket, Message, Bytes, {IP, Port}, Timeout, Options);
                    {error, _} = Error -> Error
                end
            after
                gen_udp:close(Socket)
            end;
        {error, _} = Error ->
            Error
    end.

udp_exchange(Socket, Message, Bytes, Server, Timeout, Options) ->
    case gen_udp:send(Socket, Bytes) of
        ok -> udp_receive(Socket, Message, Server, erlang:monotonic_time(millisecond) + Timeout, Options);
        {error, _} = Error -> Error
    end.

% Replies from the server until one answers the query; others (wrong id or question) are skipped.
udp_receive(Socket, Message, {IP, Port} = Server, Deadline, Options) ->
    Left = max(0, Deadline - erlang:monotonic_time(millisecond)),
    case gen_udp:recv(Socket, 0, Left) of
        {ok, {IP, Port, Reply}} ->
            case reply(Reply, Message, Options) of
                {error, Skip} when Skip =:= badid; Skip =:= unknown; Skip =:= noquery ->
                    udp_receive(Socket, Message, Server, Deadline, Options);
                Result ->
                    Result
            end;
        {ok, _} ->
            udp_receive(Socket, Message, Server, Deadline, Options);
        {error, _} = Error ->
            Error
    end.

tcp_query(_, _, 0, _) ->
    timeout;
tcp_query({Message, Bytes}, {IP, Port}, Timeout, Options) ->
    verbose(Options, "Try TCP server : ~p:~p (timeout=~w)~n", [IP, Port, Timeout]),
    try gen_tcp:connect(IP, Port, [{active, false}, {packet, 2}, binary, family(IP)], Timeout) of
        {ok, Socket} ->
            Result =
                case gen_tcp:send(Socket, Bytes) of
                    ok -> tcp_reply(gen_tcp:recv(Socket, 0, Timeout), Message, Options);
                    Error -> Error
                end,
            gen_tcp:close(Socket),
            Result;
        Error ->
            Error
    catch
        _:_ -> {error, einval}
    end.

tcp_reply({ok, Reply}, Message, Options) ->
    case reply(Reply, Message, Options) of
        {error, badid} -> {error, servfail};
        Result -> Result
    end;
tcp_reply(Error, _, _) ->
    Error.

family({_, _, _, _}) -> inet;
family(_) -> inet6.

% A reply to the query: its id, flags and question checked as OTP's decode_reply checks them, and its rcode (with the
% EDNS extended rcode and a TSIG error) as an error.
reply(Bytes, #dns_rec{header = Header, qdlist = [Question]}, Options) ->
    case clause_net:dns_decode(Bytes) of
        {ok, #dns_rec{header = Reply} = Message} ->
            case answers(Header, Question, Message) of
                ok ->
                    verbose(Options, "Got reply size ~w: ~p~n", [byte_size(Bytes), dns_msg(Message)]),
                    rcode(Reply, Message);
                Error ->
                    Error
            end;
        {error, _} = Error ->
            Error
    end.

answers(
    #dns_header{id = Id, opcode = Opcode, rd = RD}, #dns_query{domain = Domain, type = Type, class = Class}, #dns_rec{
        header = Reply, qdlist = Questions
    }
) ->
    if
        Reply#dns_header.id =/= Id -> {error, badid};
        not Reply#dns_header.qr -> {error, unknown};
        Reply#dns_header.opcode =/= Opcode -> {error, unknown};
        Reply#dns_header.rd andalso not RD -> {error, unknown};
        true -> question(Questions, Domain, Type, Class)
    end.

question([#dns_query{domain = D, type = Type, class = Class}], Domain, Type, Class) ->
    case string:lowercase(D) =:= string:lowercase(Domain) of
        true -> ok;
        false -> {error, noquery}
    end;
question(_, _, _, _) ->
    {error, noquery}.

rcode(#dns_header{rcode = Code}, #dns_rec{arlist = Additional} = Message) ->
    Tsig =
        case lists:keyfind(dns_rr_tsig, 1, Additional) of
            false -> false;
            Rr when element(10, Rr) =:= 0 -> false;
            Rr -> element(10, Rr)
        end,
    Extended =
        case lists:keyfind(dns_rr_opt, 1, Additional) of
            false -> 0;
            #dns_rr_opt{ext_rcode = Ext} -> Ext
        end,
    case {Tsig, (Extended bsl 4) bor Code} of
        {false, 0} -> {ok, Message};
        {false, 16} -> {error, {badvers, Message}};
        {false, Number} -> {error, {rcode_name(Number), Message}};
        {Number, _} -> {error, {rcode_name(Number), Message}}
    end.

rcode_name(1) -> qfmterror;
rcode_name(2) -> servfail;
rcode_name(3) -> nxdomain;
rcode_name(4) -> notimp;
rcode_name(5) -> refused;
rcode_name(6) -> yxdomain;
rcode_name(7) -> yxrrset;
rcode_name(8) -> nxrrset;
rcode_name(9) -> notauth;
rcode_name(10) -> notzone;
rcode_name(16) -> badsig;
rcode_name(17) -> badkey;
rcode_name(18) -> badtime;
rcode_name(22) -> badtrunc;
rcode_name(_) -> unknown.

verbose(#{verbose := true}, Format, Args) -> io:format(Format, Args);
verbose(_, _, _) -> ok.

%% --- Host entries ---

gethostbyname(Name) ->
    case maps:get(inet6, defaults()) of
        true -> gethostbyname_tm(Name, inet6, [], false);
        false -> gethostbyname_tm(Name, inet, [], false)
    end.

gethostbyname(Name, Family) -> gethostbyname_tm(Name, Family, [], false).

gethostbyname(Name, Family, Timeout) -> gethostbyname(Name, Family, [], Timeout).

gethostbyname(Name, Family, Options, Timeout) ->
    Timer = inet:start_timer(Timeout),
    Result = gethostbyname_tm(Name, Family, Options, Timer),
    _ = inet:stop_timer(Timer),
    Result.

gethostbyname_tm(Name, Family, Timer) -> gethostbyname_tm(Name, Family, [], Timer).

gethostbyname_tm(Name, inet, Options, Timer) -> getbyname_tm(Name, a, Options, Timer);
gethostbyname_tm(Name, inet6, Options, Timer) -> getbyname_tm(Name, aaaa, Options, Timer);
gethostbyname_tm(_, _, _, _) -> {error, einval}.

getbyname(Name, Type) -> getbyname_tm(Name, Type, [], false).

getbyname(Name, Type, Timeout) -> getbyname(Name, Type, [], Timeout).

getbyname(Name, Type, Options, Timeout) ->
    Timer = inet:start_timer(Timeout),
    Result = getbyname_tm(Name, Type, Options, Timer),
    _ = inet:stop_timer(Timer),
    Result.

getbyname_tm(Name, Type, Timer) -> getbyname_tm(Name, Type, [], Timer).

%% A hostent of the answers of Type for Name, through the search list as OTP walks it.
getbyname_tm(Name, Type, Options, Timer) when is_list(Name) ->
    case lists:member(Type, host_types()) andalso visible(Name) of
        true -> by_name(Name, Type, options(Options), Timer);
        false -> {error, formerr}
    end;
getbyname_tm(Name, Type, Options, Timer) when is_atom(Name) ->
    getbyname_tm(atom_to_list(Name), Type, Options, Timer);
getbyname_tm(_, _, _, _) ->
    {error, formerr}.

host_types() ->
    [
        a,
        aaaa,
        mx,
        ns,
        md,
        mf,
        cname,
        soa,
        mb,
        mg,
        mr,
        null,
        wks,
        hinfo,
        txt,
        srv,
        naptr,
        spf,
        uinfo,
        uid,
        gid,
        uri,
        caa
    ].

% An absolute name alone; a name without dots through the search list first; others first as given.
by_name(Name, Type, Options, Timer) ->
    case dots(Name, 0) of
        {_, true} ->
            by_query(lists:droplast(Name), Type, Options, Timer);
        {0, false} ->
            case by_search(Name, search_list(), nxdomain, Type, Options, Timer) of
                {error, _} -> by_query(Name, Type, Options, Timer);
                Found -> Found
            end;
        {_, false} ->
            case by_query(Name, Type, Options, Timer) of
                {error, Reason} -> by_search(Name, search_list(), Reason, Type, Options, Timer);
                Found -> Found
            end
    end.

dots([$.], N) -> {N, true};
dots([$. | T], N) -> dots(T, N + 1);
dots([_ | T], N) -> dots(T, N);
dots([], N) -> {N, false}.

search_list() ->
    case clause_inet_config:config() of
        #{search := [], domain := Domain} -> [Domain];
        #{search := Search} -> Search
    end.

by_search(Name, [Domain | Domains], _, Type, Options, Timer) ->
    case by_query(search_name(Name, Domain), Type, Options#{alt_nameservers := []}, Timer) of
        {ok, _} = Found -> Found;
        {error, Reason} -> by_search(Name, Domains, Reason, Type, Options, Timer)
    end;
by_search(_, [], Reason, _, _, _) ->
    {error, Reason}.

search_name(Name, Domain) when Domain =:= "."; Domain =:= "" -> Name;
search_name("", Domain) -> Domain;
search_name(Name, Domain) -> Name ++ "." ++ Domain.

by_query(Name, Type, Options, Timer) ->
    case plain_error(query(Name, in, Type, Options, Timer)) of
        {ok, Message} -> hostent_of(Name, Type, Message);
        Error -> Error
    end.

% The hostent of the answers for Name and Type, following CNAME records; the aliases are the names before the
% canonical one, the latest first.
hostent_of(Name, Type, #dns_rec{anlist = Answers}) ->
    Records = [{string:lowercase(D), T, Data} || #dns_rr{domain = D, class = in, type = T, data = Data} <- Answers],
    case canonical(strip_dot(Name), Type, Records, []) of
        {error, _} = Error -> Error;
        {Canonical, Datas, Aliases} -> {ok, hostent(Canonical, Aliases, Type, Datas)}
    end.

canonical(Domain, Type, Records, Aliases) ->
    Lower = string:lowercase(Domain),
    case [Data || {D, T, Data} <- Records, D =:= Lower, T =:= Type] of
        [] ->
            case [Data || {D, cname, Data} <- Records, D =:= Lower] of
                [CName] ->
                    case lists:member(string:lowercase(CName), [string:lowercase(A) || A <- [Domain | Aliases]]) of
                        true -> {error, nxdomain};
                        false -> canonical(CName, Type, Records, [Domain | Aliases])
                    end;
                _ ->
                    {error, nxdomain}
            end;
        Datas ->
            {Domain, Datas, Aliases}
    end.

strip_dot(Name) ->
    case lists:reverse(Name) of
        [$. | Rest] -> lists:reverse(Rest);
        _ -> Name
    end.

hostent(Name, Aliases, a, Addresses) -> {hostent, Name, Aliases, inet, 4, Addresses};
hostent(Name, Aliases, aaaa, Addresses) -> {hostent, Name, Aliases, inet6, 16, Addresses};
hostent(Name, Aliases, Type, Datas) -> {hostent, Name, Aliases, Type, length(Datas), Datas}.

gethostbyaddr(Address) -> gethostbyaddr_tm(Address, [], false).

gethostbyaddr(Address, Timeout) -> gethostbyaddr(Address, [], Timeout).

gethostbyaddr(Address, Options, Timeout) ->
    Timer = inet:start_timer(Timeout),
    Result = gethostbyaddr_tm(Address, Options, Timer),
    _ = inet:stop_timer(Timer),
    Result.

gethostbyaddr_tm(Address, Timer) -> gethostbyaddr_tm(Address, [], Timer).

%% The hostent of the PTR answer for an address; an IPv4-mapped IPv6 address is looked up as IPv4.
gethostbyaddr_tm(Address, Options, Timer) when is_atom(Address) ->
    gethostbyaddr_tm(atom_to_list(Address), Options, Timer);
gethostbyaddr_tm(Address, Options, Timer) when is_list(Address) ->
    case inet:parse_address(Address) of
        {ok, IP} -> gethostbyaddr_tm(IP, Options, Timer);
        _ -> {error, formerr}
    end;
gethostbyaddr_tm(IP, Options, Timer) ->
    case reverse_domain(mapped(IP)) of
        {ok, Domain} ->
            case plain_error(query(Domain, in, ptr, options(Options), Timer)) of
                {ok, Message} -> ptr_hostent(Domain, IP, Message);
                Error -> Error
            end;
        {error, _} = Error ->
            Error
    end.

mapped({0, 0, 0, 0, 0, 16#ffff, G, H}) -> {G bsr 8, G band 16#ff, H bsr 8, H band 16#ff};
mapped(IP) -> IP.

ptr_hostent(Domain, IP, Message) ->
    case hostent_of(Domain, ptr, Message) of
        {ok, {hostent, _, _, _, _, [Name]}} when tuple_size(IP) =:= 4 -> {ok, {hostent, Name, [], inet, 4, [IP]}};
        {ok, {hostent, _, _, _, _, [Name]}} -> {ok, {hostent, Name, [], inet6, 16, [IP]}};
        {ok, _} -> {error, nxdomain};
        Error -> Error
    end.

%% The fields of a DNS message's records as {Type, [{Field, Value}]} lists, for printing.
dns_msg([]) ->
    [];
dns_msg([{Field, Msg} | Fields]) ->
    [{Field, dns_msg(Msg)} | dns_msg(Fields)];
dns_msg([Msg | Msgs]) ->
    [dns_msg(Msg) | dns_msg(Msgs)];
dns_msg(#dns_rr{domain = Domain, type = Type, class = Class, ttl = Ttl, data = Data, func = Func}) ->
    % inet_dns's rr/1 leaves out the access count, time and normalized domain.
    {rr, dns_msg([{domain, Domain}, {type, Type}, {class, Class}, {ttl, Ttl}, {data, Data}, {func, Func}])};
dns_msg(Msg) ->
    case record_fields(Msg) of
        undefined -> Msg;
        {Type, Names} -> {Type, dns_msg(lists:zip(Names, tl(tuple_to_list(Msg))))}
    end.

record_fields(#dns_rec{}) -> {msg, record_info(fields, dns_rec)};
record_fields(#dns_header{}) -> {header, record_info(fields, dns_header)};
record_fields(#dns_query{}) -> {dns_query, record_info(fields, dns_query)};
record_fields(#dns_rr_opt{}) -> {rr, record_info(fields, dns_rr_opt)};
record_fields(_) -> undefined.
