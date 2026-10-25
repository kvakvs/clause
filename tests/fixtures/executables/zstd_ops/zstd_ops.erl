-module(zstd_ops).
-export([main/1]).

%% zstd (plan 21 step LIB54E): one-shot compression at levels and with
%% parameters (compared by digests of the frames), iodata, streaming with
%% stream/2, flush/1 and finish/2 and the chunks they return, large data,
%% raw content dictionaries loaded and referenced, parameters read back,
%% frame headers and dictionary ids, and errors.
main([]) ->
    one_shot(),
    parameters(),
    streaming(),
    large(),
    dictionaries(),
    headers(),
    files(),
    errors(),
    io:format("zstd_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, clean(Value)]).

%% References by shape.
clean(Ref) when is_reference(Ref) -> ref;
clean(List) when is_list(List) -> [clean(E) || E <- List];
clean(Tuple) when is_tuple(Tuple) -> list_to_tuple(clean(tuple_to_list(Tuple)));
clean(Other) -> Other.

digest(IoData) ->
    Bin = iolist_to_binary(IoData),
    {byte_size(Bin), binary:encode_hex(erlang:md5(Bin))}.

text(N) -> iolist_to_binary([integer_to_list(I rem 97) || I <- lists:seq(1, N)]).

noise(N) -> noise(N, 12345, []).

noise(0, _, Acc) ->
    list_to_binary(Acc);
noise(N, Seed, Acc) ->
    Next = (Seed * 1103515245 + 12345) rem 2147483648,
    noise(N - 1, Next, [(Next bsr 16) band 255 | Acc]).

one_shot() ->
    Text = text(5000),
    Frame = iolist_to_binary(zstd:compress(Text)),
    show(compress, [digest(Frame), iolist_to_binary(zstd:decompress(Frame)) =:= Text]),
    show(levels, [digest(zstd:compress(Text, #{compressionLevel => L})) || L <- [-5, 1, 3, 9, 19]]),
    show(empty, [
        digest(zstd:compress(<<>>)), zstd:decompress(zstd:compress(<<>>)), digest(zstd:compress([]))
    ]),
    show(iodata, [
        digest(zstd:compress([<<"ab">>, "cd", [$e, <<"f">>]])),
        iolist_to_binary(
            zstd:decompress([
                binary:part(Frame, 0, 10), binary:part(Frame, 10, byte_size(Frame) - 10)
            ])
        ) =:=
            Text
    ]),
    show(noise, digest(zstd:compress(noise(3000)))),
    Two = [zstd:compress(<<"one">>), zstd:compress(<<"two">>)],
    show(two_frames, zstd:decompress(Two)).

parameters() ->
    Text = text(3000),
    show(flags, [
        digest(zstd:compress(Text, #{checksumFlag => true})),
        digest(zstd:compress(Text, #{contentSizeFlag => false})),
        digest(
            zstd:compress(Text, #{windowLog => 12, strategy => btultra2, compressionLevel => 5})
        ),
        digest(zstd:compress(Text, #{enableLongDistanceMatching => true}))
    ]),
    {ok, C} = zstd:context(compress, #{
        compressionLevel => 7, strategy => lazy, checksumFlag => true
    }),
    show(read_back, [
        zstd:get_parameter(C, compressionLevel),
        zstd:get_parameter(C, strategy),
        zstd:get_parameter(C, checksumFlag),
        zstd:get_parameter(C, windowLog),
        zstd:set_parameter(C, strategy, default),
        zstd:get_parameter(C, strategy)
    ]),
    {ok, D} = zstd:context(decompress, #{windowLogMax => 20}),
    show(decompress_read_back, zstd:get_parameter(D, windowLogMax)),
    show(closes, [zstd:close(C), zstd:close(D)]).

streaming() ->
    {ok, C} = zstd:context(compress),
    S1 = zstd:stream(C, <<"hello ">>),
    S2 = zstd:stream(C, [<<"stream">>, "ing", <<" world">>]),
    F = zstd:flush(C),
    {done, Done} = zstd:finish(C, <<"!">>),
    show(stream_shapes, [shape(S1), shape(S2), shape(F), [byte_size(B) || B <- Done]]),
    Frame = iolist_to_binary([out(S1), out(S2), out(F), Done]),
    show(stream_frame, [digest(Frame), iolist_to_binary(zstd:decompress(Frame))]),
    {ok, D} = zstd:context(decompress),
    <<A:10/binary, B/binary>> = Frame,
    D1 = zstd:stream(D, A),
    D2 = zstd:stream(D, B),
    show(decompress_stream, [D1, D2]),
    show(decompress_finish, zstd:finish(D, zstd:compress(<<"again">>))),
    show(reset, [zstd:reset(C), digest(iolist_to_binary(zstd:compress(<<"after reset">>, C)))]),
    show(reused, digest(zstd:compress(<<"after reset">>, C))),
    zstd:close(C),
    zstd:close(D).

shape({continue, Out}) -> {continue, byte_size(Out)};
shape({continue, Rest, Out}) -> {continue, iolist_size(Rest), byte_size(Out)}.

out({continue, Out}) -> Out;
out({continue, _, Out}) -> Out.

large() ->
    Big = text(200000),
    Frame = iolist_to_binary(zstd:compress(Big)),
    show(large, [digest(Frame), byte_size(Big)]),
    show(large_chunks, [byte_size(B) || B <- zstd:decompress(Frame)]),
    Unsized = iolist_to_binary(zstd:compress(Big, #{contentSizeFlag => false})),
    show(unsized_chunks, [byte_size(B) || B <- zstd:decompress(Unsized)]),
    {ok, D} = zstd:context(decompress),
    {continue, Rest, Out} = zstd:stream(D, Frame),
    show(large_stream, [byte_size(Rest), byte_size(Out)]),
    zstd:close(D),
    Noisy = noise(300000),
    show(noisy, [byte_size(B) || B <- zstd:compress(Noisy)]),
    {ok, C} = zstd:context(compress),
    show(noisy_stream, shape(zstd:stream(C, Noisy))),
    zstd:close(C).

dictionaries() ->
    Dict = text(2000),
    Data = <<(binary:part(Dict, 100, 500))/binary, "tail">>,
    Plain = digest(zstd:compress(Data)),
    WithDict = zstd:compress(Data, #{dictionary => Dict}),
    show(raw_dictionary, [
        Plain,
        digest(WithDict),
        iolist_to_binary(zstd:decompress(WithDict, #{dictionary => Dict})) =:= Data
    ]),
    {ok, CDict} = zstd:dict(compress, Dict),
    {ok, CDict9} = zstd:dict(compress, Dict, #{compressionLevel => 9}),
    {ok, DDict} = zstd:dict(decompress, Dict),
    Referenced = zstd:compress(Data, #{dictionary => CDict}),
    show(dict_handles, [
        digest(Referenced),
        digest(zstd:compress(Data, #{dictionary => CDict9})),
        iolist_to_binary(zstd:decompress(Referenced, #{dictionary => DDict})) =:= Data,
        zstd:get_dict_id(CDict),
        zstd:get_dict_id(DDict),
        zstd:get_dict_id(Dict),
        zstd:get_dict_id(iolist_to_binary(Referenced))
    ]),
    show(missing_dictionary, outcome(fun() -> zstd:decompress(WithDict) end)).

headers() ->
    Frame = iolist_to_binary(zstd:compress(text(1000), #{checksumFlag => true})),
    show(header, sorted(zstd:get_frame_header(Frame))),
    show(
        unsized_header,
        sorted(zstd:get_frame_header(zstd:compress(text(10), #{contentSizeFlag => false})))
    ),
    Skippable = <<16#184D2A50:32/little, 4:32/little, "skip">>,
    show(skippable, sorted(zstd:get_frame_header(Skippable))),
    show(not_a_frame, zstd:get_frame_header(<<"not a zstd frame">>)).

% file:open/2 with {zstd, Options}: each write is a frame of its own, closing adds an empty one.
files() ->
    File = "zstd_ops.zst",
    {ok, W} = file:open(File, [write, {zstd, #{compressionLevel => 5}}]),
    show(file_write, [
        file:write(W, <<"first line\n">>), file:write(W, ["second", <<" line\n">>]), file:close(W)
    ]),
    {ok, Bytes} = file:read_file(File),
    show(file_bytes, [digest(Bytes), iolist_to_binary(zstd:decompress(Bytes))]),
    {ok, R} = file:open(File, [read, binary, {zstd, #{}}]),
    show(file_read, [
        file:read(R, 3),
        file:read_line(R),
        file:position(R, cur),
        file:position(R, 2),
        file:read(R, 100),
        file:read(R, 1),
        file:close(R)
    ]),
    {ok, Raw} = file:open(File, [read, raw, {zstd, #{}}]),
    show(file_raw_read, [file:read(Raw, 6), file:close(Raw)]),
    show(file_errors, [
        file:open(File, [read, write, {zstd, #{}}]),
        file:open(File, [write, {zstd, #{bogus => 1}}]),
        file:open(File, [append, {zstd, #{}}])
    ]),
    ok = file:delete(File).

sorted({ok, Map}) -> {ok, lists:sort(maps:to_list(Map))};
sorted(Other) -> Other.

errors() ->
    {ok, C} = zstd:context(compress),
    {ok, D} = zstd:context(decompress),
    show(bad_parameters, [
        outcome(fun() -> zstd:set_parameter(C, bogus, 1) end),
        outcome(fun() -> zstd:set_parameter(C, strategy, slow) end),
        outcome(fun() -> zstd:set_parameter(C, checksumFlag, 1) end),
        outcome(fun() -> zstd:set_parameter(C, compressionLevel, high) end),
        outcome(fun() -> zstd:set_parameter(C, windowLog, 99) end),
        outcome(fun() -> zstd:get_parameter(C, bogus) end),
        outcome(fun() -> zstd:set_parameter(D, windowLogMax, x) end),
        outcome(fun() -> zstd:set_parameter(D, other, 1) end),
        outcome(fun() -> zstd:get_parameter(D, other) end),
        outcome(fun() -> zstd:set_parameter(not_a_context, k, v) end),
        outcome(fun() -> zstd:get_parameter(not_a_context, k) end),
        outcome(fun() -> zstd:context(compress, #{bogus => 1}) end)
    ]),
    show(bad_data, [
        outcome(fun() -> zstd:decompress(<<"garbage data here">>) end),
        outcome(fun() -> zstd:stream(D, [foo]) end),
        outcome(fun() -> zstd:flush(D) end),
        outcome(fun() ->
            zstd:decompress(binary:part(iolist_to_binary(zstd:compress(text(100))), 0, 8))
        end)
    ]),
    Self = self(),
    spawn(fun() -> Self ! {other, outcome(fun() -> zstd:stream(C, <<"x">>) end)} end),
    receive
        {other, Other} -> show(other_process, Other)
    end,
    ok = zstd:close(C),
    show(closed, [
        outcome(fun() -> zstd:stream(C, <<"x">>) end),
        outcome(fun() -> zstd:close(C) end),
        outcome(fun() -> zstd:get_parameter(C, compressionLevel) end)
    ]),
    show(wrong_kind, [outcome(fun() -> zstd:reset({compress, element(2, D)}) end)]),
    zstd:close(D).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
