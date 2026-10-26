%% OTP 29 erl_lint types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([error_description/0,error_info/0,fun_used_vars/0]).
-type fun_used_vars() ::
          #{erl_parse:abstract_expr() => {[atom()], fun_used_vars()}}.
-type error_description() :: term().
-type error_info() ::
          {erl_anno:location() | none, module(), error_description()}.
