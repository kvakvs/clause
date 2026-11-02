%% Clause's inet_tls_dist (plan 30 step DIST12a, docs/distribution.md#tls-distribution): the IPv4 TLS carrier of
%% distribution, selected with --proto-dist inet_tls, written from OTP's ssl distribution guide and observed behavior.
%% It listens and looks nodes up through inet_tcp_dist's fam_listen/4 and fam_setup/4; each connection runs a TLS
%% handshake over Clause's ssl, then dist_util's handshake with a clause_dist_controller over the ssl socket, in
%% packets of 4 bytes from the start as OTP's. The ssl options come from --ssl-dist-optfile (ssl_dist_sup's
%% ssl_dist_opts table) and --ssl-dist-opt. Its fam_* functions take an address family for inet6_tls_dist.
-module(inet_tls_dist).
-export([
    accept/1,
    accept_connection/5,
    address/0,
    allowed_hosts/1,
    cert_nodes/1,
    childspecs/0,
    close/1,
    dbg/0,
    fam_accept/2,
    fam_accept_connection/6,
    fam_address/1,
    fam_listen/3,
    fam_select/2,
    fam_setup/6,
    get_ssl_client_options/0,
    get_ssl_server_options/1,
    inet_ktls_getopt/3,
    inet_ktls_setopt/3,
    is_node_name/1,
    ktls_opt_cipher/6,
    ktls_opt_ulp/1,
    ktls_os/0,
    listen/2,
    select/1,
    set_ktls/1,
    set_ktls_cipher/5,
    set_ktls_ulp/2,
    setup/5,
    verify_client/3
]).

-include_lib("kernel/include/net_address.hrl").
-include_lib("kernel/include/dist_util.hrl").
-include_lib("public_key/include/public_key.hrl").

-define(FAMILY, inet).
-define(PROTOCOL, tls).

% net_sup's child of TLS distribution, as OTP's tuple; the literal fun links ssl_dist_sup, named here only as data.
childspecs() ->
    _ = fun ssl_dist_sup:start_link/0,
    {ok, [{ssl_dist_sup, {ssl_dist_sup, start_link, []}, permanent, infinity, supervisor, [ssl_dist_sup]}]}.

select(Node) -> fam_select(?FAMILY, Node).

fam_select(Family, Node) -> inet_tcp_dist:fam_select(Family, Node).

address() -> fam_address(?FAMILY).

fam_address(Family) -> (inet_tcp_dist:fam_address(Family))#net_address{protocol = ?PROTOCOL}.

is_node_name(Node) -> dist_util:is_node_name(Node).

close(Listen) -> gen_tcp:close(Listen).

%% Listening: a TCP socket of passive packets of 4 bytes, registered with epmd as inet_tcp_dist's.
listen(Name, Host) -> fam_listen(?FAMILY, Name, Host).

