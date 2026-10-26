%% OTP 29 zip types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([create_option/0, filename/0, handle/0]).
-type extra() :: [extended_timestamp | uid_gid].
-type create_option() ::
    memory
    | cooked
    | verbose
    | {comment, Comment :: string()}
    | {cwd, CWD :: file:filename()}
    | {compress, What :: extension_spec()}
    | {uncompress, What :: extension_spec()}
    | {extra, extra()}.
-type extension() :: string().
-type extension_spec() ::
    all
    | [Extension :: extension()]
    | {add, [Extension :: extension()]}
    | {del, [Extension :: extension()]}.
-type filename() :: file:filename().
-opaque handle() :: pid().
