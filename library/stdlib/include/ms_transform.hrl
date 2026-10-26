%% Clause's ms_transform.hrl (plan 21 step LIB75, docs/libraries/library.md): the ets:fun2ms/1 and dbg:fun2ms/1 calls of a
%% module that includes it are transformed by ms_transform's parse transform, as with OTP's header.
-compile({parse_transform, ms_transform}).
