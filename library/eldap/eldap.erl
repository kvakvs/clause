%% OTP's LDAP client (plan 21 step MIM2, docs/libraries/eldap.md): a connection process per handle, linked to the caller
%% that opened it, speaking LDAPv3 over gen_tcp or ssl with the ELDAPv3 codec clau generates from
%% library/eldap/asn1/ELDAPv3.asn1. Results, errors, message identifiers and log calls are OTP's.
-module(eldap).
-export([
    'and'/1,
    'not'/1,
    'or'/1,
    add/3,
    add/4,
    approxMatch/2,
    baseObject/0,
    close/1,
    controlling_process/2,
    delete/2,
    delete/3,
    derefAlways/0,
    derefFindingBaseObj/0,
    derefInSearching/0,
    equalityMatch/2,
    extensibleMatch/2,
    getopts/2,
    greaterOrEqual/2,
    info/1,
    lessOrEqual/2,
    mod_add/2,
    mod_delete/2,
    mod_replace/2,
    modify/3,
    modify/4,
    modify_dn/5,
    modify_password/3,
    modify_password/4,
    modify_password/5,
    neverDerefAliases/0,
    open/1,
    open/2,
    paged_result_control/1,
    paged_result_control/2,
    paged_result_cookie/1,
    present/1,
    search/2,
    search/3,
    simple_bind/3,
    simple_bind/4,
    singleLevel/0,
    start_tls/2,
    start_tls/3,
    start_tls/4,
    substrings/2,
    wholeSubtree/0
]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    loop/2,
    parse_dn/1,
    parse_ldap_url/1
]).

-include("include/eldap.hrl").
-include("eldap.types.hrl").

-define(LDAP_VERSION, 3).
-define(LDAP_PORT, 389).
-define(PAGED_RESULTS, "1.2.840.113556.1.4.319").
-define(PASSWD_MODIFY, "1.3.6.1.4.1.4203.1.11.1").
-define(START_TLS, "1.3.6.1.4.1.1466.20037").
-define(FORBIDDEN_TEXT, "This option affects the eldap functionality and can't be set by user").

%% --- Connections ---

open(Hosts) ->
    open(Hosts, []).

open(Hosts, Options) when is_list(Hosts), is_list(Options) ->
    Self = self(),
    Pid = spawn_link(fun() -> init(Hosts, Options, Self) end),
    recv(Pid).

%% Sends the unbind request and closes the connection, without waiting for it.
close(Handle) when is_pid(Handle) ->
    send(Handle, close),
    ok.

%% The caller links to Pid; the connection unlinks from the caller.
controlling_process(Handle, Pid) when is_pid(Handle), is_pid(Pid) ->
    link(Pid),
    send(Handle, {controlling_process, Pid}),
    recv(Handle).

info(Handle) ->
    send(Handle, info),
    recv(Handle).

getopts(Handle, Names) when is_pid(Handle), is_list(Names) ->
    send(Handle, {getopts, Names}),
    recv(Handle).

start_tls(Handle, TlsOptions) ->
    start_tls(Handle, TlsOptions, infinity).

start_tls(Handle, TlsOptions, Timeout) ->
    start_tls(Handle, TlsOptions, Timeout, asn1_NOVALUE).

start_tls(Handle, TlsOptions, Timeout, Controls) ->
    send(Handle, {start_tls, TlsOptions, Timeout, Controls}),
    recv(Handle).

%% --- Operations ---

simple_bind(Handle, Dn, Passwd) when is_pid(Handle) ->
    simple_bind(Handle, Dn, Passwd, asn1_NOVALUE).

simple_bind(Handle, Dn, Passwd, Controls) when is_pid(Handle) ->
    send(Handle, {simple_bind, Dn, Passwd, Controls}),
    recv(Handle).

add(Handle, Entry, Attributes) when is_pid(Handle), is_list(Entry), is_list(Attributes) ->
    add(Handle, Entry, Attributes, asn1_NOVALUE).