fam_listen(Family, Name, Host) ->
    Forced = [Family, {active, false}, {packet, 4}, {nodelay, true}],
    Listen = fun(First, Last, Options) -> listen_socket(First, Last, inet_tcp_dist:merge_options(Options, Forced)) end,
    case inet_tcp_dist:fam_listen(Family, Name, Host, Listen) of
        {ok, {Socket, Address, Creation}} ->
            {ok,
                {Socket, #net_address{address = Address, host = Host, protocol = ?PROTOCOL, family = Family}, Creation}};
        Error ->
            Error
    end.

listen_socket(First, Last, Options) when First =< Last ->
    case gen_tcp:listen(First, Options) of
        {error, eaddrinuse} -> listen_socket(First + 1, Last, Options);
        Result -> Result
    end;
listen_socket(_, _, _) ->
    {error, eaddrinuse}.

%% Accepting (plan 30 step DIST12a): an acceptor process at max priority, linked to net_kernel, gives each accepted
%% socket to a process of its own, which runs the server's TLS handshake and then hands the controller to net_kernel
%% as inet_tcp_dist's acceptor does. A failed TLS handshake closes the socket.
accept(Listen) -> fam_accept(?FAMILY, Listen).

fam_accept(Family, Listen) ->
    Kernel = self(),
    spawn_opt(fun() -> accept_loop(Kernel, Listen, Family) end, dist_util:net_ticker_spawn_options()).

accept_loop(Kernel, Listen, Family) ->
    case gen_tcp:accept(Listen) of
        {ok, Socket} ->
            Handshake = spawn(fun() -> accept_one(Kernel, Family) end),
            ok = gen_tcp:controlling_process(Socket, Handshake),
            Handshake ! {socket, Socket},
            accept_loop(Kernel, Listen, Family);
        Error ->
            exit(Error)
    end.

accept_one(Kernel, Family) ->
    receive
        {socket, Socket} -> accepted(Kernel, Family, Socket, server_handshake(Socket))
    end.

server_handshake(Socket) ->
    case inet:peername(Socket) of
        {ok, {IP, _}} ->
            Options = [{active, false}, {packet, 4} | get_ssl_server_options(IP)],
            ssl:handshake(Socket, Options, net_kernel:connecttime());
        Error ->
            Error
    end.

accepted(Kernel, Family, _, {ok, SslSocket}) ->
    Controller = clause_dist_controller:start(SslSocket, transport()),
    Monitor = erlang:monitor(process, Kernel),
    Kernel ! {accept, self(), Controller, Family, ?PROTOCOL},
    receive
        {Kernel, controller, Supervisor} ->
            clause_dist_controller:call(Controller, {supervisor, Supervisor}),
            Supervisor ! {self(), controller};
        {Kernel, unsupported_protocol} ->
            exit(unsupported_protocol);
        {'DOWN', Monitor, process, Kernel, _} ->
            exit(Controller, kill)
    end;
accepted(_, _, Socket, {error, {options, _}} = Error) ->
    error_logger:error_msg("Cannot accept TLS distribution connection: ~s~n", [ssl:format_error(Error)]),
    gen_tcp:close(Socket);
accepted(_, _, Socket, _) ->
    gen_tcp:close(Socket).

% The controller's transport: ssl's functions and messages, packets of 4 bytes throughout, nodelay once up.
transport() ->
    #{
        data => ssl,
        options => ssl,
        tags => {ssl, ssl_closed, ssl_error},
        protocol => ?PROTOCOL,
        pre_nodeup => [],
        post_nodeup => [inet_tcp_dist:nodelay()]
    }.

accept_connection(Acceptor, Controller, MyNode, Allowed, SetupTime) ->
    fam_accept_connection(?FAMILY, Acceptor, Controller, MyNode, Allowed, SetupTime).

fam_accept_connection(Family, Acceptor, Controller, MyNode, Allowed, SetupTime) ->
    Kernel = self(),
    spawn_opt(
        fun() -> do_accept(Kernel, Acceptor, Controller, {MyNode, Allowed, SetupTime}, Family) end,
        dist_util:net_ticker_spawn_options()
    ).

% The connection supervisor of an accepted connection: the nodes the peer's certificate names narrow Allowed.
do_accept(Kernel, Acceptor, Controller, {MyNode, Allowed, SetupTime}, Family) ->
    Monitor = erlang:monitor(process, Acceptor),
    receive
        {Acceptor, controller} ->
            erlang:demonitor(Monitor, [flush]),
            Timer = dist_util:start_timer(SetupTime),
            SslSocket = clause_dist_controller:call(Controller, socket),
            HSData = clause_dist_controller:hs_data(Controller, SslSocket, transport(), Family),
            dist_util:handshake_other_started(HSData#hs_data{
                kernel_pid = Kernel,
                this_node = MyNode,
                timer = Timer,
                this_flags = 0,
                allowed = allowed_nodes(SslSocket, Allowed)
            });
        {'DOWN', Monitor, _, _, _} ->
            ?shutdown2(MyNode, connection_setup_failed)
    end.

%% Setting up (plan 30 step DIST12a): the connection supervisor looks the node up as inet_tcp_dist's does, then
%% connects over TLS with the client's ssl options and the node's host as the server name, and runs the handshake.
setup(Node, Type, MyNode, LongOrShortNames, SetupTime) ->
    fam_setup(?FAMILY, Node, Type, MyNode, LongOrShortNames, SetupTime).

