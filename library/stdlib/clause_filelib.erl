%% clause_filelib: the wildcard pattern builtins of filelib (plan 21 step LIB51, runtime/src/builtins/filelib.cpp):
%% escape/1 spells escapes so filename:split/1 keeps them, compile_part/1 compiles one part of a split pattern,
%% matches/3 and match/2 match directory entries against a compiled part. Each catalog builtin replaces its
%% clause_builtin_placeholder body.
-module(clause_filelib).
-export([escape/1, compile_part/1, matches/3, match/2]).

escape(_) -> clause_builtin_placeholder.
compile_part(_) -> clause_builtin_placeholder.
matches(_, _, _) -> clause_builtin_placeholder.
match(_, _) -> clause_builtin_placeholder.
