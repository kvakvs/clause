%% Clause's replacement for OTP's atomics (docs/libraries/library.md#runtime-module-replacements), generated from
%% atomics.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(atomics).
-export([
    add/3,
    add_get/3,
    compare_exchange/4,
    exchange/3,
    get/2,
    info/1,
    new/2,
    put/3,
    sub/3,
    sub_get/3
]).
-include("atomics.types.hrl").

add(_, _, _) -> clause_builtin_placeholder.
add_get(_, _, _) -> clause_builtin_placeholder.
compare_exchange(_, _, _, _) -> clause_builtin_placeholder.
exchange(_, _, _) -> clause_builtin_placeholder.
get(_, _) -> clause_builtin_placeholder.
info(_) -> clause_builtin_placeholder.
new(_, _) -> clause_builtin_placeholder.
put(_, _, _) -> clause_builtin_placeholder.
sub(_, _, _) -> clause_builtin_placeholder.
sub_get(_, _, _) -> clause_builtin_placeholder.
