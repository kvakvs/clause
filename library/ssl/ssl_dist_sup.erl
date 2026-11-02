%% Clause's ssl_dist_sup (plan 30 step DIST12a, docs/distribution.md#tls-distribution): the supervisor of TLS
%% distribution that inet_tls_dist's childspecs/0 puts under net_sup. Its start reads the file of --ssl-dist-optfile
%% (OTP's -ssl_dist_optfile) into the ssl_dist_opts table, owned by net_sup as OTP's; its child tls_dist_connection_sup
%% supervises the connection processes of TLS distribution, apart from the ssl application's.
-module(ssl_dist_sup).
-behaviour(supervisor).
-export([start_link/0, init/1, consult/1]).

% Read the options file, then start the supervisor; a bad file fails the start with {bad_ssl_dist_optfile, ...}.
start_link() ->
    case init:get_argument(ssl_dist_optfile) of
        {ok, [[File]]} ->
            Options = consult(File),
            Table = ets:new(ssl_dist_opts, [set, protected, named_table]),
            true = ets:insert(Table, Options),
            supervisor:start_link({local, ?MODULE}, ?MODULE, []);
        {ok, Other} ->
            erlang:error({bad_ssl_dist_optfile, Other});
        error ->
            supervisor:start_link({local, ?MODULE}, ?MODULE, [])
    end.

init([]) ->
    Connections = #{
        id => tls_dist_connection_sup,
        start => {supervisor, start_link, [{local, tls_dist_connection_sup}, clause_ssl_app, connections]},
        type => supervisor,
        shutdown => infinity,
        modules => [clause_ssl_app]
    },
    {ok, {#{strategy => one_for_all, intensity => 10, period => 3600}, [Connections]}}.

% The term of an options file: one expression in the file's epp encoding (latin1 without one), evaluated without
% bindings; any failure raises {bad_ssl_dist_optfile, Reason} as OTP's.
consult(File) ->
    case erl_prim_loader:read_file(File) of
        {ok, Binary} -> consult_string(characters(Binary));
        error -> erlang:error({bad_ssl_dist_optfile, File})
    end.

characters(Binary) ->
    Encoding =
        case epp:read_encoding_from_binary(Binary) of
            none -> latin1;
            Found -> Found
        end,
    case unicode:characters_to_list(Binary, Encoding) of
        {error, _, Rest} -> erlang:error({bad_ssl_dist_optfile, {encoding_error, Rest}});
        {incomplete, _, Rest} -> erlang:error({bad_ssl_dist_optfile, {encoding_incomplete, Rest}});
        String -> String
    end.

consult_string(String) ->
    case erl_scan:string(String) of
        {ok, Tokens, _} -> consult_tokens(Tokens);
        {error, Info, Location} -> erlang:error({bad_ssl_dist_optfile, {scan_error, Info, Location}})
    end.

consult_tokens(Tokens) ->
    case erl_parse:parse_exprs(Tokens) of
        {ok, [Expression]} -> evaluate(Expression);
        {ok, Other} -> erlang:error({bad_ssl_dist_optfile, {parse_error, Other}});
        {error, Info} -> erlang:error({bad_ssl_dist_optfile, {parse_error, Info}})
    end.

evaluate(Expression) ->
    {value, Value, Bindings} = erl_eval:expr(Expression, erl_eval:new_bindings()),
    case erl_eval:bindings(Bindings) of
        [] -> Value;
        Other -> erlang:error({bad_ssl_dist_optfile, {bindings, Other}})
    end.
