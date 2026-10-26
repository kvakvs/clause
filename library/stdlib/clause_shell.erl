-module(clause_shell).
-export([run/1, start_interactive/1, whereis/0]).

% The terminal side of the interactive shell (plan 21 step LIB74, docs/libraries/library.md#interactive-shell): the process
% registered as clause_shell starts shell servers on the standard streams, as OTP's user_drv does, and starts a new
% one when a shell exits. Standard input is read by io's standard input server, line by line as a terminal in
% cooked mode delivers it.

%% The entry of a program built with --shell: the interactive shell; the program ends when it halts.
run(_Args) ->
    ok = start_interactive(default),
    receive
    after infinity -> ok
    end.

%% Start the interactive shell: default, noshell (the terminal without a shell), {M, F, A} (a shell of one's own)
%% or remote (no other nodes to connect to).
start_interactive(remote) ->
    {error, noconnection};
start_interactive(Initial) ->
    Caller = self(),
    Terminal = spawn(fun() -> terminal(Caller, Initial) end),
    Ref = monitor(process, Terminal),
    receive
        {Terminal, Reply} ->
            demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, process, Terminal, Reason} ->
            exit(Reason)
    end.

%% The current shell process, or undefined.
whereis() ->
    case erlang:whereis(?MODULE) of
        undefined ->
            undefined;
        Terminal ->
            Ref = monitor(process, Terminal),
            Terminal ! {whereis, self(), Ref},
            receive
                {Ref, Shell} ->
                    demonitor(Ref, [flush]),
                    Shell;
                {'DOWN', Ref, process, Terminal, _} ->
                    undefined
            end
    end.

% Register as the terminal unless one is started; print the system version and start the first shell.
terminal(Caller, Initial) ->
    try register(?MODULE, self()) of
        true ->
            case Initial of
                noshell ->
                    Caller ! {self(), ok},
                    serve(undefined, Initial);
                _ ->
                    io:put_chars([erlang:system_info(system_version), $\n]),
                    Shell = start_shell(Initial),
                    Caller ! {self(), ok},
                    serve(Shell, Initial)
            end
    catch
        error:badarg -> Caller ! {self(), {error, already_started}}
    end.

start_shell(default) ->
    Shell = shell:start(),
    link(Shell),
    Shell;
start_shell({M, F, A}) ->
    Shell = apply(M, F, A),
    link(Shell),
    Shell.

% Answer whereis/0; a shell that exits is replaced, as OTP's user_drv restarts it.
serve(Shell, Initial) ->
    process_flag(trap_exit, true),
    receive
        {whereis, From, Ref} ->
            From ! {Ref, Shell},
            serve(Shell, Initial);
        {'EXIT', Shell, _} when Shell =/= undefined ->
            io:put_chars("*** Shell process terminated! ***\n"),
            serve(start_shell(Initial), Initial);
        _ ->
            serve(Shell, Initial)
    end.