add(Handle, Entry, Attributes, Controls) when is_pid(Handle), is_list(Entry), is_list(Attributes) ->
    send(Handle, {add, Entry, add_attributes(Attributes), Controls}),
    recv(Handle).

%% Attributes that are not {Type, Values} pairs throw in the caller.
add_attributes(Attributes) ->
    try
        lists:map(fun({Type, Values}) -> {'AddRequest_attributes', Type, Values} end, Attributes)
    catch
        error:_ -> throw({error, attribute_values})
    end.

delete(Handle, Entry) when is_pid(Handle), is_list(Entry) ->
    delete(Handle, Entry, asn1_NOVALUE).

delete(Handle, Entry, Controls) when is_pid(Handle), is_list(Entry) ->
    send(Handle, {delete, Entry, Controls}),
    recv(Handle).

modify(Handle, Object, Mods) when is_pid(Handle), is_list(Object), is_list(Mods) ->
    modify(Handle, Object, Mods, asn1_NOVALUE).

modify(Handle, Object, Mods, Controls) when is_pid(Handle), is_list(Object), is_list(Mods) ->
    send(Handle, {modify, Object, Mods, Controls}),
    recv(Handle).

mod_add(Type, Values) when is_list(Type), is_list(Values) -> modification(add, Type, Values).
mod_delete(Type, Values) when is_list(Type), is_list(Values) -> modification(delete, Type, Values).
mod_replace(Type, Values) when is_list(Type), is_list(Values) -> modification(replace, Type, Values).

modification(Operation, Type, Values) ->
    {'ModifyRequest_changes_SEQOF', Operation, {'PartialAttribute', Type, Values}}.

modify_dn(Handle, Entry, NewRDN, DelOldRDN, NewSupDN) when
    is_pid(Handle), is_list(Entry), is_list(NewRDN), is_atom(DelOldRDN), is_list(NewSupDN)
->
    modify_dn(Handle, Entry, NewRDN, DelOldRDN, NewSupDN, asn1_NOVALUE).

modify_dn(Handle, Entry, NewRDN, DelOldRDN, NewSupDN, Controls) ->
    send(Handle, {modify_dn, Entry, NewRDN, bool_p(DelOldRDN), optional(NewSupDN), Controls}),
    recv(Handle).

modify_password(Handle, Dn, NewPasswd) ->
    modify_password(Handle, Dn, NewPasswd, []).

modify_password(Handle, Dn, NewPasswd, OldPasswd) when
    is_pid(Handle), is_list(Dn), is_list(NewPasswd), is_list(OldPasswd)
->
    modify_password(Handle, Dn, NewPasswd, OldPasswd, asn1_NOVALUE).

modify_password(Handle, Dn, NewPasswd, OldPasswd, Controls) when
    is_pid(Handle), is_list(Dn), is_list(NewPasswd), is_list(OldPasswd)
->
    send(Handle, {passwd_modify, optional(Dn), optional(NewPasswd), optional(OldPasswd), Controls}),
    recv(Handle).

%% --- Searches ---

search(Handle, Search) when is_pid(Handle), is_record(Search, eldap_search) ->
    search(Handle, Search, asn1_NOVALUE);
search(Handle, Options) when is_pid(Handle), is_list(Options) ->
    search(Handle, Options, asn1_NOVALUE).

search(Handle, Search, Controls) when is_pid(Handle), is_record(Search, eldap_search) ->
    send(Handle, {search, Search, Controls}),
    recv(Handle);
