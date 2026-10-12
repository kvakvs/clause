%% Clause's replacement for OTP's counters (docs/library.md#runtime-module-replacements), generated from
%% counters.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
-module(counters).
-export([
    add/3,
    get/2,
    info/1,
    new/2,
    put/3,
    sub/3
]).

add(_, _, _) -> erlang:error({not_implemented, add, 3}).
get(_, _) -> erlang:error({not_implemented, get, 2}).
info(_) -> erlang:error({not_implemented, info, 1}).
new(_, _) -> erlang:error({not_implemented, new, 2}).
put(_, _, _) -> erlang:error({not_implemented, put, 3}).
sub(_, _, _) -> erlang:error({not_implemented, sub, 3}).
