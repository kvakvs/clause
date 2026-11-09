%% httpc's options (plan 21 step MIM4, docs/libraries/inets.md#httpc): a request's HTTP options and options with
%% OTP's defaults, invalid ones reported and ignored as OTP's, and the profile options set_options accepts.
-module(clause_httpc_options).
-export([http_options/1, request_options/1, profile_options/2]).

% The HTTP options and request options httpc knows, in the order OTP checks them.
-define(HTTP_OPTIONS, [version, timeout, autoredirect, ssl, proxy_auth, relaxed, url_encode, connect_timeout]).
-define(REQUEST_OPTIONS, [
    sync,
    stream,
    body_format,
    full_result,
    headers_as_is,
    receiver,
    socket_opts,
    ipv6_host_with_brackets,
    max_header_size,
    max_body_size
]).

% A request's HTTP options: version, timeouts, TLS options (undefined for httpc's default), redirects and proxy
% credentials.
http_options(Options) ->
    Defaults = #{
        version => "HTTP/1.1",
        timeout => infinity,
        autoredirect => true,
        ssl => undefined,
        proxy_auth => undefined,
        relaxed => false,
        url_encode => false
    },
    Set = parse(Options, ?HTTP_OPTIONS, fun http_option/2, Defaults),
    maps:merge(#{connect_timeout => maps:get(timeout, Set)}, Set).

http_option(timeout, Value) -> timeout(Value);
http_option(connect_timeout, Value) -> timeout(Value);
http_option(ssl, Value) -> is_list(Value);
http_option(autoredirect, Value) -> is_boolean(Value);
http_option(relaxed, Value) -> is_boolean(Value);
http_option(url_encode, Value) -> is_boolean(Value);
http_option(proxy_auth, {User, Password}) -> is_list(User) andalso is_list(Password);
http_option(proxy_auth, _) -> false;
http_option(version, Value) -> is_list(Value).

% A request's own options: synchronous or delivered to a receiver, streaming, the result's form and the socket
% options of a connection of its own.
request_options(Options) ->
    Defaults = #{
        sync => true,
        stream => none,
        body_format => string,
        full_result => true,
        headers_as_is => false,
        socket_opts => undefined,
        receiver => self(),
        ipv6_host_with_brackets => false
    },
    Set = parse(Options, ?REQUEST_OPTIONS, fun request_option/2, Defaults),
    #{sync := Sync, body_format := Format, socket_opts := Own, stream := Stream} = Set,
    supported_stream(Stream),
    Set#{body_format := body_format(Format, Sync), own_socket => Own =/= undefined}.

request_option(sync, Value) ->
    is_boolean(Value);
request_option(stream, Value) ->
    Value =:= none orelse Value =:= self orelse Value =:= {self, once} orelse is_list(Value);
request_option(body_format, Value) ->
    Value =:= string orelse Value =:= binary;
request_option(full_result, Value) ->
    is_boolean(Value);
request_option(headers_as_is, Value) ->
    is_boolean(Value);
request_option(socket_opts, Value) ->
    is_list(Value);
request_option(receiver, Value) ->
    receiver(Value);
request_option(ipv6_host_with_brackets, Value) ->
    is_boolean(Value);
request_option(max_header_size, Value) ->
    is_integer(Value) andalso Value > 0;
request_option(max_body_size, Value) ->
    (is_integer(Value) andalso Value > 0) orelse Value =:= nolimit.

receiver(Pid) when is_pid(Pid) -> true;
receiver(Fun) when is_function(Fun, 1) -> true;
receiver({Module, Function, Args}) -> is_atom(Module) andalso is_atom(Function) andalso is_list(Args);
receiver(_) -> false.

% Streaming once at a time or to a file is not provided.
supported_stream(Stream) when Stream =:= none; Stream =:= self -> ok;
supported_stream(Stream) -> erlang:error({not_implemented, {option, {stream, Stream}}}).

% Synchronous results have string bodies unless asked for binaries; asynchronous ones always binaries, as OTP's.
body_format(Format, true) -> Format;
body_format(_, false) -> binary.

% The known options in their table's order, the first value of each checked (an invalid one reported and ignored),
% then the unknown ones reported in the order given, as OTP's info reports.
parse(Options, Names, Check, Defaults) ->
    Set = lists:foldl(
        fun(Name, Acc) ->
            case lists:keyfind(Name, 1, Options) of
                false -> Acc;
                {_, Value} -> checked(Check(Name, Value), Name, Value, Acc)
            end
        end,
        Defaults,
        Names
    ),
    [report(io_lib:format("Invalid option ~p ignored ~n", [O])) || O <- Options, not known(O, Names)],
    Set.

checked(true, Name, Value, Set) ->
    Set#{Name => Value};
checked(false, Name, Value, Set) ->
    report(io_lib:format("Invalid option ~p:~p ignored ~n", [Name, Value])),
    Set.

known({Name, _}, Names) -> lists:member(Name, Names);
known(_, _) -> false.

report(Report) ->
    error_logger:info_report(Report).

timeout(infinity) -> true;
timeout(Value) -> is_integer(Value) andalso Value >= 0.

%% --- Profile options ---

% The profile options New over Current: ok when every one is valid, else OTP's error for the first invalid one.
profile_options(New, Current) ->
    case lists:dropwhile(fun({Name, Value}) -> profile_option(Name, Value) =:= true end, New) of
        [] -> combination(lists:foldl(fun({N, _} = O, Acc) -> lists:keystore(N, 1, Acc, O) end, Current, New));
        [{Name, Value} | _] -> profile_error(profile_option(Name, Value), Name, Value)
    end.

profile_error(unknown, Name, Value) -> {error, {not_an_option, {Name, Value}}};
profile_error(false, Name, Value) -> {error, {error, {bad_option, Name, Value}}}.

% A local address family goes with a Unix socket, the others without one.
combination(Options) ->
    case {proplists:get_value(ipfamily, Options), proplists:get_value(unix_socket, Options)} of
        {local, Path} when is_list(Path) -> ok;
        {Family, undefined} when Family =/= local -> ok;
        {Family, Path} -> {error, {error, {bad_ipfamily_unix_socket_combination, Family, Path}}}
    end.

profile_option(proxy, Value) -> proxy(Value);
profile_option(https_proxy, Value) -> proxy(Value);
profile_option(pipeline_timeout, Value) -> timeout(Value);
profile_option(max_pipeline_length, Value) -> count(Value);
profile_option(max_keep_alive_length, Value) -> count(Value);
profile_option(keep_alive_timeout, Value) -> timeout(Value);
profile_option(max_sessions, Value) -> count(Value);
profile_option(cookies, Value) -> lists:member(Value, [enabled, disabled, verify]);
profile_option(verbose, Value) -> lists:member(Value, [false, verbose, debug, trace]);
profile_option(ipfamily, Value) -> lists:member(Value, [inet, inet6, inet6fb4, local]);
profile_option(ip, Value) -> is_tuple(Value);
profile_option(port, Value) -> is_integer(Value);
profile_option(socket_opts, Value) -> is_list(Value);
profile_option(unix_socket, _) -> true;
profile_option(max_connections_open, Value) -> Value =:= infinity orelse is_integer(Value);
profile_option(_, _) -> unknown.

proxy({{Host, Port}, NoProxy}) -> is_list(Host) andalso is_integer(Port) andalso is_list(NoProxy);
proxy(_) -> false.

count(Value) -> is_integer(Value) andalso Value >= 0.
