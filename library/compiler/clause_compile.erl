%% clause_compile: the builtins of Clause's compile (plan 21 step LIB77E1, runtime/src/builtins/compile_messages.cpp).
%% quote_source/3 quotes the source line of a message's location as OTP's sys_messages does. Each catalog builtin
%% replaces its clause_builtin_placeholder body.
-module(clause_compile).
-export([quote_source/3]).

%% quote_source(Source, Encoding, Location): the line of Location in the Source binary, read as utf8 or latin1, with
%% a marker under its column: "% Line| Text\n%     |   ^\n\n" as UTF-8, or <<>> when the source has no such line.
quote_source(_, _, _) -> clause_builtin_placeholder.
