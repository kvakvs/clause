%% Clause's zstd (plan 21 step LIB54E, docs/library.md): zstd compression over the clause_zstd builtins, which hold
%% libzstd 1.5.7 contexts and dictionaries as OTP's NIF does (docs/builtins.md#zstd-module). The functions here follow
%% OTP's documentation and its iodata, streaming and frame handling, so output and chunking equal OTP's.
-module(zstd).
-export([
    close/1,
    compress/1,
    compress/2,
    context/1,
    context/2,
    decompress/1,
    decompress/2,
    dict/2,
    dict/3,
    finish/2,
    flush/1,
    get_dict_id/1,
    get_frame_header/1,
    get_parameter/2,
    reset/1,
    set_parameter/3,
    stream/2
]).
-include("zstd.types.hrl").
%% The parameter keys, frame types and results never name a called module.
-compile({clause_liveness, strict}).

% libzstd's end directives.
-define(ZSTD_e_continue, 0).
-define(ZSTD_e_flush, 1).
-define(ZSTD_e_end, 2).

set_parameter({compress, Ctx}, dictionary, {cdict, Dict}) when is_reference(Dict) ->
    clause_zstd:ref_compress_dictionary(Ctx, Dict);
set_parameter({compress, Ctx}, dictionary, Dict) ->
    clause_zstd:load_compress_dictionary(Ctx, Dict);
set_parameter({compress, Ctx}, pledgedSrcSize, Size) ->
    clause_zstd:set_pledged_src_size(Ctx, Size);
set_parameter({decompress, Ctx}, dictionary, {ddict, Dict}) when is_reference(Dict) ->
    clause_zstd:ref_decompress_dictionary(Ctx, Dict);
set_parameter({decompress, Ctx}, dictionary, Dict) ->
    clause_zstd:load_decompress_dictionary(Ctx, Dict);
set_parameter({compress, Ctx}, Key, Value) ->
    clause_zstd:set_compress_parameter(Ctx, Key, Value);
set_parameter({decompress, Ctx}, Key, Value) ->
    clause_zstd:set_decompress_parameter(Ctx, Key, Value);
set_parameter(Ctx, _Key, _Value) ->
    error({badarg, {invalid_context, Ctx}}).

get_parameter({compress, Ctx}, Key) -> clause_zstd:get_compress_parameter(Ctx, Key);
get_parameter({decompress, Ctx}, Key) -> clause_zstd:get_decompress_parameter(Ctx, Key);
get_parameter(Ctx, _Key) -> error({badarg, {invalid_context, Ctx}}).

