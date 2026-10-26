%% Clause's erl_abstract_code (plan 21 step LIB54F, docs/libraries/library.md): the debug information backend of beams compiled
%% with debug_info, as beam_lib asks it for a module's abstract code. Core Erlang needs the compiler, which Clause
%% programs do not have: that conversion fails as a failed one does in OTP.
-module(erl_abstract_code).
-export([debug_info/4]).

debug_info(_Format, _Module, {none, _CompilerOpts}, _Opts) ->
    {error, missing};
debug_info(erlang_v1, _Module, {AbstrCode, _CompilerOpts}, _Opts) ->
    {ok, AbstrCode};
debug_info(core_v1, _Module, {_AbstrCode, _CompilerOpts}, _Opts) ->
    {error, failed_conversion};
debug_info(_, _, _, _) ->
    {error, unknown_format}.
