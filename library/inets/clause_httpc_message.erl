%% httpc's HTTP/1.1 messages (plan 21 step MIM4, docs/libraries/inets.md#httpc): request bytes with OTP's header
%% order and defaults, and responses parsed incrementally into OTP's status lines, header lists and bodies.
-module(clause_httpc_message).
-export([request/1, request_uri/1, host_header/1, parse/3, finish/2, result_headers/1, new_parser/0]).

% The request header fields OTP's header record holds, in its field order; requests write them last to first.
-define(REQUEST_FIELDS, [
    "cache-control",
    "connection",
    "date",
    "pragma",
    "trailer",
    "transfer-encoding",
    "upgrade",
    "via",
    "warning",
    "accept",
    "accept-charset",
    "accept-encoding",
    "accept-language",
    "authorization",
    "expect",
    "from",
    "host",
    "if-match",
    "if-modified-since",
    "if-none-match",
    "if-range",
    "if-unmodified-since",
    "max-forwards",
    "proxy-authorization",
    "range",
    "referer",
    "te",
    "user-agent",
    "allow",
    "content-encoding",
    "content-language",
    "content-length",
    "content-location",
    "content-md5",
    "content-range",
    "content-type",
    "expires",
    "last-modified"
]).
% The response header fields OTP's response record holds, in its field order; results list them first.
-define(RESPONSE_FIELDS, [
    "cache-control",
    "connection",
    "date",
    "pragma",
    "trailer",
    "transfer-encoding",
    "upgrade",
    "via",
    "warning",
    "accept-ranges",
    "age",
    "etag",
    "location",
    "proxy-authenticate",
    "retry-after",
    "server",
    "vary",
    "www-authenticate",
    "allow",
    "content-encoding",
    "content-language",
    "content-length",
    "content-location",
    "content-md5",
    "content-range",
    "content-type",
    "expires",
    "last-modified"
]).

%% --- Requests ---

