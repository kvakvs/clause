-module(shell_session).
-export([main/1]).

% The interactive shell started by the program (plan 21 step LIB74): commands come from standard input, and the shell
% halts the program at its end.
main(_) ->
    ok = shell:start_interactive(),
    receive
    after infinity -> ok
    end.