search(Handle, Options, Controls) when is_pid(Handle), is_list(Options) ->
    try search_args(Options, #eldap_search{}) of
        Search -> search(Handle, Search, Controls)
    catch
        throw:{error, _} = Error -> Error
    end.

%% Later options win.
search_args([], Search) -> Search;
search_args([{base, Base} | T], S) -> search_args(T, S#eldap_search{base = Base});
search_args([{filter, Filter} | T], S) -> search_args(T, S#eldap_search{filter = Filter});
search_args([{scope, Scope} | T], S) -> search_args(T, S#eldap_search{scope = Scope});
search_args([{deref, Deref} | T], S) -> search_args(T, S#eldap_search{deref = Deref});
search_args([{attributes, Attrs} | T], S) -> search_args(T, S#eldap_search{attributes = Attrs});
search_args([{types_only, TypesOnly} | T], S) -> search_args(T, S#eldap_search{types_only = TypesOnly});
search_args([{timeout, Ms} | T], S) when is_integer(Ms) -> search_args(T, S#eldap_search{timeout = Ms});
search_args([{size_limit, L} | T], S) when is_integer(L) -> search_args(T, S#eldap_search{size_limit = L});
search_args([Other | _], _) -> throw({error, {unknown_arg, Other}}).

baseObject() -> baseObject.
singleLevel() -> singleLevel.
wholeSubtree() -> wholeSubtree.

neverDerefAliases() -> neverDerefAliases.
derefInSearching() -> derefInSearching.
derefFindingBaseObj() -> derefFindingBaseObj.
derefAlways() -> derefAlways.

%% --- Filters ---

'and'(Filters) when is_list(Filters) -> {'and', Filters}.
'or'(Filters) when is_list(Filters) -> {'or', Filters}.
'not'(Filter) when is_tuple(Filter) -> {'not', Filter}.

equalityMatch(Desc, Value) -> {equalityMatch, assertion(Desc, Value)}.
greaterOrEqual(Desc, Value) -> {greaterOrEqual, assertion(Desc, Value)}.
lessOrEqual(Desc, Value) -> {lessOrEqual, assertion(Desc, Value)}.
approxMatch(Desc, Value) -> {approxMatch, assertion(Desc, Value)}.

assertion(Desc, Value) ->
    {'AttributeValueAssertion', Desc, Value}.

present(Type) when is_list(Type) -> {present, Type}.

substrings(Type, SubStrings) when is_list(Type), is_list(SubStrings) ->
    case lists:dropwhile(fun substring/1, SubStrings) of
        [] -> {substrings, {'SubstringFilter', Type, SubStrings}};
        [Bad | _] -> throw({error, {substring_arg, Bad}})
    end.

substring({initial, _}) -> true;
substring({any, _}) -> true;
substring({final, _}) -> true;
substring(_) -> false.

extensibleMatch(MatchValue, Options) ->
    mra(Options, {'MatchingRuleAssertion', asn1_NOVALUE, asn1_NOVALUE, MatchValue, asn1_DEFAULT}).

%% The matching rule assertion with the options applied, later ones winning.
mra([{matchingRule, Rule} | T], Mra) -> mra(T, setelement(2, Mra, Rule));
mra([{type, Type} | T], Mra) -> mra(T, setelement(3, Mra, Type));
mra([{dnAttributes, true} | T], Mra) -> mra(T, setelement(5, Mra, true));
mra([{dnAttributes, false} | T], Mra) -> mra(T, setelement(5, Mra, false));
mra([], Mra) -> {extensibleMatch, Mra};
mra([Other | _], _) -> throw({error, {extensibleMatch_arg, Other}}).

%% --- Paged results (RFC 2696) ---

paged_result_control(PageSize) when is_integer(PageSize) ->
    paged_result_control(PageSize, "").

paged_result_control(PageSize, Cookie) when is_integer(PageSize) ->
    {ok, Value} = 'ELDAPv3':encode('RealSearchControlValue', {'RealSearchControlValue', PageSize, Cookie}),
    {control, ?PAGED_RESULTS, true, Value}.

paged_result_cookie(#eldap_search_result{controls = Controls}) ->
    find_paged_result_cookie(Controls).

%% Results without controls (asn1_NOVALUE) raise function_clause, as OTP's.
find_paged_result_cookie([]) -> {error, no_cookie};
find_paged_result_cookie([{'Control', ?PAGED_RESULTS, _, Value} | _]) -> paged_cookie(Value);
find_paged_result_cookie([_ | Rest]) -> find_paged_result_cookie(Rest).

paged_cookie(Value) ->
    {ok, {'RealSearchControlValue', _, Cookie}} = 'ELDAPv3':decode('RealSearchControlValue', iolist_to_binary(Value)),
    {ok, Cookie}.

%% --- Declined ---

loop(_, _) -> erlang:error({not_implemented, loop, 2}).
parse_dn(_) -> erlang:error({not_implemented, parse_dn, 1}).
parse_ldap_url(_) -> erlang:error({not_implemented, parse_ldap_url, 1}).

%% --- Calls ---

send(Handle, Request) ->
    Handle ! {self(), Request}.

%% Waits for the connection's answer as OTP's eldap does: forever, also when the connection is gone.
recv(Handle) ->
    receive
        {Handle, Reply} -> Reply
    end.

bool_p(true) -> true;
bool_p(false) -> false.

optional([]) -> asn1_NOVALUE;
optional(Value) -> Value.

%% --- The connection process ---

init(Hosts, Options, Caller) ->
    case options(Options, defaults()) of
        {ok, State} -> connect(Hosts, State, Caller);
        Error -> Caller ! {self(), Error}
    end.

defaults() ->
    #{
        port => ?LDAP_PORT,
        timeout => infinity,
        log => undefined,
        ldaps => false,
        anon_auth => false,
        tls_options => [],
        tcp_options => [],
        transport => gen_tcp,
        socket => undefined,
        using_tls => false,
        buffer => <<>>,
        id => 0
    }.

options([], State) ->
    {ok, State};
options([{port, Port} | T], State) when is_integer(Port) ->
    options(T, State#{port := Port});
options([{timeout, Ms} | T], State) when is_integer(Ms), Ms > 0 ->
    options(T, State#{timeout := Ms});
options([{log, Log} | T], State) when is_function(Log, 3) ->
    options(T, State#{log := Log});
options([{log, _} | T], State) ->
    options(T, State);
options([{ssl, Ldaps} | T], State) ->
    options(T, State#{ldaps := Ldaps});
options([{anon_auth, Anon} | T], State) ->
    options(T, State#{anon_auth := Anon});
options([{sslopts, TlsOptions} | T], State) ->
    options(T, State#{tls_options := TlsOptions});
options([{tcpopts, TcpOptions} | T], State) when is_list(TcpOptions) ->
    case lists:search(fun forbidden/1, TcpOptions) of
        {value, Bad} -> {error, {{forbidden_tcp_option, Bad}, ?FORBIDDEN_TEXT}};
        false -> options(T, State#{tcp_options := TcpOptions})
    end;
options([Other | _], _) ->
    {error, {wrong_option, Other}}.

%% The socket options eldap sets itself.
forbidden(binary) -> true;
forbidden(list) -> true;
forbidden({Name, _}) -> lists:member(Name, [active, binary, deliver, list, mode, packet]);
forbidden(_) -> false.

%% The hosts are tried in order; the caller gets the handle or "connect failed".
connect([], _, Caller) ->
    Caller ! {self(), {error, "connect failed"}};
connect([Host | Hosts], State, Caller) ->
    case connect_host(Host, State) of
        {ok, Socket} ->
            Caller ! {self(), {ok, self()}},
            loop_connection(State#{socket := Socket, transport := transport(State)});
        Error ->
            log(State, "Connect: ~p failed ~p~n", [Host, Error]),
            connect(Hosts, State, Caller)
    end.

transport(#{ldaps := true}) -> ssl;
transport(_) -> gen_tcp.

connect_host(Host, #{port := Port, timeout := Timeout, tcp_options := TcpOptions} = State) ->
    Options =
        case transport(State) of
            ssl -> socket_options() ++ maps:get(tls_options, State) ++ TcpOptions;
            gen_tcp -> socket_options() ++ TcpOptions
        end,
    try
        (transport(State)):connect(Host, Port, Options, Timeout)
    catch
        _:Reason -> {error, Reason}
    end.

%% OTP's eldap reads {packet, asn1} messages; Clause's reads bytes and frames them itself.
socket_options() ->
    [binary, {packet, 0}, {active, false}].

loop_connection(State) ->
    receive
        {_From, close} ->
            unbind(State);
        {From, Request} ->
            {Reply, NewState} = handle(Request, From, State),
            From ! {self(), Reply},
            loop_connection(NewState)
    end.

handle({simple_bind, Dn, Passwd, Controls}, _, State) ->
    simple_bind_request(Dn, Passwd, Controls, State);
handle({search, Search, Controls}, _, State) ->
    search_request(Search, Controls, State);
handle({add, Entry, Attributes, Controls}, _, State) ->
    add_request(Entry, Attributes, Controls, State);
handle({modify, Object, Mods, Controls}, _, State) ->
    Request = {'ModifyRequest', Object, Mods},
    operation("modify", {modifyRequest, Request}, modifyResponse, Controls, State);
handle({delete, Entry, Controls}, _, State) ->
    operation("del", {delRequest, Entry}, delResponse, Controls, State);
handle({modify_dn, Entry, NewRDN, DelOldRDN, NewSupDN, Controls}, _, State) ->
    Request = {'ModifyDNRequest', Entry, NewRDN, DelOldRDN, NewSupDN},
    operation("modify DN", {modDNRequest, Request}, modDNResponse, Controls, State);
handle({passwd_modify, Dn, NewPasswd, OldPasswd, Controls}, _, State) ->
    passwd_modify_request(Dn, NewPasswd, OldPasswd, Controls, State);
handle({start_tls, TlsOptions, Timeout, Controls}, _, State) ->
    start_tls_request(TlsOptions, Timeout, Controls, State);
handle({getopts, Names}, _, State) ->
    {getopts_reply(Names, State), State};
handle(info, _, #{socket := Socket} = State) ->
    {#{socket => Socket, socket_type => socket_type(State)}, State};
handle({controlling_process, _Pid}, From, State) ->
    unlink(From),
    {ok, State}.

socket_type(#{ldaps := true}) -> ssl;
socket_type(_) -> tcp.

getopts_reply(Names, State) ->
    try
        {ok, [option(Name, State) || Name <- Names]}
    catch
        Class:Reason -> {error, {Class, Reason}}
    end.

option(Name, State) ->
    case Name of
        port -> {port, maps:get(port, State)};
        log -> {log, maps:get(log, State)};
        timeout -> {timeout, maps:get(timeout, State)};
        ssl -> {ssl, maps:get(ldaps, State)}
    end.

%% --- Requests and answers ---

simple_bind_request(_, [], _, #{anon_auth := Anon} = State) when Anon =/= true ->
    {{error, anonymous_auth}, State};
simple_bind_request(Dn, Passwd, Controls, State) ->
    Request = {'BindRequest', ?LDAP_VERSION, Dn, {simple, Passwd}},
    log(State, "bind request = ~p~n", [Request]),
    Id = next_id(State),
    try exchange("bind", Id, {bindRequest, Request}, Controls, State) of
        {Answer, NewState} -> settle(Id, bind_answer(bindResponse, Id, Answer), NewState)
    catch
        error:Reason:Stack -> {{error, {'EXIT', {Reason, Stack}}}, State}
    end.

%% Binds keep their socket errors.
bind_answer(_, _, {socket, Reason}) -> {error, {gen_tcp_error, Reason}};
bind_answer(Expected, Id, Answer) -> answer(Expected, Id, Answer).

add_request(Entry, Attributes, Controls, State) ->
    operation("add", {addRequest, {'AddRequest', Entry, Attributes}}, addResponse, Controls, State).

%% A request answered by an LDAPResult: ok, a referral or the result code.
operation(Name, Op, Expected, Controls, State) ->
    {RequestName, Request} = Op,
    log(State, Name ++ " request = ~p~n", [request_term(RequestName, Request)]),
    Id = next_id(State),
    guarded(State, fun() ->
        {Answer, NewState} = exchange(Name, Id, Op, Controls, State),
        settle(Id, closed_answer(Expected, Id, Answer), NewState)
    end).

request_term(delRequest, Entry) -> Entry;
request_term(_, Request) -> Request.

%% Requests other than binds report a lost connection as ldap_closed.
closed_answer(_, _, {socket, timeout}) -> {error, {gen_tcp_error, timeout}};
closed_answer(_, _, {socket, _}) -> {error, ldap_closed};
closed_answer(Expected, Id, Answer) -> answer(Expected, Id, Answer).

answer(Expected, Id, Answer) ->
    case expected(Expected, Id, Answer) of
        {value, Result} -> result(Result);
        Error -> Error
    end.

expected(Expected, Id, {ok, {'LDAPMessage', Id, {Expected, Value}, _}}) -> {value, Value};
expected(_, Id, {ok, {'LDAPMessage', Id, {Other, Value}, _}}) -> {error, {Other, Value}};
expected(_, _, {ok, Message}) -> {error, {ok, Message}};
expected(_, _, Error) -> Error.

%% The result code of an LDAPResult, BindResponse or ExtendedResponse.
result(Result) ->
    case element(2, Result) of
        success -> ok;
        referral -> {ok, {referral, element(5, Result)}};
        Code -> {error, Code}
    end.

%% Successful requests take their message identifier.
settle(_, {error, _} = Error, State) -> {Error, State};
settle(Id, Reply, State) -> {Reply, State#{id := Id}}.

passwd_modify_request(Dn, NewPasswd, OldPasswd, Controls, State) ->
    Value = {'PasswdModifyRequestValue', Dn, OldPasswd, NewPasswd},
    log(State, "modify password request = ~p~n", [Value]),
    Id = next_id(State),
    guarded(State, fun() ->
        {ok, Bytes} = 'ELDAPv3':encode('PasswdModifyRequestValue', Value),
        Request = {'ExtendedRequest', ?PASSWD_MODIFY, Bytes},
        log(State, "extended request = ~p~n", [Request]),
        {Answer, NewState} = exchange("modify password", Id, {extendedReq, Request}, Controls, State),
        settle(Id, passwd_answer(Id, Answer), NewState)
    end).

passwd_answer(Id, Answer) ->
    case expected(extendedResp, Id, socket_answer(Answer)) of
        {value, Response} -> passwd_result(Response);
        Error -> Error
    end.

passwd_result({'ExtendedResponse', success, _, _, _, _, asn1_NOVALUE}) ->
    ok;
passwd_result({'ExtendedResponse', success, _, _, _, _, Value}) ->
    case 'ELDAPv3':decode('PasswdModifyResponseValue', iolist_to_binary(Value)) of
        {ok, {'PasswdModifyResponseValue', asn1_NOVALUE}} -> ok;
        {ok, {'PasswdModifyResponseValue', Generated}} -> {ok, Generated}
    end;
passwd_result({'ExtendedResponse', referral, _, _, Referral, _, _}) ->
    {ok, {referral, Referral}};
passwd_result(Response) ->
    {error, {response, element(2, Response)}}.

socket_answer({socket, timeout}) -> {error, {gen_tcp_error, timeout}};
socket_answer({socket, _}) -> {error, ldap_closed};
socket_answer(Answer) -> Answer.

%% StartTLS takes the current message identifier and writes no log.
start_tls_request(_, _, _, #{using_tls := true} = State) ->
    {{error, tls_already_started}, State};
start_tls_request(_, _, _, #{ldaps := true} = State) ->
    {{error, tls_already_started}, State};
start_tls_request(TlsOptions, Timeout, Controls, #{id := Id} = State) ->
    Request = {'ExtendedRequest', ?START_TLS, asn1_NOVALUE},
    guarded(State, fun() ->
        {Answer, NewState} = exchange_quietly(Id, {extendedReq, Request}, Controls, State),
        case expected(extendedResp, Id, socket_answer(Answer)) of
            {value, {'ExtendedResponse', success, _, _, _, _, _}} -> upgrade(TlsOptions, Timeout, NewState);
            {value, Response} -> {{error, {response, element(2, Response)}}, NewState};
            Error -> {Error, NewState}
        end
    end).

%% The connection goes over TLS; a failed handshake ends it.
upgrade(TlsOptions, Timeout, #{socket := Socket} = State) ->
    case ssl:connect(Socket, socket_options() ++ TlsOptions, Timeout) of
        {ok, TlsSocket} ->
            {ok, State#{socket := TlsSocket, transport := ssl, using_tls := true, buffer := <<>>}};
        Error ->
            gen_tcp:close(Socket),
            {Error, State}
    end.

%% A request whose arguments or encoding fail answers as OTP's: the error, or the exception with its stack.
guarded(State, Fun) ->
    try
        Fun()
    catch
        throw:{error, _} = Error -> {Error, State};
        error:Reason:Stack -> {{error, {Reason, Stack}}, State}
    end.

%% --- Searches ---

search_request(Search, Controls, State) ->
    guarded(State, fun() ->
        Request = search_record(Search),
        log(State, "search request = ~p~n", [Request]),
        Id = next_id(State),
        case send_message(Id, {searchRequest, Request}, Controls, State) of
            ok -> collect(Id, State, [], []);
            {error, Reason} -> {closed_answer(searchResDone, Id, {socket, Reason}), State}
        end
    end).

%% OTP checks the options in the order of the SearchRequest's fields.
search_record(#eldap_search{} = S) ->
    Scope = v_scope(S#eldap_search.scope),
    Deref = v_deref(S#eldap_search.deref),
    SizeLimit = v_size_limit(S#eldap_search.size_limit),
    TimeLimit = v_timeout(S#eldap_search.timeout),
    TypesOnly = v_bool(S#eldap_search.types_only),
    Filter = v_filter(S#eldap_search.filter),
    Attributes = v_attributes(S#eldap_search.attributes),
    {'SearchRequest', S#eldap_search.base, Scope, Deref, SizeLimit, TimeLimit, TypesOnly, Filter, Attributes}.

v_scope(Scope) when Scope =:= baseObject; Scope =:= singleLevel; Scope =:= wholeSubtree -> Scope;
v_scope(Scope) -> throw({error, lists:concat(["unknown scope: ", Scope])}).

v_deref(Deref) when
    Deref =:= neverDerefAliases; Deref =:= derefInSearching; Deref =:= derefFindingBaseObj; Deref =:= derefAlways
->
    Deref.

v_size_limit(Limit) when is_integer(Limit), Limit >= 0 -> Limit;
v_size_limit(Limit) -> throw({error, lists:concat(["size_limit not positive integer: ", Limit])}).

v_timeout(Ms) when is_integer(Ms), Ms >= 0 -> Ms;
v_timeout(Ms) -> throw({error, lists:concat(["timeout not positive integer: ", Ms])}).

v_bool(true) -> true;
v_bool(false) -> false;
v_bool(Other) -> throw({error, lists:concat(["not Boolean: ", Other])}).

v_filter({Kind, _} = Filter) when
    Kind =:= 'and';
    Kind =:= 'or';
    Kind =:= 'not';
    Kind =:= equalityMatch;
    Kind =:= substrings;
    Kind =:= greaterOrEqual;
    Kind =:= lessOrEqual;
    Kind =:= present;
    Kind =:= approxMatch;
    Kind =:= extensibleMatch
->
    Filter;
v_filter(Filter) ->
    throw({error, lists:concat(["unknown filter: ", Filter])}).

v_attributes(Attributes) ->
    lists:map(fun v_attribute/1, Attributes).

v_attribute(Attribute) when is_list(Attribute) -> Attribute;
v_attribute(Attribute) -> throw({error, lists:concat(["attribute not String: ", Attribute])}).

%% Entries and references until the search's result; any result read takes the message identifier.
collect(Id, State, Entries, Referrals) ->
    {Answer, NewState} = read_answer("search", State),
    case Answer of
        {socket, _} ->
            {closed_answer(searchResDone, Id, Answer), NewState};
        {ok, {'LDAPMessage', Id, {searchResEntry, {'SearchResultEntry', Name, Attributes}}, _}} ->
            Entry = #eldap_entry{object_name = Name, attributes = [{T, V} || {'PartialAttribute', T, V} <- Attributes]},
            collect(Id, NewState, [Entry | Entries], Referrals);
        {ok, {'LDAPMessage', Id, {searchResRef, Refs}, _}} ->
            collect(Id, NewState, Entries, [Refs | Referrals]);
        {ok, {'LDAPMessage', Id, {searchResDone, Result}, Controls}} ->
            log(State, "search reply = searchResDone ~n", []),
            Reply = search_result(Result, #eldap_search_result{
                entries = Entries,
                referrals = Referrals,
                controls = Controls
            }),
            {Reply, NewState#{id := Id}};
        _ ->
            {expected(searchResDone, Id, Answer), NewState}
    end.

search_result(Result, Found) ->
    case element(2, Result) of
        success -> {ok, Found};
        sizeLimitExceeded -> {ok, Found};
        referral -> {ok, {referral, element(5, Result)}};
        Code -> {error, Code}
    end.

%% --- Messages ---

next_id(#{id := Id}) -> Id + 1.

%% Sends one request and reads the next message, logging it under the request's name.
exchange(Name, Id, Op, Controls, State) ->
    case send_message(Id, Op, Controls, State) of
        ok -> read_answer(Name, State);
        {error, Reason} -> {{socket, Reason}, State}
    end.

exchange_quietly(Id, Op, Controls, State) ->
    case send_message(Id, Op, Controls, State) of
        ok -> read_message(State);
        {error, Reason} -> {{socket, Reason}, State}
    end.

send_message(Id, Op, Controls, #{transport := Transport, socket := Socket}) ->
    {ok, Bytes} = 'ELDAPv3':encode('LDAPMessage', {'LDAPMessage', Id, Op, controls(Controls)}),
    Transport:send(Socket, Bytes).

controls(asn1_NOVALUE) -> asn1_NOVALUE;
controls(Controls) -> [{'Control', Type, Critical, Value} || {control, Type, Critical, Value} <- Controls].

read_answer(Name, State) ->
    case read_message(State) of
        {{socket, _}, _} = Lost ->
            Lost;
        {Decoded, NewState} ->
            log(State, Name ++ " reply = ~p~n", [Decoded]),
            {Decoded, NewState}
    end.

%% The next message: {ok, Message}, the codec's error, or {socket, Reason}.
read_message(#{buffer := Buffer, timeout := Timeout} = State) ->
    case read_packet(Buffer, deadline(Timeout), State) of
        {ok, Packet, Rest} -> {'ELDAPv3':decode('LDAPMessage', Packet), State#{buffer := Rest}};
        {error, Reason, Rest} -> {{socket, Reason}, State#{buffer := Rest}}
    end.

read_packet(Buffer, Deadline, State) ->
    case erlang:decode_packet(asn1, Buffer, []) of
        {ok, Packet, Rest} -> {ok, Packet, Rest};
        {more, _} -> receive_more(Buffer, Deadline, State);
        {error, Reason} -> {error, Reason, Buffer}
    end.

receive_more(Buffer, Deadline, #{transport := Transport, socket := Socket} = State) ->
    case Transport:recv(Socket, 0, remaining(Deadline)) of
        {ok, Data} -> read_packet(<<Buffer/binary, Data/binary>>, Deadline, State);
        {error, Reason} -> {error, Reason, Buffer}
    end.

deadline(infinity) -> infinity;
deadline(Ms) -> erlang:monotonic_time(millisecond) + Ms.

remaining(infinity) -> infinity;
remaining(Deadline) -> max(0, Deadline - erlang:monotonic_time(millisecond)).

%% The unbind request takes the current message identifier; the connection ends after it.
unbind(#{id := Id, transport := Transport, socket := Socket} = State) ->
    log(State, "unbind request = ~p (has no reply)~n", [[]]),
    try
        send_message(Id, {unbindRequest, []}, asn1_NOVALUE, State)
    catch
        error:_ -> ok
    end,
    _ = Transport:close(Socket),
    ok.

log(#{log := Log}, Format, Args) when is_function(Log, 3) ->
    Log(2, Format, Args);
log(_, _, _) ->
    ok.