dict(Mode, Dict) -> dict(Mode, Dict, #{}).

dict(compress, Dict, #{compressionLevel := Level}) -> {ok, {cdict, clause_zstd:create_cdict(Dict, Level)}};
dict(compress, Dict, Params = #{}) -> dict(compress, Dict, Params#{compressionLevel => 0});
dict(decompress, Dict, #{}) -> {ok, {ddict, clause_zstd:create_ddict(Dict)}}.

% The id of a dictionary, or of the dictionary a frame names; a binary that is no frame is read as a dictionary.
get_dict_id({cdict, Dict}) ->
    clause_zstd:dict_id_of_cdict(Dict);
get_dict_id({ddict, Dict}) ->
    clause_zstd:dict_id_of_ddict(Dict);
get_dict_id(DictOrFrame) ->
    case get_frame_header(DictOrFrame) of
        {error, _Reason} -> clause_zstd:dict_id_of_dict(DictOrFrame);
        {ok, _} -> clause_zstd:dict_id_of_frame(DictOrFrame)
    end.

get_frame_header(Frame) ->
    try
        clause_zstd:get_frame_header(Frame)
    catch
        error:{zstd_error, Reason} -> {error, Reason}
    end.

context(Mode) when Mode =:= compress; Mode =:= decompress -> context(Mode, #{}).

context(compress, Options) ->
    Ref = {compress, clause_zstd:init_compress()},
    [ok = set_parameter(Ref, Key, Value) || Key := Value <- Options],
    {ok, Ref};
context(decompress, Options) ->
    Ref = {decompress, clause_zstd:init_decompress()},
    [ok = set_parameter(Ref, Key, Value) || Key := Value <- Options],
    {ok, Ref}.

% The builtins take binaries; iodata goes through its iovec, the binaries after the first coming back as the rest.
stream(Ctx, Data) when is_list(Data) ->
    try erlang:iolist_to_iovec(Data) of
        [] ->
            stream(Ctx, <<>>);
        [H] ->
            stream(Ctx, H);
        [H | T] ->
            case stream(Ctx, H) of
                {continue, Rem, Out} -> {continue, [Rem | T], Out};
                {continue, Out} -> {continue, T, Out}
            end
    catch
        _:_ -> error(badarg)
    end;
stream({compress, Ref}, Data) ->
    clause_zstd:compress_stream(Ref, Data, ?ZSTD_e_continue);
stream({decompress, Ref}, Data) ->
    clause_zstd:decompress_stream(Ref, Data, ?ZSTD_e_continue).

flush({compress, Ref} = Ctx) ->
    case clause_zstd:compress_stream(Ref, <<>>, ?ZSTD_e_flush) of
        {continue, _} = Res ->
            Res;
        {done, Output} ->
            {continue, Output};
        {flush, Output0} ->
            {continue, Output} = flush(Ctx),
            {continue, iolist_to_binary([Output0, Output])}
    end;
flush(Ctx = {decompress, _}) ->
    error({badarg, {invalid_context, Ctx}}).

finish({compress, Ref}, Data) when is_binary(Data) ->
    finish_1(fun clause_zstd:compress_stream/3, Ref, Data);
finish({decompress, Ref} = DCtx, Data) when is_binary(Data) ->
    Result = finish_1(fun clause_zstd:decompress_stream/3, Ref, Data),
    reset(DCtx),
    Result;
finish(Ctx, []) ->
    finish(Ctx, <<>>);
finish(Ctx, [Data]) when is_binary(Data) ->
    finish(Ctx, Data);
finish(Ctx, Data) ->
    {Out, Remain} =
        case stream(Ctx, Data) of
            {continue, Rest, Output} -> {Output, Rest};
            {continue, Output} -> {Output, <<>>}
        end,
    {done, Tail} = finish(Ctx, Remain),
    {done, [Out | Tail]}.

finish_1(Codec, Ref, Data) when is_binary(Data) ->
    case Codec(Ref, Data, ?ZSTD_e_end) of
        {continue, Remainder, Output} ->
            {done, Tail} = finish_1(Codec, Ref, Remainder),
            {done, [Output | Tail]};
        {flush, Output} ->
            {done, Tail} = finish_1(Codec, Ref, <<>>),
            {done, [Output | Tail]};
        {done, Output} ->
            {done, [Output]}
    end.

reset({compress, Ref}) -> clause_zstd:compress_reset(Ref);
reset({decompress, Ref}) -> clause_zstd:decompress_reset(Ref).

close({compress, Ref}) -> clause_zstd:compress_close(Ref);
close({decompress, Ref}) -> clause_zstd:decompress_close(Ref).

compress(Data) -> compress(Data, #{}).

compress(Data, Options) when is_map(Options) ->
    {ok, Ref} = context(compress, Options),
    try
        compress(Data, Ref)
    after
        close(Ref)
    end;
compress(Data, {compress, Ref}) ->
    % Empty data still makes an empty frame.
    IOV =
        case erlang:iolist_to_iovec(Data) of
            [] -> [<<>>];
            IOV0 -> IOV0
        end,
    ok = clause_zstd:set_pledged_src_size(Ref, erlang:iolist_size(IOV)),
    codec_loop(fun clause_zstd:compress_stream/3, Ref, IOV).

decompress(Data) -> decompress(Data, #{}).

decompress(Data, Options) when is_map(Options) ->
    {ok, DCtx} = context(decompress, Options),
    try
        decompress(Data, DCtx)
    after
        close(DCtx)
    end;
decompress(Data, {decompress, Ref}) ->
    codec_loop(fun clause_zstd:decompress_stream/3, Ref, erlang:iolist_to_iovec(Data)).

% Each binary of the iovec in turn, the last one ending the frame; output left in libzstd is fetched with no input.
codec_loop(Codec, Ref, [Data | Next]) ->
    EndDirective =
        case Next of
            [] -> ?ZSTD_e_end;
            _ -> ?ZSTD_e_continue
        end,
    case Codec(Ref, Data, EndDirective) of
        {continue, Remainder, Output} -> [Output | codec_loop(Codec, Ref, [Remainder | Next])];
        {continue, <<>>} -> codec_loop(Codec, Ref, Next);
        {continue, Output} -> [Output | codec_loop(Codec, Ref, Next)];
        {flush, Output} -> [Output | codec_loop(Codec, Ref, [<<>>])];
        {done, Output} -> [Output]
    end.
