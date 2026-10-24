%% erl_eval's bit syntax segments (plan 21 step LIB32, docs/builtins.md#erl-eval): runtime builtins called by
%% library/stdlib/erl_eval.erl.
-module(clause_erl_eval).
-export([bit_type/2, bit_segment/3, bit_take/3]).

%% bit_type(Size, Types): {Size1, {Type, Unit, Sign, Endian}} as eval_bits settles a bin_element's specifiers, Size1
%% being the default size (all, undefined, 8 or 64) when Size is default and Size otherwise; erl_bits' errors raise.
bit_type(_, _) -> clause_builtin_placeholder.

%% bit_segment(Value, Size, Spec): the segment as a bitstring, or the atom badarg or system_limit.
bit_segment(_, _, _) -> clause_builtin_placeholder.

%% bit_take(Bits, Size, Spec): {Value, Rest} of the segment at the start of Bits, or nomatch.
bit_take(_, _, _) -> clause_builtin_placeholder.
