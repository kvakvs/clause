%% Clause's replacement for OTP's erts_code_purger (docs/libraries/library.md#runtime-module-replacements), generated from
%% erts_code_purger.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). Linked code has no old code to purge (plan step 41N); the purger's own
%% process loop and on_load support wait for step 71.
-module(erts_code_purger).
-export([
    finish_after_on_load/2,
    pending_purge_lambda/3,
    purge/1,
    soft_purge/1,
    start/0,
    wait_for_request/0
]).

finish_after_on_load(_, _) -> erlang:error({not_implemented, finish_after_on_load, 2}).
pending_purge_lambda(_, _, _) -> erlang:error({not_implemented, pending_purge_lambda, 3}).
%% {WasOld, DidKill}: no module has old code.
purge(Module) when is_atom(Module) -> {false, false}.

soft_purge(_) -> true.
start() -> erlang:error({not_implemented, start, 0}).
wait_for_request() -> erlang:error({not_implemented, wait_for_request, 0}).