fam_setup(Family, Node, Type, MyNode, LongOrShortNames, SetupTime) ->
    Kernel = self(),
    spawn_opt(
        fun() -> do_setup(Kernel, Node, {Type, MyNode, LongOrShortNames, SetupTime}, Family) end,
        dist_util:net_ticker_spawn_options()
    ).

do_setup(Kernel, Node, {Type, MyNode, LongOrShortNames, SetupTime}, Family) ->
    Timer = dist_util:start_timer(SetupTime),
    ParseAddress = fun(Host) -> inet:parse_strict_address(Host, Family) end,
    {#net_address{host = Host, address = {IP, Port}}, ConnectOptions, Version} =
        inet_tcp_dist:fam_setup(Family, Node, LongOrShortNames, ParseAddress),
    Options = inet_tcp_dist:merge_options(
        inet_tcp_dist:merge_options(ConnectOptions, get_ssl_client_options()),
        [Family, binary, {active, false}, {packet, 4}, {nodelay, true}],
        [{server_name_indication, Host}]
    ),
    dist_util:reset_timer(Timer),
    case ssl:connect(IP, Port, Options, net_kernel:connecttime()) of
        {ok, SslSocket} ->
            Controller = clause_dist_controller:start(SslSocket, transport()),
            clause_dist_controller:call(Controller, {supervisor, self()}),
            HSData = clause_dist_controller:hs_data(Controller, SslSocket, transport(), Family),
            dist_util:handshake_we_started(HSData#hs_data{
                kernel_pid = Kernel,
                other_node = Node,
                this_node = MyNode,
                timer = Timer,
                this_flags = 0,
                other_version = Version,
                request_type = Type
            });
        Other ->
            ?shutdown2(Node, {ssl_connect_failed, IP, Port, Other})
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Certificate checks (plan 30 step DIST12a), as OTP's
%%--------------------------------------------------------------------------------------------------------------------

