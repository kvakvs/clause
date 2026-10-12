%% Clause's replacement for OTP's atomics (docs/library.md#runtime-module-replacements), generated from
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

add(_, _, _) -> erlang:error({not_implemented, add, 3}).
add_get(_, _, _) -> erlang:error({not_implemented, add_get, 3}).
compare_exchange(_, _, _, _) -> erlang:error({not_implemented, compare_exchange, 4}).
exchange(_, _, _) -> erlang:error({not_implemented, exchange, 3}).
get(_, _) -> erlang:error({not_implemented, get, 2}).
info(_) -> erlang:error({not_implemented, info, 1}).
new(_, _) -> erlang:error({not_implemented, new, 2}).
put(_, _, _) -> erlang:error({not_implemented, put, 3}).
sub(_, _, _) -> erlang:error({not_implemented, sub, 3}).
sub_get(_, _, _) -> erlang:error({not_implemented, sub_get, 3}).
