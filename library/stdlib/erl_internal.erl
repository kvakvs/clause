%% Clause's erl_internal (plan 21 step LIB8, docs/builtins.md#compiler-tables): OTP's BIF, guard, type test and
%% operator tables and add_predefined_functions/1 as runtime builtins; bif/2 answers from erlang.terms' auto-import
%% flags, as the compiler's own table does.
-module(erl_internal).
-export([
    add_predefined_functions/1,
    arith_op/2,
    bif/2,
    bif/3,
    bool_op/2,
    comp_op/2,
    guard_bif/2,
    is_type/2,
    list_op/2,
    new_type_test/2,
    old_type_test/2,
    op_type/2,
    send_op/2,
    type_test/2
]).

add_predefined_functions(_) -> clause_builtin_placeholder.
arith_op(_, _) -> clause_builtin_placeholder.
bif(_, _) -> clause_builtin_placeholder.
bif(_, _, _) -> clause_builtin_placeholder.
bool_op(_, _) -> clause_builtin_placeholder.
comp_op(_, _) -> clause_builtin_placeholder.
guard_bif(_, _) -> clause_builtin_placeholder.
is_type(_, _) -> clause_builtin_placeholder.
list_op(_, _) -> clause_builtin_placeholder.
new_type_test(_, _) -> clause_builtin_placeholder.
old_type_test(_, _) -> clause_builtin_placeholder.
op_type(_, _) -> clause_builtin_placeholder.
send_op(_, _) -> clause_builtin_placeholder.
type_test(_, _) -> clause_builtin_placeholder.
