%% Clause's uri_string (plan 21 step LIB5, docs/library.md): RFC 3986 parsing, recomposition, normalization,
%% reference resolution and the form-urlencoded query helpers. Erlang, because OTP's results include where its
%% parser stopped: the error fragments, the userinfo-or-host and IPv4-or-name retries and the thrown errors that
%% escape are kept by walking the same grammar, here as one walker driven by a delimiter table.
-module(uri_string).
-export([
    allowed_characters/0,
    compose_query/1,
    compose_query/2,
    dissect_query/1,
    is_host/1,
    is_path/1,
    normalize/1,
    normalize/2,
    parse/1,
    percent_decode/1,
    quote/1,
    quote/2,
    recompose/1,
    resolve/2,
    resolve/3,
    transcode/2,
    unquote/1
]).

-include("uri_string.types.hrl").

-define(IS_ALPHA(C), (($A =< C andalso C =< $Z) orelse ($a =< C andalso C =< $z))).
-define(IS_DIGIT(C), ($0 =< C andalso C =< $9)).
-define(IS_HEX(C),
    (?IS_DIGIT(C) orelse ($a =< C andalso C =< $f) orelse ($A =< C andalso C =< $F))
).

%% Parsing

%% The components of a URI reference, as a map of binaries or, for a list input, of lists.
parse(URIString) when is_binary(URIString) ->
    try
        reference(URIString)
    catch
        throw:{error, _, _} = Error -> Error
    end;
parse(URIString) when is_list(URIString) ->
    try
        maps:map(fun to_list_field/2, reference(unicode:characters_to_binary(URIString)))
    catch
        throw:{error, _, _} = Error -> Error
    end.

to_list_field(_, Value) when is_binary(Value) -> unicode:characters_to_list(Value);
to_list_field(_, Value) -> Value.

%% A URI with a scheme, else a relative reference; a failure after the scheme's colon is final.
reference(<<>>) ->
    #{path => <<>>};
