%% Clause's replacement for OTP's persistent_term (docs/library.md#runtime-module-replacements), generated from
%% persistent_term.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Every function is a runtime builtin that replaces its stub (plan step
%% 41M2, docs/builtins.md#persistent-terms).
-module(persistent_term).
-export([
    erase/1,
    get/0,
    get/1,
    get/2,
    info/0,
    put/2,
    put_new/2
]).

erase(_) -> erlang:error({not_implemented, erase, 1}).
get() -> erlang:error({not_implemented, get, 0}).
get(_) -> erlang:error({not_implemented, get, 1}).
get(_, _) -> erlang:error({not_implemented, get, 2}).
info() -> erlang:error({not_implemented, info, 0}).
put(_, _) -> erlang:error({not_implemented, put, 2}).
put_new(_, _) -> erlang:error({not_implemented, put_new, 2}).
