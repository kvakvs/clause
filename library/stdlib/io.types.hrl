%% OTP 29 io types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([
    device/0,
    format/0,
    server_no_data/0,
    standard_error/0,
    standard_io/0,
    user/0
]).
-type standard_io() :: standard_io.
-type standard_error() :: standard_error.
-type user() :: user.
-type device() ::
    atom()
    | pid()
    | file:io_server()
    | standard_io()
    | standard_error()
    | user().
-type server_no_data() :: {error, ErrorDescription :: term()} | eof.
-type format() :: atom() | string() | binary().
