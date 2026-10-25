-module(shell_expand).
-export([main/1]).

% Completion in the interactive shell (plan 21 step LIB74B): commands come from standard input, and the shell
% halts the program at its end.
main(_) ->
    ok = shell:start_interactive(),
    receive
    after infinity -> ok
    end.
