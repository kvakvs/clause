%% OTP 29 erl_error types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([format_fun/0,format_options/0,stack_trim_fun/0]).
-type column() :: pos_integer().
-type stack_trim_fun() :: fun((module(), atom(), arity()) -> boolean()).
-type format_fun() :: fun((term(), column()) -> iolist()).
-type format_options() ::
          #{column => column(),
            stack_trim_fun => stack_trim_fun(),
            format_fun => format_fun()}.
