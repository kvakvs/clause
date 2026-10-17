-module(clause_rand).
-export([
    bytes/3,
    jump/2,
    next/2,
    normal/2,
    seed/2,
    shuffle/3,
    uniform/2, uniform/3,
    uniform_real/2,
    ziggurat/1
]).

% rand's generators (plan 21 step LIB6, docs/builtins.md#random-numbers): seeding, words, OTP's generic
% algorithms and jumps on the state of one of OTP's algorithms (the first argument), and the normal
% distribution's ziggurat strips; runtime builtins that rand's functions call.
bytes(_, _, _) -> clause_builtin_placeholder.
jump(_, _) -> clause_builtin_placeholder.
next(_, _) -> clause_builtin_placeholder.
normal(_, _) -> clause_builtin_placeholder.
seed(_, _) -> clause_builtin_placeholder.
shuffle(_, _, _) -> clause_builtin_placeholder.
uniform(_, _) -> clause_builtin_placeholder.
uniform(_, _, _) -> clause_builtin_placeholder.
uniform_real(_, _) -> clause_builtin_placeholder.
ziggurat(_) -> clause_builtin_placeholder.
