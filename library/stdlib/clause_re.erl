-module(clause_re).
-export([compile/2, run/4]).

% Clause's PCRE2 builtins for the re module (plan step 41ZB, docs/builtins.md#re-module), which replace these bodies
% at registration; library/stdlib/re.erl answers OTP's re functions over them.

% {ok, Pattern} or {error, {Message, Offset}} for a regex binary.
compile(_, _) -> erlang:error(undef).

% One match of a binary subject, as OTP's re:run BIF answers it.
run(_, _, _, _) -> erlang:error(undef).
