-module(zlib_streams).
-export([main/1]).

%% zlib over the pinned zlib 1.3.2 OTP bundles (plan step 41Q): stream
%% states and errors, byte-exact output for every level, strategy and window
%% size, round trips, damaged input, the controlling process and
%% dictionaries. A gzip header names the platform, so gzip output is checked
%% by its round trip and its size.
main([]) ->
    io:format("~p~n", [states()]),
    io:format("~p~n", [outputs()]),
    io:format("~p~n", [round_trips()]),
    io:format("~p~n", [damaged()]),
    io:format("~p~n", [others()]),
    io:format("~p~n", [terms()]),
    io:format("zlib_streams: ok~n").

states() ->
    Z = zlib:open(),
    steps([
        fun() -> zlib:deflate(Z, <<"x">>) end,
        fun() -> zlib:deflateInit(Z) end,
        fun() -> zlib:deflateInit(Z) end,
        fun() -> zlib:deflate(Z, <<"hello">>) end,
        fun() -> zlib:deflate(Z, [<<"a">>, "b"], sync) end,
        fun() -> zlib:deflate(Z, <<>>, finish) end,
        fun() -> zlib:deflate(Z, <<"more">>, finish) end,
        fun() -> zlib:deflateEnd(Z) end,
        fun() -> zlib:deflateEnd(Z) end,
        fun() -> zlib:inflateInit(Z) end,
        fun() -> zlib:inflate(Z, zlib:compress(<<"abc">>)) end,
        fun() -> zlib:inflate(Z, <<"after the end">>) end,
        fun() -> zlib:inflateEnd(Z) end,
        fun() -> zlib:inflateEnd(Z) end,
        fun() -> zlib:inflateInit(Z) end,
        fun() -> zlib:inflate(Z, <<"garbage!!">>) end,
        fun() -> zlib:inflateEnd(Z) end,
        fun() -> zlib:inflateInit(Z, 15, error) end,
        fun() -> zlib:inflate(Z, <<(zlib:compress(<<"e">>))/binary, "x">>) end,
        fun() -> zlib:inflateEnd(Z) end,
        fun() -> zlib:inflateInit(Z, 15, reset) end,
        fun() ->
            iolist_to_binary(
                zlib:inflate(Z, <<
                    (zlib:compress(<<"r1">>))/binary, (zlib:compress(<<"r2">>))/binary
                >>)
            )
        end,
        fun() -> zlib:inflateReset(Z) end,
        fun() -> zlib:inflate(Z, <<120, 156>>) end,
        fun() -> zlib:inflateEnd(Z) end,
        fun() -> zlib:close(Z) end,
        fun() -> zlib:close(Z) end,
        fun() -> zlib:inflateInit(Z) end,
        fun() -> zlib:deflateInit(zlib:open(), 11) end,
        fun() -> zlib:deflateInit(zlib:open(), default, deflated, 7, 8, default) end,
        fun() -> zlib:deflateInit(zlib:open(), default, deflated, 15, 10, default) end,
        fun() -> zlib:deflateInit(zlib:open(), default, deflated, 15, 8, x) end,
        fun() -> zlib:deflateInit(zlib:open(), default, stored, 15, 8, default) end,
        fun() -> zlib:inflateInit(zlib:open(), 15, never) end,
        fun() ->
            Z1 = zlib:open(),
            ok = zlib:deflateInit(Z1),
            zlib:deflate(Z1, <<"x">>, bogus)
        end
    ]).

%% The value or error of every step, in order.
steps(Steps) -> [outcome(Step) || Step <- Steps].

%% Byte-exact compressed output for every level, strategy and window size.
outputs() ->
    Data = iolist_to_binary([integer_to_list(N) || N <- lists:seq(1, 300)]),
    [
        zlib:compress(<<"hello hello hello">>),
        zlib:compress([<<"ab">>, $c]),
        zlib:zip(<<"ab">>),
        zlib:compress(<<>>),
        [
            deflated(Data, Level, default, 15)
         || Level <- [none, best_speed, default, best_compression, 0, 5, 9]
        ],
        [
            deflated(Data, default, Strategy, 15)
         || Strategy <- [default, filtered, huffman_only, rle]
        ],
        [deflated(Data, default, default, Bits) || Bits <- [9, 12, -9, -15]],
        byte_size(zlib:gzip(Data)),
        streamed(Data)
    ].

deflated(Data, Level, Strategy, Bits) ->
    Z = zlib:open(),
    ok = zlib:deflateInit(Z, Level, deflated, Bits, 8, Strategy),
    Output = iolist_to_binary(zlib:deflate(Z, Data, finish)),
    ok = zlib:deflateEnd(Z),
    ok = zlib:close(Z),
    erlang:md5(Output).

%% Output of a stream fed in pieces with sync and full flushes and a parameter change in between.
streamed(Data) ->
    Z = zlib:open(),
    ok = zlib:deflateInit(Z, best_speed),
    A = zlib:deflate(Z, binary:part(Data, 0, 100), sync),
    ok = zlib:deflateParams(Z, best_compression, filtered),
    B = zlib:deflate(Z, binary:part(Data, 100, 200), full),
    C = zlib:deflate(Z, binary:part(Data, 300, byte_size(Data) - 300), finish),
    ok = zlib:deflateEnd(Z),
    Out = iolist_to_binary([A, B, C]),
    {erlang:md5(Out), zlib:uncompress(Out) =:= Data}.

