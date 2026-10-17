-module(json_codec).
-export([main/1]).

%% json (plan 21 step LIB5): decoding with OTP's error terms, the callback decoders and their call order, streamed
%% decoding byte by byte, encoding with the iodata shapes OTP builds, the leaf encoders, custom encoders and
%% formatting; maps print in key order.
main(_) ->
    Texts = [
        <<"0">>,
        <<"-0">>,
        <<"7">>,
        <<"-12">>,
        <<"1.5">>,
        <<"-0.25">>,
        <<"1e5">>,
        <<"1E+2">>,
        <<"2e-2">>,
        <<"0e0">>,
        <<"0.5E3">>,
        <<"-1.25e-3">>,
        <<"123456789012345678901234567890">>,
        <<"true">>,
        <<"false">>,
        <<"null">>,
        <<"\"\"">>,
        <<"\"abc\"">>,
        <<"\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"">>,
        <<"\"\\u0041\\u00e4\\u20AC\"">>,
        <<"\"\\ud83d\\ude00\"">>,
        <<"\"x", 16#C3, 16#A4, "y\"">>,
        <<"\"", 16#F0, 16#9F, 16#98, 16#80, "\"">>,
        <<"[]">>,
        <<"[ ]">>,
        <<"{}">>,
        <<"{ }">>,
        <<"[1,[2,{\"a\":null}],\"s\"]">>,
        <<" {\"a\" : 1 , \"b\":[true, false]}\r\n\t">>,
        <<"{\"a\":1,\"a\":2}">>,
        <<"[[[[]]]]">>,
        <<"1 ">>,
        <<"">>,
        <<"  ">>,
        <<"t">>,
        <<"tru">>,
        <<"trux">>,
        <<"truxy">>,
        <<"fals">>,
        <<"falsy">>,
        <<"nul">>,
        <<"nulx">>,
        <<"nullx">>,
        <<"[">>,
        <<"[1">>,
        <<"[1,">>,
        <<"[1 2]">>,
        <<"[1,]">>,
        <<"[,1]">>,
        <<"]">>,
        <<"{">>,
        <<"{\"a\"">>,
        <<"{\"a\":">>,
        <<"{\"a\":1">>,
        <<"{\"a\":1,">>,
        <<"{\"a\" 1}">>,
        <<"{1:2}">>,
        <<"{\"a\":1,}">>,
        <<"{\"a\":1 \"b\":2}">>,
        <<"\"abc">>,
        <<"\"a\\">>,
        <<"\"a\\x\"">>,
        <<"\"\\u12\"">>,
        <<"\"\\u12">>,
        <<"\"\\uzzzz\"">>,
        <<"\"\\ud800\"">>,
        <<"\"\\ud800\\u0041\"">>,
        <<"\"\\ud800\\u">>,
        <<"\"\\udc00\"">>,
        <<"\"\\ud800">>,
        <<"\"\\u00e4">>,
        <<"\"a\nb\"">>,
        <<"\"a", 0, "\"">>,
        <<"\"", 255, "\"">>,
        <<"\"", 16#C3>>,
        <<"\"", 16#C3, "\"">>,
        <<"\"", 16#E0, 16#80, 16#80, "\"">>,
        <<"\"", 16#ED, 16#A0, 16#80, "\"">>,
        <<"\"", 16#F4, 16#90, 16#80, 16#80, "\"">>,
        <<"\"", 16#E2, 16#82>>,
        <<"-">>,
        <<"-a">>,
        <<"--1">>,
        <<"01">>,
        <<"0x">>,
        <<"1.">>,
        <<"1.a">>,
        <<"1.5.">>,
        <<"1e">>,
        <<"1e+">>,
        <<"1ex">>,
        <<"1.0e">>,
        <<"1.0e-">>,
        <<"1.0e-x">>,
        <<"1.5e999">>,
        <<"1e999">>,
        <<"[1e999]">>,
        <<"1e-999">>,
        <<"+1">>,
        <<".5">>,
        <<"1 2">>,
        <<"[1]x">>,
        <<"{}}">>,
        <<"x">>,
        <<255>>,
        <<16#C3, 16#A4>>
    ],
    show(decode, [reason(fun() -> json:decode(T) end) || T <- Texts]),
    show(decode_crash, [reason(fun() -> json:decode(id("[]")) end)]),
    show(decode3, [reason(fun() -> json:decode(T, acc, #{}) end) || T <- Texts]),
    show(decode3_trace, [traced(T, tracing()) || T <- trace_texts()]),
    show(decode3_partial, [
        traced(T, D)
     || D <- partial_decoders(), T <- [<<"[1,2]">>, <<"{\"a\":1}">>, <<"[]">>, <<"{}">>]
    ]),
    show(decode3_float, [
        reason(fun() -> json:decode(T, acc, #{float => fun(_) -> error(bad) end}) end)
     || T <- [<<"1.5">>, <<"[1.5]">>, <<"1e2 ">>, <<"7">>]
    ]),
    show(decode3_bad, [
        reason(fun() -> json:decode(<<"1">>, acc, id(#{foo => 1})) end),
        reason(fun() -> json:decode(<<"1">>, acc, id(#{float => 1})) end),
        reason(fun() -> json:decode(id("1"), acc, #{}) end),
        reason(fun() -> json:decode(<<"null">>, acc, #{null => nil}) end),
        reason(fun() -> json:decode(<<"[1]">>, acc, #{integer => fun(_) -> throw(stop) end}) end)
    ]),
    show(stream, [streamed(T) || T <- Texts]),
    show(stream_chunks, [
        chunked([<<"{\"foo\":">>, <<"1}">>]),
        chunked([<<"123">>, <<"4">>]),
        chunked([<<"1.">>, <<"5">>]),
        chunked([<<"tr">>, <<"ue">>, <<" x">>]),
        chunked([<<"[1,">>, <<"2] rest">>]),
        chunked([<<"\"\\ud83d">>, <<"\\ude00\"">>]),
        chunked([<<"1.5e999">>]),
        chunked([<<"[1">>])
    ]),
    show(stream_bad, [
        reason(fun() -> json:decode_start(id("1"), acc, #{}) end),
        reason(fun() ->
            {continue, State} = json:decode_start(<<"[">>, acc, #{}),
            json:decode_continue(id(x), State)
        end)
    ]),
    show(encode, [encoded(V) || V <- encode_values()]),
    show(encode_shape, [
        reason(fun() -> json:encode(V) end)
     || V <- [
            1,
            <<"abc">>,
            <<"a\nb">>,
            <<"\"">>,
            <<"ab\"cd\"">>,
            [],
            [1],
            [1, 2, 3],
            #{},
            #{<<"a">> => 1},
            #{1 => 2, 3 => 4},
            foo,
            null,
            [[]]
        ]
    ]),
    show(encode_errors, [
        reason(fun() -> json:encode(V) end)
     || V <- [
            {a},
            [1, {a}, {b}],
            [1 | 2],
            #{{k} => 1},
            #{a => {x}},
            <<255>>,
            <<"a", 16#C3>>,
            <<16#E0, 16#80, 16#80>>,
            <<"ok", 16#ED, 16#A0, 16#80>>,
            <<1:7>>
        ]
    ]),
    show(leaves, [
        json:encode_integer(-42),
        json:encode_integer(123456789012345678901234567890),
        [
            json:encode_float(F)
         || F <- [0.1, 1.0, -0.0, 1.0e300, 1.5e-7, 123456789.0, 2.5e15, 1.0e16]
        ],
        json:encode_binary(<<"plain">>),
        json:encode_binary(<<"tab\there", 16#C3, 16#A4>>),
        reason(fun() -> json:encode_binary(id("list")) end),
        reason(fun() -> json:encode_integer(id(1.0)) end),
        reason(fun() -> json:encode_float(id(1)) end),
        json:encode_atom(null, fun no_call/2),
        json:encode_atom(other, fun(V, _) -> {called, V} end),
        json:encode_value(<<"v">>, fun no_call/2),
        json:encode_value([1, 2], fun(V, _) -> {e, V} end),
        json:encode_list([a, b], fun(V, _) -> {e, V} end),
        reason(fun() -> json:encode_list(id(x), fun no_call/2) end),
        json:encode_map(#{1 => a}, fun(V, _) -> {e, V} end),
        reason(fun() -> json:encode_map(id([]), fun no_call/2) end)
    ]),
    show(escape_all, [
        reason(fun() -> json:encode_binary_escape_all(B) end)
     || B <- [
            <<"plain">>,
            <<"a", 16#C3, 16#A4, "b">>,
            <<16#C3, 16#A4>>,
            <<16#C4, 16#80>>,
            <<16#DF, 16#BF>>,
            <<16#E0, 16#A0, 16#80>>,
            <<16#E0, 16#BF, 16#BF>>,
            <<16#E1, 16#80, 16#80>>,
            <<16#E2, 16#82, 16#AC, "x">>,
            <<16#EF, 16#BF, 16#BF>>,
            <<16#F0, 16#9F, 16#98, 16#80>>,
            <<"q\"\n", 16#C3, 16#A4, "\\">>,
            <<255>>,
            <<"ab", 16#C3>>,
            <<16#ED, 16#A0, 16#80>>
        ]
    ]),
    show(custom, [
        iolist_to_binary(json:encode([{a, []}, {b, 1}], fun kv_encoder/2)),
        iolist_to_binary(json:encode(#{<<"t">> => {1, 2}}, fun tuple_encoder/2)),
        iolist_to_binary(
            json:encode_key_value_list(
                [{a, 1}, x, {<<"b">>, 2}, {3, 3}, {4.5, 4}], fun json:encode_value/2
            )
        ),
        json:encode_key_value_list([{a, 1}], fun json:encode_value/2),
        json:encode_key_value_list([], fun json:encode_value/2),
        reason(fun() -> json:encode_key_value_list([{"k", 1}], fun json:encode_value/2) end),
        iolist_to_binary(
            json:encode_key_value_list_checked([{a, 1}, {b, 2}], fun json:encode_value/2)
        ),
        json:encode_key_value_list_checked([{a, 1}], fun json:encode_value/2),
        reason(fun() ->
            json:encode_key_value_list_checked([{a, 1}, {<<"a">>, 2}], fun json:encode_value/2)
        end),
        reason(fun() ->
            json:encode_key_value_list_checked([{1, 1}, {<<"1">>, 2}], fun json:encode_value/2)
        end),
        reason(fun() -> json:encode_key_value_list_checked([x], fun json:encode_value/2) end),
        iolist_to_binary(
            json:encode_map_checked(#{<<"a">> => 1, <<"b">> => 2}, fun json:encode_value/2)
        ),
        reason(fun() ->
            json:encode_map_checked(#{a => 1, <<"a">> => 2}, fun json:encode_value/2)
        end),
        reason(fun() -> json:encode(1, id(fun(_) -> x end)) end)
    ]),
    format_cases(),
    io:format("json_codec: ok~n").

%% Formatting: pretty JSON through io:put_chars, and a few iodata shapes.
format_cases() ->
    Doc = #{
        name => <<"clause">>,
        tags => [<<"a">>, <<"b">>],
        nums => lists:seq(1, 5),
        nested => #{inner => [#{x => 1}, []], empty => #{}},
        one => #{only => 1},
        deep => #{only => [1]},
        flags => [true, false, null]
    },
    Deep = nest(300, #{<<"leaf">> => [1, <<"x">>]}),
    show(deep, [
        digest(json:encode(Deep)),
        digest(json:format(Deep)),
        digest(json:format(Deep, #{indent => 1}))
    ]),
    Wide = #{
        <<"rows">> => [lists:seq(1, 30), [<<"a">>, 2], #{k => lists:seq(1, 12)}],
        <<"n">> => lists:seq(1, 25)
    },
    io:put_chars(json:format(Wide, #{indent => 3, max => 20})),
    io:put_chars(json:format(Wide, #{max => 0, level => 1, col => 7})),
    show(format_options, [
        reason(fun() -> json:format([1, [2]], #{indent => 1.5}) end),
        reason(fun() -> json:format([1, [2]], #{max => x}) end),
        reason(fun() -> json:format(#{a => [1]}, #{indent => -2}) end)
    ]),
    io:put_chars(json:format(Doc)),
    io:put_chars(json:format(Doc, #{indent => 4})),
    io:put_chars(json:format(lists:seq(1, 40), #{max => 20})),
    io:put_chars(json:format([[1, 2], [3]], #{indent => 3})),
    io:put_chars(
        json:format(#{a => #{b => #{c => #{d => #{e => #{f => #{g => [1]}}}}}}}, #{indent => 3})
    ),
    io:put_chars(json:format(#{a => 1}, #{indent => 0})),
    io:put_chars(json:format([], #{})),
    io:put_chars(json:format(#{})),
    io:put_chars(json:format(<<"s">>)),
    io:put_chars(json:format(#{k => {posix, 0}}, fun formatter/3)),
    io:put_chars(json:format([1, {posix, 1}], fun formatter/3, #{indent => 1})),
    show(format, [
        json:format(1),
        json:format([1, 2]),
        json:format(#{a => 1}),
        json:format(#{a => [1]}),
        json:format(#{a => 1, b => 2}),
        json:format([<<"x">>]),
        json:format_value(1, fun no_call/3, #{}),
        json:format_value(<<"b">>, fun no_call/3, #{}),
        json:format_value(foo, fun(V, _, S) -> {f, V, maps:get(level, S)} end, #{level => 0}),
        json:format_key_value_list([{a, 1}], fun json:format_value/3, #{
            level => 0, indent => 2, col => 0, max => 100
        }),
        json:format_key_value_list([], fun json:format_value/3, #{level => 0, indent => 2}),
        json:format_key_value_list_checked([{a, 1}, {b, [2]}], fun json:format_value/3, #{
            level => 1, indent => 2, col => 0, max => 100
        }),
        json:format_key_value_list_checked([], fun json:format_value/3, #{level => 0, indent => 2}),
        reason(fun() ->
            json:format_key_value_list_checked([{a, 1}, {a, 2}], fun json:format_value/3, #{
                level => 0, indent => 2, col => 0, max => 100
            })
        end),
        reason(fun() -> json:format({a}) end),
        reason(fun() -> json:format(#{{k} => 1}) end),
        reason(fun() -> json:format(1, id(x)) end),
        reason(fun() -> json:format(#{a => x}, fun(_, _, _) -> [] end) end)
    ]).

nest(0, Leaf) -> Leaf;
nest(N, Leaf) when N rem 2 =:= 0 -> [nest(N - 1, Leaf)];
nest(N, Leaf) -> #{N => nest(N - 1, Leaf)}.

digest(Iodata) ->
    {
        iolist_size(Iodata),
        binary:encode_hex(erlang:md5(Iodata)),
        erlang:md5(term_to_binary(Iodata))
    }.

formatter({posix, Seconds}, Encode, State) ->
    json:format_value(integer_to_binary(Seconds * 1000), Encode, State);
formatter(Other, Encode, State) ->
    json:format_value(Other, Encode, State).

kv_encoder([{_, _} | _] = Value, Encode) -> json:encode_key_value_list(Value, Encode);
kv_encoder(Other, Encode) -> json:encode_value(Other, Encode).

tuple_encoder(Tuple, Encode) when is_tuple(Tuple) -> json:encode_list(tuple_to_list(Tuple), Encode);
tuple_encoder(Other, Encode) -> json:encode_value(Other, Encode).

no_call(_, _) -> error(called).
no_call(_, _, _) -> error(called).

encode_values() ->
    [
        0,
        -7,
        123456789012345678901234567890,
        1.5,
        -0.0,
        1.0e-10,
        true,
        false,
        null,
        foo,
        'with space',
        'quote"atom',
        <<>>,
        <<"plain text">>,
        <<0, 1, 7, 8, 9, 10, 11, 12, 13, 14, 31, 32, 127>>,
        <<"\"\\/">>,
        <<"x", 16#C3, 16#A4, 16#E2, 16#82, 16#AC, 16#F0, 16#9F, 16#98, 16#80>>,
        [],
        [1, [2, [3, []]]],
        [true, null, <<"s">>, 2.5],
        #{},
        #{<<"k">> => <<"v">>},
        #{1 => one, 2 => two},
        #{2.5 => x},
        #{a => #{b => [#{}]}},
        #{'a b' => 1},
        [#{1 => []}, #{}]
    ].

encoded(Value) ->
    case reason(fun() -> json:encode(Value) end) of
        {error, _} = Error -> Error;
        Iodata -> iolist_to_binary(Iodata)
    end.

%% Decoders that log each call in the process dictionary and return markers.
tracing() ->
    #{
        array_start => fun(Acc) ->
            note({array_start, Acc}),
            {array, Acc}
        end,
        array_push => fun(Value, Acc) ->
            note({array_push, Value, Acc}),
            [Value | Acc]
        end,
        array_finish => fun(Acc, Old) ->
            note({array_finish, Acc, Old}),
            {{list, Acc}, {after_array, Old}}
        end,
        object_start => fun(Acc) ->
            note({object_start, Acc}),
            {object, Acc}
        end,
        object_push => fun(Key, Value, Acc) ->
            note({object_push, Key, Value}),
            [{Key, Value} | Acc]
        end,
        object_finish => fun(Acc, Old) ->
            note({object_finish, Acc, Old}),
            {{obj, Acc}, {after_object, Old}}
        end,
        float => fun(Text) ->
            note({float, Text}),
            {f, Text}
        end,
        integer => fun(Text) ->
            note({integer, Text}),
            {i, Text}
        end,
        string => fun(Text) ->
            note({string, Text}),
            {s, Text}
        end,
        null => nil
    }.

trace_texts() ->
    [
        <<"[1, 2.5, \"s\", null, true]">>,
        <<"{\"a\": {\"b\": []}, \"c\": [{}]}">>,
        <<"[]">>,
        <<"{}">>,
        <<"[[1]] tail">>,
        <<"\"\\u0041\"">>,
        <<"1e2">>,
        <<"[1, x]">>,
        <<"{\"k\": 1, 2}">>
    ].

%% Only some of the callbacks given: the defaults fill in around them.
partial_decoders() ->
    [
        #{array_start => fun(_) -> [start] end},
        #{array_finish => fun(Acc, Old) -> {{done, Acc}, Old} end},
        #{object_start => fun(_) -> [{<<"z">>, 0}] end},
        #{object_finish => fun(Acc, Old) -> {{done, Acc}, Old} end},
        #{
            array_push => fun(V, Acc) -> [V, V | Acc] end,
            object_push => fun(K, V, Acc) -> [{K, V}, {K, V} | Acc] end
        }
    ].

traced(Text, Decoders) ->
    put(log, []),
    Result = reason(fun() -> json:decode(Text, acc, Decoders) end),
    {Result, lists:reverse(get(log))}.

note(Event) -> put(log, [Event | get(log)]).

%% Decoding fed one byte at a time, then end_of_input.
streamed(Text) ->
    reason(fun() -> feed(json:decode_start(<<>>, acc, #{}), binary_to_list(Text)) end).

feed({continue, State}, [Byte | Rest]) -> feed(json:decode_continue(<<Byte>>, State), Rest);
feed({continue, State}, []) -> json:decode_continue(end_of_input, State);
feed(Done, Rest) -> {Done, list_to_binary(Rest)}.

chunked([First | Rest]) ->
    reason(fun() -> feed_chunks(json:decode_start(First, acc, #{}), Rest) end).

feed_chunks({continue, State}, [Chunk | Rest]) ->
    feed_chunks(json:decode_continue(Chunk, State), Rest);
feed_chunks({continue, State}, []) ->
    json:decode_continue(end_of_input, State);
feed_chunks(Done, _) ->
    Done.

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~kw~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> Value
    catch
        error:Reason -> {error, Reason};
        throw:Thrown -> {thrown, Thrown}
    end.
