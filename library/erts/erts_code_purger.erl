%% Clause's replacement for OTP's erts_code_purger (docs/library.md#runtime-module-replacements), generated from
%% erts_code_purger.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
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
purge(_) -> erlang:error({not_implemented, purge, 1}).
soft_purge(_) -> erlang:error({not_implemented, soft_purge, 1}).
start() -> erlang:error({not_implemented, start, 0}).
wait_for_request() -> erlang:error({not_implemented, wait_for_request, 0}).
