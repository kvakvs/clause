%% TLS 1.3 session tickets of ssl (plan 21 step LIB81D2b, docs/libraries/ssl.md#session-tickets): the client ticket
%% store of session_tickets auto, registered as tls_client_ticket_store as OTP's, and the ticket settings of the
%% application environment. A ticket is a map: cipher_suite ({Cipher, Hash}), sni, timestamp and ticket (libssl's
%% DER session); manual clients get the same maps. The store keeps tickets oldest first, gives each out once, and
%% drops them after client_session_ticket_lifetime seconds or beyond client_session_ticket_store_size.
-module(clause_ssl_tickets).
-behaviour(gen_server).
-export([start_link/0, store/1, take/2, ticket/3]).
-export([server_amount/0, server_lifetime/0, server_store_size/0, max_early_data/0]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-define(TWO_HOURS, 7200).
-define(SEVEN_DAYS, 604800).
-define(DEFAULT_STORE_SIZE, 1000).
-define(DEFAULT_AMOUNT, 3).
-define(DEFAULT_MAX_EARLY_DATA, 16384).

start_link() ->
    gen_server:start_link({local, tls_client_ticket_store}, ?MODULE, [], []).

%% The ticket map of a TLS 1.3 session the server sent under a suite code, for the client naming the server SNI.
ticket(Data, SuiteCode, SNI) ->
    #{cipher := Cipher, prf := Hash} = clause_ssl_algorithms:code_suite(SuiteCode),
    #{cipher_suite => {Cipher, Hash}, sni => SNI, timestamp => erlang:system_time(millisecond), ticket => Data}.

%% Keep a ticket for later connections.
store(Ticket) ->
    gen_server:cast(tls_client_ticket_store, {store, Ticket}).

%% Take the oldest valid ticket for the server name SNI whose suite the client offers ({Cipher, Hash} pairs in
%% preference order): its DER session, else undefined. Tickets of the first hash that has one come first, as OTP's.
take(SNI, Suites) ->
    try
        gen_server:call(tls_client_ticket_store, {take, SNI, Suites}, infinity)
    catch
        exit:_ -> undefined
    end.

%% The server settings: tickets sent after each handshake, their lifetime and a stateful server's store size.
server_amount() -> env(server_session_tickets_amount, fun(N) -> is_integer(N) andalso N > 0 end, ?DEFAULT_AMOUNT).

server_lifetime() ->
    env(server_session_ticket_lifetime, fun(N) -> is_integer(N) andalso N =< ?SEVEN_DAYS end, ?TWO_HOURS).

server_store_size() -> env(server_session_ticket_store_size, fun is_integer/1, ?DEFAULT_STORE_SIZE).

%% Added for early data (LIB81D2c): the early data a server takes and its tickets name.
max_early_data() -> env(server_session_ticket_max_early_data, fun is_integer/1, ?DEFAULT_MAX_EARLY_DATA).

env(Key, Valid, Default) ->
    case application:get_env(ssl, Key) of
        {ok, Value} ->
            case Valid(Value) of
                true -> Value;
                false -> Default
            end;
        undefined ->
            Default
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% The store process
%%--------------------------------------------------------------------------------------------------------------------

%% The state: tickets (by {monotonic time, unique integer}, oldest first), lifetime (seconds) and max (entries kept).
init([]) ->
    {ok, #{
        tickets => gb_trees:empty(),
        lifetime => env(client_session_ticket_lifetime, fun(N) -> is_integer(N) andalso N > 0 end, ?TWO_HOURS),
        max => env(client_session_ticket_store_size, fun(N) -> is_integer(N) andalso N > 0 end, ?DEFAULT_STORE_SIZE)
    }}.

handle_call({take, SNI, Suites}, _From, #{tickets := Tickets} = S) ->
    Hashes = unique([Hash || {_, Hash} <- Suites]),
    case first_usable(Hashes, SNI, Suites, S) of
        {Key, #{ticket := Data}} -> {reply, Data, S#{tickets => gb_trees:delete(Key, Tickets)}};
        none -> {reply, undefined, S}
    end.

handle_cast({store, Ticket}, #{tickets := Tickets0, max := Max} = S) ->
    Tickets1 =
        case gb_trees:size(Tickets0) >= Max of
            true -> element(3, gb_trees:take_smallest(Tickets0));
            false -> Tickets0
        end,
    Key = {erlang:monotonic_time(), erlang:unique_integer([monotonic])},
    Stamped = Ticket#{stored => erlang:monotonic_time(millisecond)},
    {noreply, S#{tickets => gb_trees:insert(Key, Stamped, Tickets1)}}.

handle_info(_Message, S) ->
    {noreply, S}.

unique([]) -> [];
unique([X | Rest]) -> [X | unique([Y || Y <- Rest, Y =/= X])].

% The oldest usable ticket of the first hash that has one.
first_usable([], _SNI, _Suites, _S) ->
    none;
first_usable([Hash | Hashes], SNI, Suites, S) ->
    case oldest(gb_trees:iterator(maps:get(tickets, S)), Hash, SNI, Suites, S) of
        none -> first_usable(Hashes, SNI, Suites, S);
        Found -> Found
    end.

oldest(Iterator0, Hash, SNI, Suites, S) ->
    case gb_trees:next(Iterator0) of
        {Key, Ticket, Iterator} ->
            case usable(Ticket, Hash, SNI, Suites, S) of
                true -> {Key, Ticket};
                false -> oldest(Iterator, Hash, SNI, Suites, S)
            end;
        none ->
            none
    end.

% A ticket of the hash, still valid, for the same server (any when the client names none), and of a cipher the client
% offers.
usable(#{cipher_suite := {Cipher, Hash}, sni := TicketSNI, stored := Stored}, Hash, SNI, Suites, #{lifetime := Life}) ->
    erlang:monotonic_time(millisecond) - Stored < Life * 1000 andalso
        (SNI == undefined orelse SNI == TicketSNI) andalso
        lists:keymember(Cipher, 1, Suites);
usable(_Ticket, _Hash, _SNI, _Suites, _S) ->
    false.