% A request's bytes: the request line, the headers and the body (an iodata body; fun bodies are sent separately).
request(#{method := Method, version := Version} = Request) ->
    Line = [string:uppercase(atom_to_list(Method)), " ", request_uri(Request), " ", Version, "\r\n"],
    [Line, [[Name, ": ", Value, "\r\n"] || {Name, Value} <- headers(Request)], "\r\n", body(Request)].

% The request target: the path and query, or the absolute URI when a proxy carries the request.
request_uri(#{proxy := true, scheme := Scheme} = Request) ->
    [atom_to_list(Scheme), "://", host_header(Request), path_query(Request)];
request_uri(Request) ->
    path_query(Request).

path_query(#{path := Path, query := []}) -> Path;
path_query(#{path := Path, query := Query}) -> [Path, "?", Query].

% The host header: the host, with the port when it is not the scheme's.
host_header(#{scheme := http, host := Host, port := 80}) -> Host;
host_header(#{scheme := https, host := Host, port := 443}) -> Host;
host_header(#{host := Host, port := Port}) -> Host ++ ":" ++ integer_to_list(Port).

body(#{body := Body}) when is_list(Body); is_binary(Body) -> Body;
body(_) -> [].

% The header lines, as OTP writes them: user headers as given (headers_as_is, last first), or the record's fields
% last to first with their defaults, then the other headers in the given order.
headers(#{headers_as_is := true, headers := Headers}) ->
    lists:reverse(Headers);
headers(#{headers := Headers} = Request) ->
    {Fields, Others} = lists:foldl(fun field/2, {#{}, []}, Headers),
    Set = defaults(Request, Fields),
    [{Name, map_get(Name, Set)} || Name <- lists:reverse(?REQUEST_FIELDS), is_map_key(Name, Set)] ++
        lists:reverse(Others).

% One user header into the record fields (the last value wins) or the other headers.
field({Name, Value}, {Fields, Others}) ->
    case lists:member(Name, ?REQUEST_FIELDS) of
        true -> {Fields#{Name => Value}, Others};
        false -> {Fields, [{Name, Value} | Others]}
    end.

% The fields httpc sets: host and connection by version, authorization from the URI's user, the content's type and
% length, and TE named in connection when a te header is sent.
defaults(Request, Fields0) ->
    Fields1 = maps:remove("content-length", Fields0),
    Fields2 = version_fields(Request, Fields1),
    Fields3 = user_fields(Request, Fields2),
    Fields4 = content_fields(Request, Fields3),
    case Fields4 of
        #{"te" := _, "connection" := Connection} -> Fields4#{"connection" => Connection ++ ", TE"};
        _ -> Fields4
    end.

version_fields(#{version := "HTTP/1.1", close := Close} = Request, Fields) ->
    Connection =
        case Close of
            true -> "close";
            false -> "keep-alive"
        end,
    maps:merge(#{"host" => host_header(Request), "connection" => Connection}, Fields);
version_fields(#{version := "HTTP/1.0"}, Fields) ->
    maps:merge(#{"connection" => "close"}, Fields);
version_fields(_, Fields) ->
    maps:merge(#{"connection" => "keep-alive"}, Fields).

user_fields(#{userinfo := undefined} = Request, Fields) ->
    proxy_fields(Request, Fields);
user_fields(#{userinfo := User} = Request, Fields) ->
    proxy_fields(Request, maps:merge(#{"authorization" => basic(User)}, Fields)).

proxy_fields(#{proxy := true, proxy_auth := {User, Password}}, Fields) ->
    maps:merge(#{"proxy-authorization" => basic(User ++ ":" ++ Password)}, Fields);
proxy_fields(_, Fields) ->
    Fields.

% Basic credentials of a user, "user" alone standing for "user:".
basic(User) ->
    Credentials =
        case lists:member($:, User) of
            true -> User;
            false -> User ++ ":"
        end,
    "Basic " ++ base64:encode_to_string(unicode:characters_to_binary(Credentials, latin1)).

content_fields(#{content_type := undefined, content_length := undefined}, Fields) ->
    Fields;
content_fields(#{content_type := undefined, content_length := Length}, Fields) ->
    Fields#{"content-length" => Length};
content_fields(#{content_type := Type, content_length := Length}, Fields) ->
    Fields#{"content-type" => Type, "content-length" => Length}.

%% --- Responses ---

% A parser for one connection's next response.
new_parser() ->
    #{stage => status, buffer => <<>>, body => [], size => 0}.

% Feed received bytes to a response of Method: {more, Parser}, {response, Response, Rest} or {error, Reason}.
% A response is #{status, fields, received_fields, body}.
parse(Data, Method, #{buffer := Buffer} = Parser) ->
    step(Method, Parser#{buffer := <<Buffer/binary, Data/binary>>}).

step(Method, #{stage := status, buffer := Buffer} = Parser) ->
    case status_line(Buffer) of
        more -> {more, Parser};
        {error, _} = Error -> Error;
        {ok, Status, Rest} -> step(Method, Parser#{stage := headers, status => Status, fields => [], buffer := Rest})
    end;
step(Method, #{stage := headers, buffer := Buffer, fields := Fields} = Parser) ->
    case binary:split(Buffer, <<"\r\n">>) of
        [_] -> {more, Parser};
        [<<>>, Rest] -> headers_done(Method, Parser#{buffer := Rest, fields := lists:reverse(Fields)});
        [Line, Rest] -> step(Method, Parser#{buffer := Rest, fields := [header_line(Line) | Fields]})
    end;
step(_Method, #{stage := {length, Left}, buffer := Buffer} = Parser) when byte_size(Buffer) >= Left ->
    <<Data:Left/binary, Rest/binary>> = Buffer,
    done(add(Data, Parser#{buffer := Rest}));
step(_Method, #{stage := {length, Left}, buffer := Buffer} = Parser) ->
    {more, add(Buffer, Parser#{stage := {length, Left - byte_size(Buffer)}, buffer := <<>>})};
step(_Method, #{stage := until_close, buffer := Buffer} = Parser) ->
    {more, add(Buffer, Parser#{buffer := <<>>})};
step(Method, #{stage := chunk_size, buffer := Buffer} = Parser) ->
    case binary:split(Buffer, <<"\r\n">>) of
        [_] -> {more, Parser};
        [Line, Rest] -> chunk_size(Method, chunk_length(Line), Parser#{buffer := Rest})
    end;
step(Method, #{stage := {chunk, Size}, buffer := Buffer} = Parser) when byte_size(Buffer) >= Size + 2 ->
    <<Data:Size/binary, "\r\n", Rest/binary>> = Buffer,
    step(Method, add(Data, Parser#{stage := chunk_size, buffer := Rest}));
step(_Method, #{stage := {chunk, _}} = Parser) ->
    {more, Parser};
step(Method, #{stage := trailers, buffer := Buffer, fields := Fields} = Parser) ->
    case binary:split(Buffer, <<"\r\n">>) of
        [_] -> {more, Parser};
        [<<>>, Rest] -> done(chunked_fields(Parser#{buffer := Rest}));
        [Line, Rest] -> step(Method, Parser#{buffer := Rest, fields := Fields ++ [header_line(Line)]})
    end.

% "HTTP/x.y Code Reason\r\n": the version up to a space must be HTTP's and the code up to the next space a number.
status_line(Buffer) ->
    case binary:split(Buffer, <<" ">>) of
        [_] ->
            more;
        [<<"HTTP/", _/binary>> = Version, Rest] ->
            code(Version, Rest, Buffer);
        [Version, _] ->
            {error, {invalid_version, binary_to_list(Version)}}
    end.

code(Version, Rest, Buffer) ->
    case binary:split(Rest, <<" ">>) of
        [_] ->
            more;
        [Code, Rest2] ->
            case {number(Code), binary:split(Rest2, <<"\r\n">>)} of
                {error, _} -> {error, {could_not_parse_as_http, Buffer}};
                {_, [_]} -> more;
                {Number, [Reason, Rest3]} -> {ok, {binary_to_list(Version), Number, binary_to_list(Reason)}, Rest3}
            end
    end.

number(Code) ->
    try
        binary_to_integer(Code)
    catch
        error:badarg -> error
    end.

% A header line as a lowercase name and its value without surrounding blanks.
header_line(Line) ->
    {Name, Value} =
        case binary:split(Line, <<":">>) of
            [N, V] -> {N, V};
            [N] -> {N, <<>>}
        end,
    {string:lowercase(binary_to_list(string:trim(Name))), binary_to_list(string:trim(Value, both, " \t"))}.

% After the headers: an informational response is skipped, then the body is framed by the method, the status, the
% transfer coding or the length, else read until the connection closes.
headers_done(Method, #{status := {_, Code, _}, buffer := Rest}) when Code >= 100, Code < 200 ->
    step(Method, (new_parser())#{buffer := Rest});
headers_done(Method, #{status := {_, Code, _}, fields := Fields} = Parser0) ->
    Parser = Parser0#{received_fields => Fields},
    case framing(Method, Code, Fields) of
        none -> done(Parser);
        chunked -> step(Method, Parser#{stage := chunk_size});
        {length, 0} -> done(Parser);
        {length, Length} -> step(Method, Parser#{stage := {length, Length}});
        until_close -> step(Method, Parser#{stage := until_close})
    end.

framing(head, _, _) ->
    none;
framing(_, Code, _) when Code =:= 204; Code =:= 304 ->
    none;
framing(_, _, Fields) ->
    case {first("transfer-encoding", Fields), first("content-length", Fields)} of
        {{ok, Coding}, _} ->
            case string:find(string:lowercase(Coding), "chunked") of
                nomatch -> length_framing(first("content-length", Fields));
                _ -> chunked
            end;
        {error, Length} ->
            length_framing(Length)
    end.

length_framing({ok, Length}) ->
    case string:to_integer(Length) of
        {N, []} when N >= 0 -> {length, N};
        _ -> until_close
    end;
length_framing(error) ->
    until_close.

chunk_length(Line) ->
    [Hex | _] = binary:split(Line, <<";">>),
    binary_to_integer(string:trim(Hex), 16).

chunk_size(Method, 0, Parser) -> step(Method, Parser#{stage := trailers});
chunk_size(Method, Size, Parser) -> step(Method, Parser#{stage := {chunk, Size}}).

% A chunked body's headers: its length as content-length, the coding left out, its trailers after the others.
chunked_fields(#{fields := Fields, size := Size} = Parser) ->
    Kept = [F || {Name, _} = F <- Fields, Name =/= "transfer-encoding", Name =/= "content-length"],
    Parser#{fields := [{"content-length", integer_to_list(Size)} | Kept]}.

add(Data, #{body := Body, size := Size} = Parser) ->
    Parser#{body := [Body, Data], size := Size + byte_size(Data)}.

% The parsed response and the bytes after it; streams start with the headers as received.
done(#{status := Status, fields := Fields, received_fields := Received, body := Body, buffer := Rest}) ->
    {response, #{status => Status, fields => Fields, received_fields => Received, body => iolist_to_binary(Body)},
        Rest}.

% The response of a connection closed by the server: complete when its body ran until the close.
finish(#{stage := until_close} = Parser, _Method) ->
    {response, Response, _} = done(Parser),
    {ok, Response};
finish(_Parser, _Method) ->
    error.

% A response's headers as OTP's results list them: the record's fields in its order, each with its first value,
% then the others as received.
result_headers(Fields) ->
    Known = [{Name, Value} || Name <- ?RESPONSE_FIELDS, {ok, Value} <- [first(Name, Fields)]],
    Known ++ [F || {Name, _} = F <- Fields, not lists:member(Name, ?RESPONSE_FIELDS)].

first(Name, Fields) ->
    case lists:keyfind(Name, 1, Fields) of
        {_, Value} -> {ok, Value};
        false -> error
    end.
