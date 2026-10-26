%% Clause's replacement for OTP's zlib (docs/libraries/library.md#runtime-module-replacements), generated from
%% zlib.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Streams are handle references over the pinned zlib (plan step 41Q).
-module(zlib).
-export([
    close/1,
    compress/1,
    deflate/2,
    deflate/3,
    deflateEnd/1,
    deflateInit/1,
    deflateInit/2,
    deflateInit/6,
    deflateParams/3,
    deflateReset/1,
    deflateSetDictionary/2,
    gunzip/1,
    gzip/1,
    inflate/2,
    inflate/3,
    inflateEnd/1,
    inflateGetDictionary/1,
    inflateInit/1,
    inflateInit/2,
    inflateInit/3,
    inflateReset/1,
    inflateSetDictionary/2,
    on_load/0,
    open/0,
    safeInflate/2,
    set_controlling_process/2,
    uncompress/1,
    unzip/1,
    zip/1
]).
-include("zlib.types.hrl").

%% Streams over the clause_zlib builtins (plan step 41Q, docs/builtins.md#zlib-module); output is a list of
%% binaries, [] when there is none.

-define(DEFAULT_WBITS, 15).

open() -> clause_zlib:open().

close(Z) ->
    erase({?MODULE, pending, Z}),
    erase({?MODULE, input, Z}),
    clause_zlib:close(Z).

set_controlling_process(Z, Pid) -> clause_zlib:set_controller(Z, Pid).

deflateInit(Z) -> deflateInit(Z, default).

deflateInit(Z, Level) -> deflateInit(Z, Level, deflated, ?DEFAULT_WBITS, 8, default).

deflateInit(Z, Level, Method, WindowBits, MemLevel, Strategy) ->
    Parameters = {
        arg_level(Level),
        arg_method(Method),
        arg_bitsz(WindowBits),
        arg_mem(MemLevel),
        arg_strategy(Strategy)
    },
    clause_zlib:deflate_init(Z, Parameters).

deflate(Z, Data) -> deflate(Z, Data, none).

%% The compressed output of Data, after any output deflateParams/3 kept.
deflate(Z, Data, Flush) ->
    Input = input(Data),
    Output = clause_zlib:deflate(Z, Input, arg_flush(Flush)),
    case erase({?MODULE, pending, Z}) of
        undefined -> chunks(Output);
        Pending -> Pending ++ chunks(Output)
    end.

deflateEnd(Z) ->
    erase({?MODULE, pending, Z}),
    clause_zlib:deflate_end(Z).

deflateReset(Z) ->
    erase({?MODULE, pending, Z}),
    clause_zlib:deflate_reset(Z).

%% New parameters; the input compressed so far is kept for the next deflate.
deflateParams(Z, Level, Strategy) ->
    Output = clause_zlib:deflate_params(Z, {arg_level(Level), arg_strategy(Strategy)}),
    case chunks(Output) of
        [] -> ok;
        Chunks -> put({?MODULE, pending, Z}, pending(Z) ++ Chunks)
    end,
    ok.

pending(Z) ->
    case get({?MODULE, pending, Z}) of
        undefined -> [];
        Pending -> Pending
    end.

deflateSetDictionary(Z, Dictionary) -> clause_zlib:deflate_set_dictionary(Z, input(Dictionary)).

inflateInit(Z) -> inflateInit(Z, ?DEFAULT_WBITS).

inflateInit(Z, WindowBits) -> inflateInit(Z, WindowBits, cut).

inflateInit(Z, WindowBits, EoSBehavior) ->
    clause_zlib:inflate_init(Z, {arg_bitsz(WindowBits), arg_eos_behavior(EoSBehavior)}).

inflate(Z, Data) -> inflate(Z, Data, []).

%% The decompressed output; a stream that needs a dictionary raises {need_dictionary, Adler} unless
%% {exception_on_need_dict, false} asks for {need_dictionary, Adler, Output}.
inflate(Z, Data, Options) when is_list(Options) ->
    case inflate_queued(Z, Data) of
        {{need_dictionary, Adler}, Output} ->
            case proplists_value(exception_on_need_dict, Options, true) of
                true -> erlang:error({need_dictionary, Adler});
                false -> {need_dictionary, Adler, chunks(Output)}
            end;
        {_, Output} ->
            chunks(Output)
    end.

%% {finished, Output} once all queued input is decompressed, which one call always does here, or
%% {need_dictionary, Adler, Output}.
safeInflate(Z, Data) ->
    case inflate_queued(Z, Data) of
        {{need_dictionary, Adler}, Output} -> {need_dictionary, Adler, chunks(Output)};
        {_, Output} -> {finished, chunks(Output)}
    end.

%% Inflate Data after the input a need_dictionary answer kept; input left by a new need_dictionary is kept again.
inflate_queued(Z, Data) ->
    Input =
        case erase({?MODULE, input, Z}) of
            undefined -> input(Data);
            Queued -> <<Queued/binary, (input(Data))/binary>>
        end,
    case clause_zlib:inflate(Z, Input) of
        {Status, Output, Rest} ->
            put({?MODULE, input, Z}, Rest),
            {Status, Output};
        Result ->
            Result
    end.

inflateEnd(Z) ->
    erase({?MODULE, input, Z}),
    clause_zlib:inflate_end(Z).

inflateReset(Z) ->
    erase({?MODULE, input, Z}),
    clause_zlib:inflate_reset(Z).

inflateSetDictionary(Z, Dictionary) -> clause_zlib:inflate_set_dictionary(Z, input(Dictionary)).

inflateGetDictionary(Z) -> clause_zlib:inflate_get_dictionary(Z).

%% Whole-buffer compression: zlib format, raw deflate (zip) and gzip.
compress(Data) -> deflate_all(Data, ?DEFAULT_WBITS).

zip(Data) -> deflate_all(Data, -?DEFAULT_WBITS).

gzip(Data) -> deflate_all(Data, 16 + ?DEFAULT_WBITS).

uncompress(Data) ->
    case byte_size(input(Data)) of
        Size when Size >= 8 -> inflate_all(Data, ?DEFAULT_WBITS, cut);
        _ -> erlang:error(data_error)
    end.

unzip(Data) -> inflate_all(Data, -?DEFAULT_WBITS, cut).

%% Concatenated gzip members decompress one after the other.
gunzip(Data) -> inflate_all(Data, 16 + ?DEFAULT_WBITS, reset).

deflate_all(Data, WindowBits) ->
    Input = input(Data),
    Z = open(),
    try
        deflateInit(Z, default, deflated, WindowBits, 8, default),
        Output = deflate(Z, Input, finish),
        deflateEnd(Z),
        iolist_to_binary(Output)
    after
        close(Z)
    end.

inflate_all(Data, WindowBits, EoSBehavior) ->
    Input = input(Data),
    Z = open(),
    try
        inflateInit(Z, WindowBits, EoSBehavior),
        Output = inflate(Z, Input),
        inflateEnd(Z),
        iolist_to_binary(Output)
    after
        close(Z)
    end.

%% The NIF loader of OTP's module: nothing to load.
on_load() -> ok.

%% Input data as one binary; badarg for anything but iodata.
input(Data) when is_binary(Data) -> Data;
input(Data) ->
    try
        iolist_to_binary(Data)
    catch
        error:_ -> erlang:error(badarg)
    end.

chunks(<<>>) -> [];
chunks(Binary) -> [Binary].

proplists_value(Key, [{Key, Value} | _], _) -> Value;
proplists_value(Key, [_ | Rest], Default) -> proplists_value(Key, Rest, Default);
proplists_value(_, [], Default) -> Default.

arg_flush(none) -> 0;
arg_flush(sync) -> 2;
arg_flush(full) -> 3;
arg_flush(finish) -> 4;
arg_flush(_) -> erlang:error(bad_flush_mode).

arg_level(none) -> 0;
arg_level(best_speed) -> 1;
arg_level(best_compression) -> 9;
arg_level(default) -> -1;
arg_level(Level) when is_integer(Level), Level >= 0, Level =< 9 -> Level;
arg_level(_) -> erlang:error(bad_compression_level).

arg_strategy(filtered) -> 1;
arg_strategy(huffman_only) -> 2;
arg_strategy(rle) -> 3;
arg_strategy(default) -> 0;
arg_strategy(_) -> erlang:error(bad_compression_strategy).

arg_method(deflated) -> 8;
arg_method(_) -> erlang:error(bad_compression_method).

arg_eos_behavior(error) -> 0;
arg_eos_behavior(reset) -> 1;
arg_eos_behavior(cut) -> 2;
arg_eos_behavior(_) -> erlang:error(bad_eos_behavior).

arg_bitsz(Bits) when
    is_integer(Bits), (8 =< Bits andalso Bits < 48) orelse (-15 =< Bits andalso Bits =< -8)
->
    Bits;
arg_bitsz(_) ->
    erlang:error(bad_windowbits).

arg_mem(Level) when is_integer(Level), 1 =< Level, Level =< 9 -> Level;
arg_mem(_) -> erlang:error(bad_memlevel).
