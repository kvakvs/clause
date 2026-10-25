%% erl_eval's bit syntax segments and pattern matching (plan 21 steps LIB32, LIB34C, docs/builtins.md#erl-eval):
%% runtime builtins called by library/stdlib/erl_eval.erl.
-module(clause_erl_eval).
-export([bit_type/2, bit_segment/3, bit_take/3, match/4, bits_gen/4]).

%% bit_type(Size, Types): {Size1, {Type, Unit, Sign, Endian}} as eval_bits settles a bin_element's specifiers, Size1
%% being the default size (all, undefined, 8 or 64) when Size is default and Size otherwise; erl_bits' errors raise.
bit_type(_, _) -> clause_builtin_placeholder.

%% bit_segment(Value, Size, Spec): the segment as a bitstring, or the atom badarg or system_limit.
bit_segment(_, _, _) -> clause_builtin_placeholder.

%% bit_take(Bits, Size, Spec): {Value, Rest} of the segment at the start of Bits, or nomatch.
bit_take(_, _, _) -> clause_builtin_placeholder.

%% match(Pattern, Term, Bindings, OuterBindings): {match, Bindings1}, nomatch, or invalid for an illegal pattern;
%% map keys and segment sizes are evaluated over OuterBindings.
match(_, _, _, _) -> clause_builtin_placeholder.

%% bits_gen(Fields, Bits, Bindings, OuterBindings): a binary generator's pattern at the front of Bits: {match, Rest,
%% Bindings1}, {nomatch, Rest} or done; bad specifiers raise.
bits_gen(_, _, _, _) -> clause_builtin_placeholder.
