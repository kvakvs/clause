%% Clause's replacement for OTP's mnesia_frag_hash (docs/libraries/library.md#runtime-module-replacements), generated from
%% mnesia_frag_hash.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}); a catalog builtin replaces its clause_builtin_placeholder body.
-module(mnesia_frag_hash).
-export([
    add_frag/1,
    del_frag/1,
    init_state/2,
    key_to_frag_number/2,
    match_spec_to_frag_numbers/2
]).

add_frag(_) -> erlang:error({not_implemented, add_frag, 1}).
del_frag(_) -> erlang:error({not_implemented, del_frag, 1}).
init_state(_, _) -> erlang:error({not_implemented, init_state, 2}).
key_to_frag_number(_, _) -> erlang:error({not_implemented, key_to_frag_number, 2}).
match_spec_to_frag_numbers(_, _) -> erlang:error({not_implemented, match_spec_to_frag_numbers, 2}).
