-module(clause_receive).
-export([peek/0, skip/0, take/0, restart/0, wait/1]).

% The mailbox steps of a compiled receive (plan step 41S, docs/builtins.md#prim-eval) as runtime builtins, which
% replace these bodies at registration. library/erts/prim_eval.erl runs erl_eval's receive over them.

peek() -> erlang:error(undef).
skip() -> erlang:error(undef).
take() -> erlang:error(undef).
restart() -> erlang:error(undef).
wait(_) -> erlang:error(undef).
