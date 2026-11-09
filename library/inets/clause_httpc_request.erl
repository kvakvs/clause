%% httpc's requests (plan 21 step MIM4, docs/libraries/inets.md#httpc): the arguments of httpc:request/4,5 checked in
%% OTP's order (method, request form, headers, URI, options) with OTP's errors, as the request a handler sends.
-module(clause_httpc_request).
-export([new/4, at/2]).

-define(METHODS, [get, head, options, trace, post, put, patch, delete]).

% The request, or {error, Reason} as httpc:request returns it.
new(Method, Request, HttpOptions, Options) ->
    case lists:member(Method, ?METHODS) of
        true -> form(Method, Request, HttpOptions, Options);
        false -> {error, invalid_method}
    end.

% The request forms each method takes: no content, content, or either.
form(Method, {Url, Headers}, HttpOptions, Options) when
    Method =/= post, Method =/= patch
->
    Length =
        case Method of
            put -> "0";
            _ -> undefined
        end,
    headers(
        Url,
        Headers,
        #{method => Method, content_type => undefined, content_length => Length, body => []},
        HttpOptions,
        Options
    );
form(Method, {Url, Headers, ContentType, Body}, HttpOptions, Options) when
    (Method =:= post orelse Method =:= patch orelse Method =:= put orelse Method =:= delete),
    is_list(ContentType)
->
    case body(Body) of
        true ->
            Content = #{
                method => Method, content_type => ContentType, content_length => length_of(Body, Headers), body => Body
            },
            headers(Url, Headers, Content, HttpOptions, Options);
        false ->
            {error, invalid_request}
    end;
form(_, _, _, _) ->
    {error, invalid_request}.

body(Body) when is_list(Body); is_binary(Body) -> true;
body({Fun, _}) when is_function(Fun, 1) -> true;
body({chunkify, Fun, _}) when is_function(Fun, 1) -> true;
body(_) -> false.

% The content length: the body's size, or for a fun body the content-length header given (else "0").
length_of(Body, _Headers) when is_list(Body); is_binary(Body) ->
    integer_to_list(iolist_size(Body));
length_of(_, Headers) ->
    case [Value || {Name, Value} <- Headers, is_list(Name), string:lowercase(Name) =:= "content-length"] of
        [] -> "0";
        Values -> lists:last(Values)
    end.

headers(Url, Headers, Content, HttpOptions, Options) ->
    case header_parse(Headers) of
        {error, _} = Error -> Error;
        Parsed -> uri(Url, Content#{headers => Parsed}, HttpOptions, Options)
    end.

% The headers with string values; a name that is not a string or a value that is neither a string nor a binary is
% OTP's headers_error.
header_parse([]) ->
    [];
header_parse([{Name, _} | _]) when not is_list(Name) ->
    {error, {headers_error, invalid_field}};
header_parse([{Name, Value} | Rest]) when is_list(Value); is_binary(Value) ->
    case header_parse(Rest) of
        {error, _} = Error -> Error;
        Parsed -> [{Name, value(Value)} | Parsed]
    end;
header_parse([{_, _} | _]) ->
    {error, {headers_error, invalid_value}}.

value(Value) when is_binary(Value) -> binary_to_list(Value);
value(Value) -> Value.

uri(Url, Request, HttpOptions, Options) when is_binary(Url) ->
    uri(binary_to_list(Url), Request, HttpOptions, Options);
uri(Url, Request, HttpOptions, Options) ->
    case uri_string:normalize(Url, [return_map]) of
        {error, _, _} ->
            {error, invalid_uri};
        #{scheme := Scheme} = Uri when Scheme =:= "http"; Scheme =:= "https" ->
            options(Uri, Request, HttpOptions, Options);
        #{scheme := Scheme} ->
            {error, {bad_scheme, Scheme}};
        #{} ->
            {error, {no_scheme}}
    end.

options(Uri, Request0, HttpOptions, Options) ->
    Http = clause_httpc_options:http_options(HttpOptions),
    Own = clause_httpc_options:request_options(Options),
    % HTTP/1.0 requests and requests with their own socket options use a connection of their own and close it.
    Request1 = maps:merge(Request0, maps:merge(Http, Own)),
    Request = Request1#{close => maps:get(own_socket, Request1) orelse maps:get(version, Request1) =:= "HTTP/1.0"},
    case Request of
        #{stream := self, sync := true} ->
            {error, streaming_error};
        #{headers_as_is := false, headers := Headers} ->
            {ok, at(Uri, Request#{headers := [{string:lowercase(N), V} || {N, V} <- Headers], redirects => 0})};
        #{} ->
            {ok, at(Uri, Request#{redirects => 0})}
    end.

% The request sent to a normalized URI: its scheme, server, path, query and user.
at(Uri, Request) ->
    Scheme = list_to_atom(maps:get(scheme, Uri)),
    Request#{
        scheme => Scheme,
        host => maps:get(host, Uri, []),
        port => maps:get(port, Uri, default_port(Scheme)),
        path => path(maps:get(path, Uri, [])),
        query => maps:get(query, Uri, []),
        userinfo => userinfo(maps:get(userinfo, Uri, undefined))
    }.

default_port(http) -> 80;
default_port(https) -> 443.

path([]) -> "/";
path(Path) -> Path.

userinfo(undefined) -> undefined;
userinfo(User) -> uri_string:percent_decode(User).
