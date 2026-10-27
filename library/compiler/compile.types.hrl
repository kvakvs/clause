%% OTP 29 compile types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([comp_ret/0,forms/0,option/0]).
-type abstract_code() :: [erl_parse:abstract_form()].
-type forms() :: abstract_code() | cerl:c_module().
-type option() :: atom() | {atom(), term()} | {d, atom(), term()}.
-type error_info() :: erl_lint:error_info().
-type errors() :: [{file:filename(), [error_info()]}].
-type warnings() :: [{file:filename(), [error_info()]}].
-type mod_ret() ::
          {ok, module()} |
          {ok, module(), cerl:c_module()} |
          {ok, module() | [], abstract_code()} |
          {ok, module(), warnings()}.
-type bin_ret() ::
          {ok, module(), binary()} |
          {ok, module(), binary(), warnings()}.
-type err_ret() :: error | {error, errors(), warnings()}.
-type comp_ret() :: mod_ret() | bin_ret() | err_ret().
