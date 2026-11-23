%% Distribution from the node start options (plan 30 step DIST4b1b, docs/distribution.md#node-start-options): when
%% --sname or --name names the node, the runtime runs boot/3 as the main process instead of the entry. It starts
%% kernel_sup with net_sup as OTP's kernel does at boot (DIST4b1d), runs the -on_load functions (plan 21 step MIM9d),
%% then calls the entry; clau links this module with --distribution and into programs that call net_kernel. Node mode
%% (--node, DIST10a, docs/executables.md#node-mode) boots the same way and then runs node/1 in the entry's place.
-module(clause_dist_start).
-export([boot/3, node/1]).

% Start kernel_sup and its services, distribution among them, then call Module:Function(Arguments...) in its place.
% A failed start halts the program with the report of OTP's kernel application.
boot(Module, Function, Arguments) ->
    case clause_kernel:ensure_started() of
        ok -> clause_on_load:boot(Module, Function, Arguments);
        {error, Reason} -> erlang:halt(clause_on_load:kernel_failure(Reason))
    end.

% Node mode: run OTP init's start instructions of -s and -run in order, as its boot does, then keep the node running
% until it halts. A failed instruction halts the node as OTP's init halts a failed boot.
node(Instructions) ->
    try
        lists:foreach(fun start_em/1, Instructions)
    catch
        Class:Reason:Stack -> erlang:halt(boot_failure(Class, Reason, Stack))
    end,
    receive
    after infinity -> ok
    end.

% One instruction under user's lead when it runs, as OTP's start_em/1 sets it; no words make no instruction.
start_em({_, []}) ->
    ok;
start_em({Kind, Words}) ->
    case whereis(user) of
        undefined -> ok;
        User -> group_leader(User, self())
    end,
    {Module, Function, Arguments} = instruction(Kind, Words),
    start_it(Module, Function, Arguments).

% OTP's instruction of -s (atom arguments) or -run (string arguments): Module:start(), Module:Function() or
% Module:Function(Arguments).
instruction(Kind, [Module | Rest]) ->
    Convert =
        case Kind of
            s -> fun list_to_atom/1;
            run -> fun(Word) -> Word end
        end,
    case Rest of
        [] -> {list_to_atom(Module), start, []};
        [Function] -> {list_to_atom(Module), list_to_atom(Function), []};
        [Function | Words] -> {list_to_atom(Module), list_to_atom(Function), [[Convert(W) || W <- Words]]}
    end.

% Apply one instruction with OTP's messages for a missing module or function and for a failure.
start_it(Module, Function, Arguments) ->
    case code:ensure_loaded(Module) of
        {module, Module} ->
            try
                apply(Module, Function, Arguments)
            catch
                error:undef:Stack ->
                    Arity = length(Arguments),
                    erlang:function_exported(Module, Function, Arity) orelse
                        message(
                            lists:concat(["Error! ", Module, ":", Function, "/", Arity, " is not exported.\r\n\r\n"])
                        ),
                    erlang:raise(error, undef, Stack);
                Class:Reason:Stack ->
                    erlang:display({Class, Reason, Stack}),
                    erlang:raise(Class, Reason, Stack)
            end;
        {error, _} ->
            message([
                "Error! Failed to load module '",
                atom_to_list(Module),
                "' because it cannot be found.\r\nMake sure that the module name is correct and that its .beam file\r\n"
                "is in the code path.\r\n\r\n"
            ]),
            erlang:error(undef)
    end.

% A message on standard error, as OTP's erlang:display_string/1 writes it.
message(Text) ->
    io:put_chars(standard_error, Text).

% The slogan OTP's init halts with when its boot process fails, the process's exit reason on one line.
boot_failure(Class, Reason, Stack) ->
    Exit =
        case Class of
            error -> {Reason, Stack};
            exit -> Reason;
            throw -> {{nocatch, Reason}, Stack}
        end,
    lists:flatten(["Runtime terminating during boot (", io_lib:format("~1000000p", [Exit]), ")"]).
