%% OTP 29 erl_tar types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([tar_descriptor/0]).
-record(reader, {
    handle :: user_data(),
    access :: read | write | ram,
    pos = 0 :: non_neg_integer(),
    func :: file_op()
}).
-opaque tar_descriptor() :: #reader{}.
-type user_data() :: term().
-type file_op() ::
    fun((close, file:io_device()) -> ok | {error, term()})
    | fun((position, {file:io_device(), file:location()}) -> {ok, integer()} | {error, term()})
    | fun((read2, {file:io_device(), non_neg_integer()}) -> {ok, string() | binary()} | eof | {error, term()})
    | fun((write, {file:io_device(), iodata()}) -> ok | {error, term()}).