round_trips() ->
    Data = binary:copy(<<"round trip ">>, 5000),
    [
        zlib:uncompress(zlib:compress(Data)) =:= Data,
        zlib:unzip(zlib:zip(Data)) =:= Data,
        zlib:gunzip(zlib:gzip(Data)) =:= Data,
        zlib:gunzip(<<(zlib:gzip(<<"q">>))/binary, (zlib:gzip(<<"r">>))/binary>>),
        zlib:uncompress(<<(zlib:compress(<<"q">>))/binary, "x">>),
        safe(zlib:compress(<<"safe">>)),
        safe(<<120, 156>>),
        iolist_to_binary(inflated_in_pieces(zlib:compress(Data), 7)) =:= Data
    ].

safe(Data) ->
    Z = zlib:open(),
    ok = zlib:inflateInit(Z),
    outcome(fun() -> zlib:safeInflate(Z, Data) end).

inflated_in_pieces(Compressed, Size) ->
    Z = zlib:open(),
    ok = zlib:inflateInit(Z),
    Output = [zlib:inflate(Z, Piece) || Piece <- pieces(Compressed, Size)],
    ok = zlib:inflateEnd(Z),
    Output.

pieces(Binary, Size) when byte_size(Binary) =< Size -> [Binary];
pieces(Binary, Size) ->
    [
        binary:part(Binary, 0, Size)
        | pieces(binary:part(Binary, Size, byte_size(Binary) - Size), Size)
    ].

damaged() ->
    Good = zlib:compress(<<"some data to damage">>),
    Flipped = <<
        (binary:part(Good, 0, 5))/binary, 255, (binary:part(Good, 6, byte_size(Good) - 6))/binary
    >>,
    steps([
        fun() -> zlib:uncompress(<<"bad">>) end,
        fun() -> zlib:uncompress(<<"not compressed at all">>) end,
        fun() -> zlib:uncompress(Flipped) end,
        fun() -> zlib:uncompress(binary:part(Good, 0, byte_size(Good) - 3)) end,
        fun() -> zlib:gunzip(<<"bad">>) end,
        fun() -> zlib:unzip(<<"bad">>) end,
        fun() -> zlib:compress(x) end,
        fun() -> zlib:uncompress(x) end
    ]).

others() ->
    Self = self(),
    Z = zlib:open(),
    spawn(fun() -> Self ! {other, outcome(fun() -> zlib:deflateInit(Z) end)} end),
    Other =
        receive
            {other, Answer} -> Answer
        end,
    Z2 = zlib:open(),
    Pid = spawn(fun() ->
        receive
            go -> Self ! {moved, outcome(fun() -> zlib:deflateInit(Z2) end)}
        end
    end),
    ok = zlib:set_controlling_process(Z2, Pid),
    Pid ! go,
    Moved =
        receive
            {moved, Result} -> Result
        end,
    [Other, Moved, dictionary()].

%% A dictionary for deflate and the inflate that needs it.
dictionary() ->
    Dictionary = <<"common words common words">>,
    D = zlib:open(),
    ok = zlib:deflateInit(D),
    Adler = zlib:deflateSetDictionary(D, Dictionary),
    Compressed = iolist_to_binary(zlib:deflate(D, <<"common words again">>, finish)),
    ok = zlib:deflateEnd(D),
    I = zlib:open(),
    ok = zlib:inflateInit(I),
    First = outcome(fun() -> zlib:inflate(I, Compressed, [{exception_on_need_dict, false}]) end),
    ok = zlib:inflateSetDictionary(I, Dictionary),
    Rest = zlib:inflate(I, <<>>),
    Window = zlib:inflateGetDictionary(I),
    I2 = zlib:open(),
    ok = zlib:inflateInit(I2),
    [Adler, First, Rest, Window, outcome(fun() -> zlib:inflate(I2, Compressed) end)].

%% Compressed external terms: OTP's bytes for every level, plain encodings when compression does not pay.
terms() ->
    Term = {lists:duplicate(200, compressible), binary:copy(<<"abc">>, 100)},
    Compressed = term_to_binary(Term, [compressed]),
    Corrupt = <<
        (binary:part(Compressed, 0, 10))/binary, 0, 0, 0, (binary:part(Compressed, 13, 20))/binary
    >>,
    [
        erlang:md5(Compressed),
        byte_size(Compressed) < byte_size(term_to_binary(Term)),
        [erlang:md5(term_to_binary(Term, [{compressed, Level}])) || Level <- [0, 1, 9]],
        term_to_binary(abc, [compressed]),
        term_to_binary(lists:seq(1, 100), [compressed]) =:= term_to_binary(lists:seq(1, 100)),
        binary_to_term(Compressed) =:= Term,
        binary_to_term(<<Compressed/binary, "tail">>, [used]) =:= {Term, byte_size(Compressed)},
        erlang:external_size(Term, [compressed]) =:= byte_size(term_to_binary(Term)),
        outcome(fun() -> binary_to_term(Corrupt) end),
        outcome(fun() -> binary_to_term(<<131, 80, 0, 0, 0, 9>>) end),
        outcome(fun() -> binary_to_term(<<131, 80, 255, 255, 255, 255, 1, 2>>) end)
    ].

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
