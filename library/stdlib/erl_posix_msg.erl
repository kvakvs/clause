%% Clause's replacement for OTP's erl_posix_msg (docs/library.md#runtime-module-replacements), generated from
%% erl_posix_msg.terms by tests/compiler/library/library_modules.py; edited by hand from here on. OTP's module is
%% internal; file:format_error/1 needs it (plan 21 step LIB50). message/1 is a catalog builtin
%% (runtime/src/builtins/erl_posix_msg.cpp) that replaces its clause_builtin_placeholder body.
-module(erl_posix_msg).
-export([
    message/1
]).

message(_) -> clause_builtin_placeholder.
