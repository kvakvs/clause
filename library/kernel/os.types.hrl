%% OTP 29 os types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([
    env_var_name/0,
    env_var_name_value/0,
    env_var_value/0,
    os_command/0,
    os_command_opts/0
]).
-type os_command() :: atom() | io_lib:chars().
-type os_command_opts() ::
    #{
        max_size => non_neg_integer() | infinity,
        exception_on_failure => boolean()
    }.
-type env_var_name() :: nonempty_string().
-type env_var_value() :: string().
-type env_var_name_value() :: nonempty_string().