% The nodes an accepted peer may be: without a list all; with one, those whose host its certificate names (or the
% peer's address, when the list holds no node or host the certificate names). A peer without a certificate gets
% Allowed; one whose certificate names none of them is refused.
allowed_nodes(_, []) ->
    [];
allowed_nodes(SslSocket, Allowed) ->
    case ssl:peercert(SslSocket) of
        {ok, Der} -> allowed_by_cert(SslSocket, public_key:pkix_decode_cert(Der, otp), Allowed);
        {error, no_peercert} -> Allowed;
        Error -> ?shutdown2(no_peer_cert, Error)
    end.

allowed_by_cert(SslSocket, Cert, Allowed) ->
    case ssl:peername(SslSocket) of
        {ok, {IP, _}} ->
            case cert_allowed(Cert, Allowed, IP) of
                [] ->
                    error_logger:error_msg("** Connection attempt from disallowed node(s) ~p ** ~n", [IP]),
                    ?shutdown2(IP, {is_allowed, not_allowed});
                Nodes ->
                    Nodes
            end;
        Error ->
            ?shutdown2(no_peer_ip, Error)
    end.

cert_allowed(Cert, [], IP) ->
    case public_key:pkix_verify_hostname(Cert, [{ip, IP}]) of
        true -> [inet:ntoa(IP)];
        false -> []
    end;
cert_allowed(Cert, [Node | Allowed], IP) ->
    case dist_util:split_node(Node) of
        {node, _, Host} -> cert_allowed(Cert, Allowed, IP, Node, Host);
        {host, Host} -> cert_allowed(Cert, Allowed, IP, Node, Host);
        _ -> cert_allowed(Cert, Allowed, IP)
    end.

cert_allowed(Cert, Allowed, IP, Node, Host) ->
    case public_key:pkix_verify_hostname(Cert, [{dns_id, Host}]) of
        true -> [Node | cert_allowed(Cert, Allowed, IP)];
        false -> cert_allowed(Cert, Allowed, IP)
    end.

% The hosts of the nodes and hosts of an allowed list, sorted and unique.
allowed_hosts(Allowed) ->
    lists:usort([Host || Entry <- Allowed, Host <- entry_host(dist_util:split_node(Entry))]).

entry_host({node, _, Host}) -> [Host];
entry_host({host, Host}) -> [Host];
entry_host(_) -> [].

% A verify_fun for servers, {fun inet_tls_dist:verify_client/3, _}: as verify_peer, and a peer certificate must name
% the client's address or one of the allowed hosts; its state is set per connection.
verify_client(_, {bad_cert, _} = Reason, _) ->
    {fail, Reason};
verify_client(_, {extension, _}, State) ->
    {unknown, State};
verify_client(_, valid, State) ->
    {valid, State};
verify_client(_, valid_peer, {[], _} = State) ->
    {valid, State};
verify_client(Cert, valid_peer, {Hosts, IP} = State) ->
    case public_key:pkix_verify_hostname(Cert, [{ip, IP} | [{dns_id, Host} || Host <- Hosts]]) of
        true -> {valid, State};
        false -> {fail, cert_no_hostname_nor_ip_match}
    end.

% The node names a certificate names: its subjectAltName hosts (dNSName) and names (a directoryName's common name),
% as Name@Host for each pair, Name@ for names alone, hosts alone, or undefined (all nodes) for neither.
cert_nodes(#'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{extensions = Extensions}}) ->
    cert_names(Extensions).

cert_names(Extensions) when is_list(Extensions) ->
    {Hosts, Names} = lists:foldl(fun alt_name/2, {[], []}, Extensions),
    node_names(lists:reverse(Hosts), lists:reverse(Names));
cert_names(_) ->
    undefined.

node_names([], []) -> undefined;
node_names(Hosts, []) -> Hosts;
node_names([], Names) -> [Name ++ "@" || Name <- Names];
node_names(Hosts, Names) -> [Name ++ "@" ++ Host || Host <- Hosts, Name <- Names].

alt_name(#'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = AltNames}, {Hosts, Names}) ->
    case subject_alt_name(AltNames) of
        {host, Host} -> {[Host | Hosts], Names};
        {name, Name} -> {Hosts, [Name | Names]};
        none -> {Hosts, Names}
    end;
alt_name(_, Acc) ->
    Acc.

subject_alt_name([]) ->
    none;
subject_alt_name([{dNSName, Host} | _]) ->
    {host, Host};
subject_alt_name([{directoryName, {rdnSequence, [Rdn | _]}} | AltNames]) ->
    case common_name(Rdn) of
        none -> subject_alt_name(AltNames);
        Name -> {name, Name}
    end;
subject_alt_name([_ | AltNames]) ->
    subject_alt_name(AltNames).

common_name([]) ->
    none;
common_name([#'AttributeTypeAndValue'{type = ?'id-at-commonName', value = {utf8String, Name}} | _]) ->
    unicode:characters_to_list(Name);
common_name([_ | Rdn]) ->
    common_name(Rdn).

%%--------------------------------------------------------------------------------------------------------------------
%% The ssl options of distribution (plan 30 step DIST12a), as OTP's
%%--------------------------------------------------------------------------------------------------------------------

get_ssl_client_options() -> ssl_options(client).

% A server's options; a {fun inet_tls_dist:verify_client/3, _} verify_fun gets the allowed hosts and the peer's IP.
get_ssl_server_options(IP) when IP =/= undefined -> verify_client_state(ssl_options(server), IP).

verify_client_state([{verify_fun, {Fun, _}} = Option | Options], IP) ->
    case fun inet_tls_dist:verify_client/3 of
        Fun when IP =:= undefined ->
            verify_client_state(Options, IP);
        Fun ->
            {ok, Allowed} = net_kernel:allowed(),
            [{verify_fun, {Fun, {allowed_hosts(Allowed), IP}}} | verify_client_state(Options, undefined)];
        _ ->
            [Option | verify_client_state(Options, IP)]
    end;
verify_client_state([Option | Options], IP) ->
    [Option | verify_client_state(Options, IP)];
verify_client_state([], _) ->
    [].

% The options of a role, client or server: those of --ssl-dist-opt, then those of the options file, with TLS 1.3 and
% 1.2 unless they name versions; erl_dist marks them as distribution's.
ssl_options(Role) ->
    Arguments =
        case init:get_argument(ssl_dist_opt) of
            {ok, Lists} -> argument_options(Role, lists:append(Lists));
            _ -> []
        end,
    [{erl_dist, true} | with_versions(Arguments ++ file_options(Role))].

file_options(Role) ->
    try ets:lookup(ssl_dist_opts, Role) of
        [{Role, Options}] -> Options;
        _ -> []
    catch
        error:badarg -> []
    end.

with_versions([]) ->
    [];
with_versions(Options) ->
    case proplists:get_value(versions, Options) of
        undefined -> [{versions, ['tlsv1.3', 'tlsv1.2']} | Options];
        _ -> Options
    end.

% --ssl-dist-opt's words in pairs: client_Key Value and server_Key Value for the role, others skipped.
argument_options(_, []) ->
    [];
argument_options(client, ["client_" ++ Key, Value | Rest] = Words) ->
    [argument_option(client, Key, Value, Words) | argument_options(client, Rest)];
argument_options(server, ["server_" ++ Key, Value | Rest] = Words) ->
    [argument_option(server, Key, Value, Words) | argument_options(server, Rest)];
argument_options(Role, [_, _ | Rest]) ->
    argument_options(Role, Rest).

argument_option(Role, Key, Value, Words) ->
    case argument_parser(Role, Key) of
        error -> erlang:error(malformed_ssl_dist_opt, [Role, Words]);
        Parse -> {list_to_atom(Key), Parse(Value)}
    end.

argument_parser(server, "dhfile") ->
    fun listify/1;
argument_parser(server, "fail_if_no_peer_cert") ->
    fun atomize/1;
argument_parser(_, Key) when Key =:= "certfile"; Key =:= "cacertfile"; Key =:= "keyfile"; Key =:= "password" ->
    fun listify/1;
argument_parser(_, Key) when
    Key =:= "verify"; Key =:= "crl_check"; Key =:= "reuse_sessions"; Key =:= "secure_renegotiate"; Key =:= "ktls"
->
    fun atomize/1;
argument_parser(_, "verify_fun") ->
    fun verify_fun/1;
argument_parser(_, "crl_cache") ->
    fun termify/1;
argument_parser(_, Key) when Key =:= "depth"; Key =:= "hibernate_after" -> fun erlang:list_to_integer/1;
argument_parser(_, "ciphers") ->
    fun(Value) -> [listify(Value)] end;
argument_parser(_, "versions") ->
    fun(Value) -> [atomize(Value)] end;
argument_parser(_, _) ->
    error.

listify(List) when is_list(List) -> List.

atomize(List) when is_list(List) -> list_to_atom(List);
atomize(Atom) when is_atom(Atom) -> Atom.

termify(String) when is_list(String) ->
    {ok, Tokens, _} = erl_scan:string(String ++ "."),
    {ok, Term} = erl_parse:parse_term(Tokens),
    Term.

% {Module, Function, State} as {fun Module:Function/3, State}.
verify_fun(Value) ->
    case termify(Value) of
        {Module, Function, State} when is_atom(Module), is_atom(Function) -> {fun Module:Function/3, State};
        _ -> erlang:error(malformed_ssl_dist_opt, [Value])
    end.

%% Declined: kernel TLS offload (the ktls option) is Linux's and OTP's internals; Clause's ssl runs TLS itself.
dbg() -> erlang:error({not_implemented, dbg, 0}).
inet_ktls_getopt(_, _, _) -> erlang:error({not_implemented, inet_ktls_getopt, 3}).
inet_ktls_setopt(_, _, _) -> erlang:error({not_implemented, inet_ktls_setopt, 3}).
ktls_opt_cipher(_, _, _, _, _, _) -> erlang:error({not_implemented, ktls_opt_cipher, 6}).
ktls_opt_ulp(_) -> erlang:error({not_implemented, ktls_opt_ulp, 1}).
ktls_os() -> erlang:error({not_implemented, ktls_os, 0}).
set_ktls(_) -> erlang:error({not_implemented, set_ktls, 1}).
set_ktls_cipher(_, _, _, _, _) -> erlang:error({not_implemented, set_ktls_cipher, 5}).
set_ktls_ulp(_, _) -> erlang:error({not_implemented, set_ktls_ulp, 2}).
