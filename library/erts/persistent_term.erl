%% Clause's replacement for OTP's persistent_term (docs/libraries/library.md#runtime-module-replacements), generated from
%% persistent_term.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Every function is a runtime builtin that replaces its placeholder (plan step
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

erase(_) -> clause_builtin_placeholder.
get() -> clause_builtin_placeholder.
get(_) -> clause_builtin_placeholder.
get(_, _) -> clause_builtin_placeholder.
info() -> clause_builtin_placeholder.
put(_, _) -> clause_builtin_placeholder.
put_new(_, _) -> clause_builtin_placeholder.