reference(Input) ->
    try
        scheme_start(Input, #{})
    catch
        throw:{hier_error, Error} -> throw(Error);
        throw:{_, _, _} -> relative_part(Input, #{})
    end.

scheme_start(<<C/utf8, Rest/binary>>, URI) ->
    case ?IS_ALPHA(C) of
        true ->
            {Tail, URI1} = scheme(Rest, URI),
            Scheme = parsed(Rest, Tail, ":"),
            (with_path(URI1))#{scheme => <<C/utf8, Scheme/binary>>};
        false ->
            invalid([C])
    end.

scheme(<<$:, Rest/binary>>, URI) ->
    try hier(Rest, URI) of
        {_, URI1} -> {Rest, URI1}
    catch
        throw:{error, invalid_uri, _} = Error -> throw({hier_error, Error})
    end;
scheme(<<C/utf8, Rest/binary>>, URI) ->
    case is_scheme(C) of
        true -> scheme(Rest, URI);
        false -> invalid([C])
    end;
scheme(<<>>, _) ->
    invalid(<<>>).

%% The hierarchical part after "scheme:".
hier(<<"//", Rest/binary>>, URI) -> authority(Rest, URI);
hier(<<$/, Rest/binary>>, URI) -> path_part($/, segment, Rest, URI);
hier(<<$?, Rest/binary>>, URI) -> delimited(query, Rest, URI);
hier(<<$#, Rest/binary>>, URI) -> delimited(fragment, Rest, URI);
hier(<<C/utf8, Rest/binary>>, URI) -> first_char(C, is_pchar(C), segment, Rest, URI);
hier(<<>>, URI) -> {<<>>, URI}.

%% A relative reference: an authority, an absolute path, a query, a fragment or a path without a colon.
relative_part(<<"//", Rest/binary>>, URI) ->
    with_path(element(2, authority(Rest, URI)));
relative_part(<<$/, Rest/binary>>, URI) ->
    element(2, path_part($/, segment, Rest, URI));
relative_part(<<$?, Rest/binary>>, URI) ->
    with_path(element(2, delimited(query, Rest, URI)));
relative_part(<<$#, Rest/binary>>, URI) ->
    with_path(element(2, delimited(fragment, Rest, URI)));
relative_part(<<C/utf8, Rest/binary>>, URI) ->
    element(2, first_char(C, is_segment_nz_nc(C), segment_nz_nc, Rest, URI)).

first_char(C, true, Kind, Rest, URI) -> path_part(C, Kind, Rest, URI);
first_char(C, false, _, _, _) -> invalid([C]).

%% userinfo@host when the userinfo walk succeeds, else the authority as a host from its start.
authority(Input, URI) ->
    try walk(userinfo, Input, URI) of
        {Tail, URI1} -> {Input, URI1#{userinfo => parsed(Input, Tail, "?#@")}}
    catch
        throw:{_, _, _} -> host_part(Input, URI)
    end.

host_part(Input, URI) ->
    {Tail, URI1} = host(Input, URI),
    {Input, URI1#{host => remove_brackets(parsed(Input, Tail, ":?#/"))}}.

%% A bracketed IPv6 address, an IPv4 address (a registered name when it is not one) or a registered name.
host(<<$[, Rest/binary>>, URI) ->
    ipv6(Rest, [], URI);
host(<<C/utf8, Rest/binary>> = Input, URI) when ?IS_DIGIT(C) ->
    try
        ipv4(Rest, [C], URI)
    catch
        throw:{_, _, _} -> walk(reg_name, Input, URI)
    end;
host(Input, URI) ->
    walk(reg_name, Input, URI).

ipv4(<<C/utf8, Rest/binary>>, Acc, URI) ->
    case delimiter(reg_name, C) of
        none when C =:= $.; ?IS_DIGIT(C) -> ipv4(Rest, [C | Acc], URI);
        none -> invalid([C]);
        Part -> after_address(ipv4, Acc, fun() -> delimited(Part, Rest, URI) end)
    end;
ipv4(<<>>, Acc, URI) ->
    after_address(ipv4, Acc, fun() -> {<<>>, URI} end).

ipv6(<<$], Rest/binary>>, Acc, URI) ->
    after_address(ipv6, Acc, fun() -> walk(ipv6_end, Rest, URI) end);
ipv6(<<C/utf8, Rest/binary>>, Acc, URI) ->
    case is_ipv6(C) of
        true -> ipv6(Rest, [C | Acc], URI);
        false -> invalid([C])
    end;
ipv6(<<>>, _, _) ->
    invalid(<<>>).

%% Continue once the reversed address characters form a strict address of the family.
after_address(Family, Acc, Continue) ->
    Address = lists:reverse(Acc),
    Parsed =
        case Family of
            ipv4 -> inet:parse_ipv4strict_address(Address);
            ipv6 -> inet:parse_ipv6strict_address(Address)
        end,
    case Parsed of
        {ok, _} -> Continue();
        {error, _} -> invalid(Address)
    end.

%% Walks the characters of one component kind until a delimiter hands over to the next component; returns the
%% input after that delimiter (empty at the end) and the map with the later components.
walk(Kind, <<C/utf8, Rest/binary>>, URI) ->
    case delimiter(Kind, C) of
        none ->
            case allowed(Kind, C) of
                true -> walk(Kind, Rest, URI);
                false -> invalid([C])
            end;
        segment ->
            walk(segment, Rest, URI);
        Part ->
            delimited(Part, Rest, URI)
    end;
walk(userinfo, <<>>, _) ->
    invalid(<<>>);
walk(_, <<>>, URI) ->
    {<<>>, URI}.

%% The component a character starts within a component kind, or none.
delimiter(Kind, $/) when Kind =:= segment; Kind =:= segment_nz_nc -> segment;
delimiter(Kind, $/) when Kind =:= port; Kind =:= reg_name; Kind =:= ipv6_end -> path;
delimiter(Kind, $:) when Kind =:= reg_name; Kind =:= ipv6_end -> port;
delimiter(userinfo, $@) -> host;
delimiter(Kind, $?) when Kind =/= query, Kind =/= fragment, Kind =/= userinfo -> query;
delimiter(Kind, $#) when Kind =/= fragment, Kind =/= userinfo -> fragment;
delimiter(_, _) -> none.

allowed(segment, C) -> is_pchar(C);
allowed(segment_nz_nc, C) -> is_segment_nz_nc(C);
allowed(query, C) -> is_query(C);
allowed(fragment, C) -> is_fragment(C);
allowed(port, C) -> ?IS_DIGIT(C);
allowed(reg_name, C) -> is_reg_name(C);
allowed(ipv6_end, C) -> is_ipv6(C);
allowed(userinfo, C) -> is_userinfo(C).

%% The component after a delimiter, added to the map.
delimited(path, Rest, URI) ->
    path_part($/, segment, Rest, URI);
delimited(port, Rest, URI) ->
    {Tail, URI1} = walk(port, Rest, URI),
    {Rest, URI1#{port => port_number(parsed(Rest, Tail, ":?#/"))}};
delimited(host, Rest, URI) ->
    host_part(Rest, URI);
delimited(Part, Rest, URI) ->
    {Tail, URI1} = walk(Part, Rest, URI),
    {Rest, URI1#{Part => parsed(Rest, Tail, "#")}}.

path_part(First, Kind, Rest, URI) ->
    {Tail, URI1} = walk(Kind, Rest, URI),
    Path = parsed(Rest, Tail, "?#"),
    {Rest, URI1#{path => <<First/utf8, Path/binary>>}}.

%% The part of Input before the delimiter that Tail follows; at the end, Input without a trailing delimiter.
parsed(<<>>, <<>>, _) ->
    <<>>;
parsed(Input, <<>>, Delimiters) ->
    case lists:member(binary:last(Input), Delimiters) of
        true -> binary:part(Input, 0, byte_size(Input) - 1);
        false -> Input
    end;
parsed(Input, Tail, _) ->
    binary:part(Input, 0, byte_size(Input) - byte_size(Tail) - 1).

port_number(<<>>) ->
    undefined;
port_number(Digits) when byte_size(Digits) =< 5 ->
    try
        binary_to_integer(Digits)
    catch
        error:badarg -> invalid(Digits)
    end;
port_number(Digits) ->
    invalid(Digits).

remove_brackets(<<$[, Rest/binary>>) ->
    {Address, Last} = split_binary(Rest, byte_size(Rest) - 1),
    case Last of
        <<$]>> -> Address;
        _ -> Rest
    end;
remove_brackets(Host) ->
    Host.

with_path(#{path := _} = URI) -> URI;
with_path(URI) -> URI#{path => <<>>}.

invalid(Fragment) -> throw({error, invalid_uri, Fragment}).

%% Recomposition

%% The URI string of a map: list when the first component present is a list, else binary.
recompose(Map) ->
    case valid_map(Map) of
        false ->
            {error, invalid_map, Map};
        true ->
            try
                lists:foldl(fun(Step, URI) -> Step(Map, URI) end, empty, [
                    fun add_scheme/2,
                    fun add_userinfo/2,
                    fun add_host/2,
                    fun add_port/2,
                    fun add_path/2,
                    fun add_query/2,
                    fun add_fragment/2
                ])
            catch
                throw:{error, _, _} = Error -> Error
            end
    end.

%% A map with a path and only URI component keys.
valid_map(#{path := _} = Map) ->
    lists:all(
        fun(Key) -> lists:member(Key, [scheme, userinfo, host, port, path, query, fragment]) end,
        maps:keys(Map)
    );
valid_map(#{}) ->
    false.

add_scheme(#{scheme := Scheme}, _) -> concat(scheme_text(Scheme), <<":">>);
add_scheme(#{}, _) -> empty.

add_userinfo(#{userinfo := Userinfo}, URI) ->
    after_part(URI, prefixed("//", encode(Userinfo, fun is_userinfo/1)));
add_userinfo(#{}, URI) ->
    URI.

add_host(#{host := Host}, empty) -> prefixed("//", host_text(Host));
add_host(#{host := Host, userinfo := _}, URI) -> concat(URI, prefixed("@", host_text(Host)));
add_host(#{host := Host}, URI) -> concat(URI, prefixed("//", host_text(Host)));
add_host(#{}, URI) -> URI.

add_port(#{port := undefined}, URI) -> concat(URI, <<":">>);
add_port(#{port := Port}, URI) -> concat(URI, <<$:, (integer_to_binary(Port))/binary>>);
add_port(#{}, URI) -> URI.

add_path(#{path := Path}, empty) ->
    encode(Path, fun is_path/1);
add_path(#{host := _, path := Path}, URI) ->
    concat(URI, encode(absolute_path(flat_path(Path)), fun is_path/1));
add_path(#{path := Path}, URI) ->
    concat(URI, encode(Path, fun is_path/1));
add_path(#{}, URI) ->
    URI.

add_query(#{query := Query}, empty) -> encode(Query, fun is_query/1);
add_query(#{query := Query}, URI) -> concat(URI, prefixed("?", encode(Query, fun is_query/1)));
add_query(#{}, URI) -> URI.

add_fragment(#{fragment := Fragment}, URI) ->
    after_part(URI, prefixed("#", encode(Fragment, fun is_fragment/1)));
add_fragment(#{}, empty) ->
    "";
add_fragment(#{}, URI) ->
    URI.

after_part(empty, Part) -> Part;
after_part(URI, Part) -> concat(URI, Part).

%% Text prefixed, keeping its binary or list form.
prefixed(Prefix, Text) when is_binary(Text) -> <<(list_to_binary(Prefix))/binary, Text/binary>>;
prefixed(Prefix, Text) when is_list(Text) -> Prefix ++ Text.

%% A binary joined with a binary; anything joined with a list gives a list.
concat(A, B) when is_binary(A), is_binary(B) -> <<A/binary, B/binary>>;
concat(A, B) when is_binary(A), is_list(B) -> unicode:characters_to_list(A) ++ B;
concat(A, B) when is_list(A), is_binary(B) -> A ++ unicode:characters_to_list(B);
concat(A, B) when is_list(A) -> A ++ B.

scheme_text([]) ->
    throw({error, invalid_scheme, ""});
scheme_text(<<>>) ->
    throw({error, invalid_scheme, <<>>});
scheme_text(Scheme) ->
    Chars =
        case Scheme of
            <<_/binary>> -> binary_to_list(Scheme);
            _ -> Scheme
        end,
    case lists:all(fun is_scheme/1, Chars) of
        true -> Scheme;
        false -> throw({error, invalid_scheme, Scheme})
    end.

%% A host as it is when it is an address or a registered name, bracketed when IPv6, else percent-encoded.
host_text([]) ->
    encode([], fun is_reg_name/1);
host_text(Host) ->
    Chars =
        case Host of
            <<_/binary>> -> unicode:characters_to_list(Host);
            _ -> Host
        end,
    case host_class(Chars) of
        ipv6 when is_binary(Host) -> <<$[, Host/binary, $]>>;
        ipv6 -> [$[ | Host] ++ "]";
        other -> encode(Host, fun is_reg_name/1);
        _ -> Host
    end.

host_class(Chars) ->
    case inet:parse_ipv6strict_address(Chars) of
        {ok, _} ->
            ipv6;
        {error, _} ->
            case inet:parse_ipv4strict_address(Chars) of
                {ok, _} -> ipv4;
                {error, _} -> registered_name(Chars)
            end
    end.

registered_name([C | Rest]) ->
    case is_reg_name(C) of
        true -> registered_name(Rest);
        false -> other
    end;
registered_name([]) ->
    regname.

flat_path(Path) when is_binary(Path) -> Path;
flat_path(Path) -> unicode:characters_to_list(Path).

absolute_path(<<>>) -> <<>>;
absolute_path("") -> "";
absolute_path(<<$/, _/binary>> = Path) -> Path;
absolute_path([$/ | _] = Path) -> Path;
absolute_path(Path) when is_binary(Path) -> <<$/, Path/binary>>;
absolute_path(Path) when is_list(Path) -> [$/ | Path].

%% Text with the characters Keep rejects percent-encoded as UTF-8; a list stays a list.
encode(Text, Keep) when is_list(Text) ->
    unicode:characters_to_list(encode(unicode:characters_to_binary(Text), Keep, <<>>));
encode(Text, Keep) when is_binary(Text) ->
    encode(Text, Keep, <<>>).

encode(<<C/utf8, Rest/binary>>, Keep, Acc) ->
    case Keep(C) of
        true -> encode(Rest, Keep, <<Acc/binary, C>>);
        false -> encode(Rest, Keep, <<Acc/binary, (percent_encoded(<<C/utf8>>))/binary>>)
    end;
encode(<<_, _/binary>> = Rest, _, _) ->
    throw({error, invalid_input, Rest});
encode(<<>>, _, Acc) ->
    Acc.

percent_encoded(Bytes) ->
    <<<<$%, (hex_digit(B bsr 4)), (hex_digit(B band 15))>> || <<B>> <= Bytes>>.

hex_digit(N) when N < 10 -> $0 + N;
hex_digit(N) -> $A + N - 10.

hex_value(C) when ?IS_DIGIT(C) -> C - $0;
hex_value(C) when $a =< C, C =< $f -> C - $a + 10;
hex_value(C) -> C - $A + 10.

%% Normalization

%% A URI with lower-case scheme and host, unreserved characters decoded, default ports dropped and dot segments
%% removed; as a string or, with [return_map], a map.
normalize(URI) ->
    normalize(URI, []).

normalize(URIMap, []) when is_map(URIMap) ->
    normalized(fun() -> recompose(normalize_map(URIMap)) end);
normalize(URIMap, [return_map]) when is_map(URIMap) ->
    normalized(fun() -> normalize_map(URIMap) end);
normalize(URIString, []) ->
    parsed_then(URIString, fun(Map) -> normalized(fun() -> recompose(normalize_map(Map)) end) end);
normalize(URIString, [return_map]) ->
    parsed_then(URIString, fun(Map) -> normalized(fun() -> normalize_map(Map) end) end).

normalized(Fun) ->
    try
        Fun()
    catch
        throw:{error, _, _} = Error -> Error
    end.

parsed_then(URIString, Fun) ->
    case parse(URIString) of
        Map when is_map(Map) -> Fun(Map);
        Error -> Error
    end.

normalize_map(Map) ->
    Lowered = maps:map(fun lower_field/2, Map),
    Decoded = maps:map(fun decoded_field/2, Lowered),
    remove_dots(by_scheme(without_undefined_port(Decoded))).

lower_field(Key, Value) when Key =:= scheme; Key =:= host -> to_lower(Value);
lower_field(_, Value) -> Value.

decoded_field(Key, Value) when
    Key =:= userinfo; Key =:= host; Key =:= path; Key =:= query; Key =:= fragment
->
    normalize_decode(Value);
decoded_field(_, Value) ->
    Value.

to_lower(Text) when is_list(Text) ->
    to_list(to_lower(to_binary(Text, utf8, utf8)), utf8);
to_lower(Text) when is_binary(Text) ->
    <<<<(lower_byte(C))>> || <<C>> <= Text>>.

lower_byte(C) when $A =< C, C =< $Z -> C + 32;
lower_byte(C) -> C.

%% Percent-encoded unreserved characters decoded, the other escapes upper-cased.
normalize_decode(Text) when is_list(Text) ->
    unicode:characters_to_list(normalize_decode(unicode:characters_to_binary(Text), <<>>));
normalize_decode(Text) ->
    normalize_decode(Text, <<>>).

normalize_decode(<<$%, H, L, Rest/binary>>, Acc) ->
    Byte = escaped(H, L),
    case is_unreserved(Byte) of
        true -> normalize_decode(Rest, <<Acc/binary, Byte>>);
        false -> normalize_decode(Rest, <<Acc/binary, $%, (upper_hex(H)), (upper_hex(L))>>)
    end;
normalize_decode(<<C, Rest/binary>>, Acc) ->
    normalize_decode(Rest, <<Acc/binary, C>>);
normalize_decode(<<>>, Acc) ->
    valid_utf8(Acc).

upper_hex(C) when $a =< C, C =< $f -> C - 32;
upper_hex(C) -> C.

%% The byte of the escape %HL; invalid_percent_encoding when H or L is no hex digit.
escaped(H, L) when ?IS_HEX(H), ?IS_HEX(L) -> hex_value(H) * 16 + hex_value(L);
escaped(H, L) -> throw({error, invalid_percent_encoding, <<$%, H, L>>}).

valid_utf8(Bytes) ->
    case unicode:characters_to_list(Bytes) of
        {incomplete, _, _} -> throw({error, invalid_utf8, Bytes});
        {error, _, _} -> throw({error, invalid_utf8, Bytes});
        _ -> Bytes
    end.

without_undefined_port(#{port := undefined} = Map) -> maps:remove(port, Map);
without_undefined_port(Map) -> Map.

%% The scheme's default port dropped; an empty http or https path made "/".
by_scheme(Map) ->
    Scheme =
        case maps:get(scheme, Map, undefined) of
            <<_/binary>> = Binary -> binary_to_list(Binary);
            Other -> Other
        end,
    Port = maps:get(port, Map, undefined),
    case default_port(Scheme) of
        none -> Map;
        Port -> root_path(Scheme, maps:remove(port, Map));
        _ -> root_path(Scheme, Map)
    end.

default_port("http") -> 80;
default_port("https") -> 443;
default_port("ftp") -> 21;
default_port("ssh") -> 22;
default_port("sftp") -> 22;
default_port("tftp") -> 69;
default_port(_) -> none.

root_path(Scheme, Map) when Scheme =:= "http"; Scheme =:= "https" ->
    case maps:get(path, Map, undefined) of
        "" -> Map#{path => "/"};
        <<>> -> Map#{path => <<"/">>};
        _ -> Map
    end;
root_path(_, Map) ->
    Map.

remove_dots(Map) ->
    Map#{path => without_dot_segments(maps:get(path, Map, <<>>))}.

%% RFC 3986's remove_dot_segments over the path's bytes.
without_dot_segments(Path) when is_binary(Path) ->
    dot_segments(Path, <<>>);
without_dot_segments(Path) when is_list(Path) ->
    to_list(dot_segments(to_binary(Path, utf8, utf8), <<>>), utf8).

dot_segments(<<>>, Out) ->
    Out;
dot_segments(<<"../", Rest/binary>>, Out) ->
    dot_segments(Rest, Out);
dot_segments(<<"./", Rest/binary>>, Out) ->
    dot_segments(Rest, Out);
dot_segments(<<"/./", Rest/binary>>, Out) ->
    dot_segments(<<$/, Rest/binary>>, Out);
dot_segments(<<"/.">>, Out) ->
    dot_segments(<<$/>>, Out);
dot_segments(<<"/../", Rest/binary>>, Out) ->
    dot_segments(<<$/, Rest/binary>>, without_last_segment(Out));
dot_segments(<<"/..">>, Out) ->
    dot_segments(<<$/>>, without_last_segment(Out));
dot_segments(<<".">>, Out) ->
    Out;
dot_segments(<<"..">>, Out) ->
    Out;
dot_segments(<<First, Rest/binary>>, Out) ->
    move_segment(Rest, <<Out/binary, First>>).

%% The rest of the segment begun by the byte just moved, up to the next slash.
move_segment(<<C, Rest/binary>>, Out) when C =/= $/ -> move_segment(Rest, <<Out/binary, C>>);
move_segment(Rest, Out) -> dot_segments(Rest, Out).

without_last_segment(<<>>) ->
    <<>>;
without_last_segment(Out) ->
    case split_binary(Out, byte_size(Out) - 1) of
        {Init, <<$/>>} -> Init;
        {Init, _} -> without_last_segment(Init)
    end.

%% Resolution

%% A reference resolved against a base URI (map or string), as RFC 3986 section 5.2 describes.
resolve(URI, Base) ->
    resolve(URI, Base, []).

resolve(URIMap, Base, Options) when is_map(URIMap) ->
    case resolve_map(URIMap, Base) of
        Target when is_map(Target) ->
            case Options of
                [return_map] -> Target;
                [] -> recompose(Target)
            end;
        Error ->
            Error
    end;
resolve(URIString, Base, Options) ->
    parsed_then(URIString, fun(Map) -> resolve(Map, Base, Options) end).

resolve_map(#{scheme := _} = URI, _) ->
    remove_dots(URI);
resolve_map(URI, #{scheme := _} = Base) ->
    resolve_against(URI, Base, path_type(URI));
resolve_map(_, Base) when is_map(Base) ->
    {error, invalid_scheme, ""};
resolve_map(URI, BaseString) ->
    case parse(BaseString) of
        #{scheme := _} = Base -> resolve_against(URI, Base, path_type(URI));
        Base when is_map(Base) -> {error, invalid_scheme, ""};
        Error -> Error
    end.

path_type(URI) ->
    case iolist_to_binary(maps:get(path, URI, <<>>)) of
        <<>> -> empty_path;
        <<$/, _/bits>> -> absolute_path;
        _ -> relative_path
    end.

-define(AUTHORITY, [scheme, userinfo, host, port]).

resolve_against(#{host := _} = URI, #{scheme := Scheme}, _) ->
    remove_dots(URI#{scheme => Scheme});
resolve_against(URI, Base, empty_path) when is_map_key(query, URI) ->
    maps:merge(URI, maps:with(?AUTHORITY ++ [path], Base));
resolve_against(URI, Base, empty_path) ->
    maps:merge(URI, maps:with(?AUTHORITY ++ [path, query], Base));
resolve_against(URI, Base, absolute_path) ->
    remove_dots(maps:merge(URI, maps:with(?AUTHORITY, Base)));
resolve_against(#{path := Path} = URI, Base, relative_path) ->
    remove_dots(maps:merge(URI#{path => merged_path(Path, Base)}, maps:with(?AUTHORITY, Base))).

%% A relative path appended to the base path's directory, or to "/" under an authority with an empty path.
merged_path(Path, #{path := BasePath} = Base) ->
    case {is_map_key(host, Base), iolist_size(BasePath)} of
        {true, 0} when is_binary(Path) -> <<$/, Path/binary>>;
        {true, 0} -> unicode:characters_to_list([$/, Path]);
        _ -> appended_path(base_directory(unicode:characters_to_list(BasePath)), Path)
    end.

appended_path(none, Path) ->
    Path;
appended_path(Directory, Path) when is_binary(Path) ->
    unicode:characters_to_binary([Directory, $/, Path]);
appended_path(Directory, Path) ->
    unicode:characters_to_list([Directory, $/, Path]).

%% The characters before the last grapheme that is a slash (string:split/3 with trailing), or none.
base_directory(Chars) -> base_directory(unicode_util:gc(Chars), [], none).

base_directory([$/ | Rest], Seen, _) ->
    base_directory(unicode_util:gc(Rest), [$/ | Seen], Seen);
base_directory([Grapheme | Rest], Seen, Last) ->
    base_directory(unicode_util:gc(Rest), [Grapheme | Seen], Last);
base_directory([], _, none) ->
    none;
base_directory([], _, Before) ->
    lists:flatten(lists:reverse(Before)).

%% Percent decoding and encoding

%% A string or every text component of a map with its escapes decoded.
percent_decode(URIMap) when is_map(URIMap) ->
    try
        maps:map(fun decode_field/2, URIMap)
    catch
        throw:Return -> Return
    end;
percent_decode(URI) when is_list(URI) orelse is_binary(URI) ->
    raw_decode(URI).

decode_field(Key, Value) when
    Key =:= userinfo; Key =:= host; Key =:= path; Key =:= query; Key =:= fragment
->
    case raw_decode(Value) of
        {error, Reason, Input} -> throw({error, {invalid, {Key, {Reason, Input}}}});
        Decoded -> Decoded
    end;
decode_field(_, Value) ->
    Value.

%% Text with its escapes decoded; a list answers errors, a binary throws them.
raw_decode(Text) when is_list(Text) ->
    try
        unicode:characters_to_list(raw_decode(unicode:characters_to_binary(Text), <<>>))
    catch
        throw:{error, _, _} = Error -> Error
    end;
raw_decode(Text) ->
    raw_decode(Text, <<>>).

raw_decode(<<$%, H, L, Rest/binary>>, Acc) -> raw_decode(Rest, <<Acc/binary, (escaped(H, L))>>);
raw_decode(<<C, Rest/binary>>, Acc) -> raw_decode(Rest, <<Acc/binary, C>>);
raw_decode(<<>>, Acc) -> valid_utf8(Acc).

%% Text with every character but the unreserved (and Safe) percent-encoded.
quote(Text) ->
    encode(Text, fun is_unreserved/1).

quote(Text, Safe) ->
    encode(Text, fun(C) -> is_unreserved(C) orelse lists:member(C, Safe) end).

unquote(Text) ->
    raw_decode(Text).

%% Percent-encoded characters re-encoded from one encoding to another.
transcode(URIString, Options) when is_binary(URIString) ->
    try
        {In, Out} = encodings(Options),
        to_binary(transcode_chars(to_list(URIString, In), In, Out), utf8, Out)
    catch
        throw:{error, _, _} = Error -> Error
    end;
transcode(URIString, Options) when is_list(URIString) ->
    {In, Out} = encodings(Options),
    Chars = flatten_input(URIString, In),
    try
        transcode_chars(Chars, In, Out)
    catch
        throw:{error, _, _} = Error -> Error
    end.

encodings(Options) ->
    {
        proplists:get_value(in_encoding, Options, utf8),
        proplists:get_value(out_encoding, Options, utf8)
    }.

%% The characters with each run of escapes decoded in In and encoded again in Out.
transcode_chars([_ | _] = Chars, In, Out) -> transcode_chars(Chars, [], In, Out).

transcode_chars([$%, _, _ | _] = Chars, Acc, In, Out) -> escape_run(Chars, <<>>, Acc, In, Out);
transcode_chars([C | Rest], Acc, In, Out) -> transcode_chars(Rest, [C | Acc], In, Out);
transcode_chars([], Acc, _, _) -> lists:reverse(Acc).

escape_run([$%, H, L | Rest] = Chars, Bytes, Acc, In, Out) ->
    case ?IS_HEX(H) andalso ?IS_HEX(L) of
        true -> escape_run(Rest, <<Bytes/binary, (escaped(H, L))>>, Acc, In, Out);
        false -> throw({error, invalid_percent_encoding, Chars})
    end;
escape_run(Rest, Bytes, Acc, In, Out) ->
    Encoded = to_list(percent_encoded(to_binary(Bytes, In, Out)), utf8),
    transcode_chars(Rest, lists:reverse(Encoded, Acc), In, Out).

%% A deep input list as characters, its binaries decoded in Encoding.
flatten_input([], _) -> [];
flatten_input(List, Encoding) -> flatten_input(List, Encoding, []).

flatten_input([H | T], Encoding, Acc) when is_binary(H) ->
    flatten_input(T, Encoding, lists:reverse(to_list(H, Encoding), Acc));
flatten_input([H | T], Encoding, Acc) when is_list(H) ->
    flatten_input(H ++ T, Encoding, Acc);
flatten_input([H | T], Encoding, Acc) ->
    flatten_input(T, Encoding, [H | Acc]);
flatten_input([], _, Acc) ->
    lists:reverse(Acc);
flatten_input(Other, _, _) ->
    throw({error, invalid_input, Other}).

to_binary(Data, In, Out) ->
    case unicode:characters_to_binary(Data, In, Out) of
        {error, _, Rest} -> throw({error, invalid_input, Rest});
        {incomplete, _, Rest} -> throw({error, invalid_input, Rest});
        Binary -> Binary
    end.

to_list(Data, In) ->
    case unicode:characters_to_list(Data, In) of
        {error, _, Rest} -> throw({error, invalid_input, Rest});
        {incomplete, _, Rest} -> throw({error, invalid_input, Rest});
        List -> List
    end.

%% Form-urlencoded queries

%% A query string from key-value pairs (a true value gives a bare key); a list when any key or value is one.
compose_query(List) ->
    compose_query(List, [{encoding, utf8}]).

compose_query([], _) ->
    [];
compose_query(List, Options) ->
    try
        compose_pairs(List, Options, false, <<>>)
    catch
        throw:{error, _, _} = Error -> Error
    end.

compose_pairs([{Key, Value} | Rest], Options, AnyList, Acc) ->
    Separator =
        case Rest of
            [] -> <<>>;
            _ -> <<"&">>
        end,
    K = form_encode(Key, Options),
    Pair =
        case Value of
            true -> K;
            _ -> <<K/binary, $=, (form_encode(Value, Options))/binary>>
        end,
    Lists = AnyList orelse is_list(Key) orelse is_list(Value),
    compose_pairs(Rest, Options, Lists, <<Acc/binary, Pair/binary, Separator/binary>>);
compose_pairs([], _, true, Acc) ->
    to_list(Acc, utf8);
compose_pairs([], _, false, Acc) ->
    Acc.

form_encode(Text, [{encoding, latin1}]) when is_list(Text) ->
    form_bytes(numeric_references(to_binary(Text, utf8, utf8)));
form_encode(Text, [{encoding, latin1}]) when is_binary(Text) ->
    form_bytes(numeric_references(Text));
form_encode(Text, [{encoding, Encoding}]) when
    is_list(Text), Encoding =:= utf8 orelse Encoding =:= unicode
->
    form_bytes(to_binary(Text, utf8, Encoding));
form_encode(Text, [{encoding, Encoding}]) when
    is_binary(Text), Encoding =:= utf8 orelse Encoding =:= unicode
->
    form_bytes(Text);
form_encode(Text, [{encoding, Encoding}]) when is_list(Text); is_binary(Text) ->
    throw({error, invalid_encoding, Encoding});
form_encode(Text, _) ->
    throw({error, invalid_input, Text}).

%% Characters above Latin-1 as HTML numeric character references.
numeric_references(Text) -> numeric_references(Text, <<>>).

numeric_references(<<C/utf8, Rest/binary>>, Acc) when C > 255 ->
    numeric_references(Rest, <<Acc/binary, "&#", (integer_to_binary(C))/binary, $;>>);
numeric_references(<<C/utf8, Rest/binary>>, Acc) ->
    numeric_references(Rest, <<Acc/binary, C>>);
numeric_references(<<>>, Acc) ->
    Acc.

%% HTML5's application/x-www-form-urlencoded byte serializer.
form_bytes(Bytes) -> <<<<(form_byte(B))/binary>> || <<B>> <= Bytes>>.

form_byte($\s) -> <<$+>>;
form_byte(B) when B =:= $*; B =:= $-; B =:= $.; B =:= $_; ?IS_DIGIT(B); ?IS_ALPHA(B) -> <<B>>;
form_byte(B) -> percent_encoded(<<B>>).

%% Key-value pairs of a form-urlencoded query; a key without "=" has the value true.
dissect_query(<<>>) ->
    [];
dissect_query([]) ->
    [];
dissect_query(Query) when is_list(Query) ->
    try
        query_key(to_binary(Query, utf8, utf8), true, [], <<>>)
    catch
        throw:{error, _, _} = Error -> Error
    end;
dissect_query(Query) ->
    try
        query_key(Query, false, [], <<>>)
    catch
        throw:{error, _, _} = Error -> Error
    end.

query_key(<<$=, Rest/binary>>, Lists, Pairs, Key) ->
    query_value(Rest, Lists, Pairs, Key, <<>>);
query_key(<<"&#", Rest/binary>>, Lists, Pairs, Key) ->
    query_key(Rest, Lists, Pairs, <<Key/binary, "&#">>);
query_key(<<$&, _/binary>> = Rest, Lists, Pairs, Key) ->
    query_value(Rest, Lists, Pairs, Key, true);
query_key(<<C, Rest/binary>>, Lists, Pairs, Key) ->
    query_key(Rest, Lists, Pairs, <<Key/binary, C>>);
query_key(<<>>, Lists, Pairs, Key) ->
    query_value(<<>>, Lists, Pairs, Key, true).

query_value(<<$&, Rest/binary>>, Lists, Pairs, Key, Value) ->
    K = form_decode(Lists, Key),
    V = form_decode(Lists, Value),
    query_key(Rest, Lists, [{K, V} | Pairs], <<>>);
query_value(<<C, Rest/binary>>, Lists, Pairs, Key, Value) ->
    query_value(Rest, Lists, Pairs, Key, <<Value/binary, C>>);
query_value(<<>>, Lists, Pairs, Key, Value) ->
    K = form_decode(Lists, Key),
    V = form_decode(Lists, Value),
    lists:reverse([{K, V} | Pairs]).

form_decode(_, true) -> true;
form_decode(true, Text) -> to_list(from_references(form_unescape(Text, <<>>), <<>>), utf8);
form_decode(false, Text) -> from_references(form_unescape(Text, <<>>), <<>>).

form_unescape(<<>>, Acc) ->
    Acc;
form_unescape(<<$+, Rest/binary>>, Acc) ->
    form_unescape(Rest, <<Acc/binary, $\s>>);
form_unescape(<<$%, H, L, Rest/binary>> = Text, Acc) ->
    case ?IS_HEX(H) andalso ?IS_HEX(L) of
        true -> form_unescape(Rest, <<Acc/binary, (escaped(H, L))>>);
        false -> throw({error, invalid_percent_encoding, to_list(Text, utf8)})
    end;
form_unescape(<<C/utf8, Rest/binary>>, Acc) ->
    form_unescape(Rest, <<Acc/binary, C/utf8>>);
form_unescape(<<B, _/binary>>, _) ->
    throw({error, invalid_character, [B]}).

%% HTML numeric character references "&#N;" decoded.
from_references(<<>>, Acc) ->
    Acc;
from_references(<<"&#", Rest/binary>>, Acc) ->
    reference_digits(Rest, 0, Acc);
from_references(<<C/utf8, Rest/binary>>, Acc) ->
    from_references(Rest, <<Acc/binary, C/utf8>>);
from_references(<<B, _/binary>>, _) ->
    throw({error, invalid_input, [B]}).

reference_digits(<<D, Rest/binary>>, Code, Acc) when ?IS_DIGIT(D) ->
    reference_digits(Rest, Code * 10 + D - $0, Acc);
reference_digits(<<$;, Rest/binary>>, Code, Acc) ->
    from_references(Rest, <<Acc/binary, Code/utf8>>);
reference_digits(<<B, _/binary>>, _, _) ->
    throw({error, invalid_input, [B]}).

%% Character classes

%% The ASCII characters each URI component allows.
allowed_characters() ->
    Classes = [
        {scheme, fun is_scheme/1},
        {userinfo, fun is_userinfo/1},
        {host, fun is_host/1},
        {ipv4, fun(C) -> C =:= $. orelse ?IS_DIGIT(C) end},
        {ipv6, fun is_ipv6/1},
        {regname, fun is_reg_name/1},
        {path, fun is_path/1},
        {query, fun is_query/1},
        {fragment, fun is_fragment/1},
        {reserved, fun is_reserved/1},
        {unreserved, fun is_unreserved/1}
    ],
    [{Name, lists:filter(Class, lists:seq(0, 127))} || {Name, Class} <- Classes].

is_host(C) -> C =:= $: orelse is_unreserved(C) orelse is_sub_delim(C).

is_path(C) -> C =:= $/ orelse is_pchar(C).

is_scheme(C) -> lists:member(C, "+-.") orelse ?IS_ALPHA(C) orelse ?IS_DIGIT(C).
is_userinfo(C) -> lists:member(C, "%:") orelse is_unreserved(C) orelse is_sub_delim(C).
is_reg_name(C) -> C =:= $% orelse is_unreserved(C) orelse is_sub_delim(C).
is_pchar(C) -> lists:member(C, "%:@") orelse is_unreserved(C) orelse is_sub_delim(C).
is_segment_nz_nc(C) -> lists:member(C, "%@") orelse is_unreserved(C) orelse is_sub_delim(C).
is_query(C) -> lists:member(C, "/?") orelse is_pchar(C).
is_fragment(C) -> lists:member(C, "/?") orelse is_pchar(C).
is_ipv6(C) -> lists:member(C, ":.") orelse ?IS_HEX(C).
is_reserved(C) -> lists:member(C, ":/?#[]@") orelse is_sub_delim(C).
is_sub_delim(C) -> lists:member(C, "!$&'()*+,;=").
is_unreserved(C) -> lists:member(C, "-._~") orelse ?IS_ALPHA(C) orelse ?IS_DIGIT(C).
