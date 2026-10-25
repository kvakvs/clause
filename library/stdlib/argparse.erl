%% Clause's argparse (plan 21 step LIB70, docs/library.md): command validation, parsing and help texts are the
%% clause_argparse builtins (runtime/src/builtins/argparse/), following OTP's checks, errors and result shapes.
%% The builtins ask this module to call custom type funs, re:run/2,3 and help description funs: each answer is
%% handed back and the builtin replays its walk up to the next call, so each fun runs once, in OTP's order.
-module(argparse).
-export([
    run/3,
    parse/2,
    parse/3,
    help/1,
    help/2,
    format_error/1,
    validate/1,
    validate/2,
    format_error/2
]).
-include("argparse.types.hrl").

validate(Command) ->
    validate(Command, #{}).

% The program name, after checking the command; an invalid command raises {argparse, Kind, Path, Field, Text}.
validate(Command, Options) ->
    Prog = executable(Options),
    is_list(Prog) orelse
        erlang:error(badarg, [Command, Options], [{error_info, #{cause => #{2 => <<"progname is not valid">>}}}]),
    case clause_argparse:validate(Prog, Command, prefixes(Options)) of
        ok ->
            Prog;
        {Kind, Entity, Path, Field, Text} ->
            erlang:error({?MODULE, Kind, Path, Field, Text}, [Entity], [{error_info, #{cause => #{}}}])
    end.

parse(Args, Command) ->
    parse(Args, Command, #{}).

parse(Args, Command, Options) ->
    Prog = validate(Command, Options),
    Prefixes = prefixes(Options),
    Strings = [unicode:characters_to_list(Arg) || Arg <- Args],
    try
        parsed(Strings, Prog, Command, Prefixes, maps:find(default, Options), [])
    catch
        throw:Reason -> {error, Reason}
    end.

% Run the parser until it has every answer it asks for (newest first).
parsed(Args, Prog, Command, Prefixes, Default, Answers) ->
    case clause_argparse:parse(Args, Prog, Command, Prefixes, Default, Answers) of
        {custom, Fun, Arg} ->
            parsed(Args, Prog, Command, Prefixes, Default, [converted(Fun, Arg) | Answers]);
        {re, Arg, Re} ->
            parsed(Args, Prog, Command, Prefixes, Default, [re:run(Arg, Re) | Answers]);
        {re, Arg, Re, ReOptions} ->
            parsed(Args, Prog, Command, Prefixes, Default, [re:run(Arg, Re, ReOptions) | Answers]);
        Result ->
            Result
    end.

% A custom type's value; error:badarg is the parser's "failed validation".
converted(Fun, Arg) ->
    try
        {ok, Fun(Arg)}
    catch
        error:badarg -> badarg
    end.

help(Command) ->
    help(Command, #{}).

help(Command, Options) ->
    Prog = validate(Command, Options),
    helped(Prog, Command, Options, []).

% Format the help until every description fun has given its text.
helped(Prog, Command, Options, Answers) ->
    case clause_argparse:help(Prog, Command, Options, Answers) of
        {describe, Fun} -> helped(Prog, Command, Options, [Fun() | Answers]);
        {ok, Text} -> Text
    end.

% Parse, then call the matched command's handler; a parser error prints the error and the help, and halts.
run(Args, Command, Options) ->
    try parse(Args, Command, Options) of
        {ok, ArgMap, Path, SubCmd} ->
            handle(Command, ArgMap, tl(Path), SubCmd);
        {error, Reason} ->
            io:format("error: ~ts~n", [argparse:format_error(Reason)]),
            io:format("~ts", [argparse:help(Command, Options#{command => tl(element(1, Reason))})]),
            erlang:halt(1)
    catch
        error:Reason:Stack ->
            io:format(erl_error:format_exception(error, Reason, Stack)),
            erlang:halt(1)
    end.

format_error({Path, undefined, undefined, Details}) ->
    io_lib:format("~ts: ~ts", [format_path(Path), Details]);
format_error({Path, undefined, Actual, Details}) ->
    io_lib:format("~ts: unknown argument: ~ts~ts", [format_path(Path), Actual, Details]);
format_error({Path, #{name := Name}, undefined, Details}) ->
    io_lib:format("~ts: required argument missing: ~ts~ts", [format_path(Path), Name, Details]);
format_error({Path, #{name := Name}, Value, Details}) ->
    io_lib:format("~ts: invalid argument for ~ts: ~ts ~ts", [format_path(Path), Name, Value, Details]).

% The extended error information of a validation error, for erl_error (EEP 54).
format_error({?MODULE, command, Path, Field, Reason}, [{_M, _F, [Cmd], Info} | _]) ->
    #{cause := Cause} = proplists:get_value(error_info, Info, #{}),
    Cause#{
        general => <<"command specification is invalid">>,
        1 => io_lib:format("~tp", [Cmd]),
        reason => io_lib:format("command \"~ts\": invalid field '~ts', reason: ~ts", [format_path(Path), Field, Reason])
    };
format_error({?MODULE, argument, Path, Field, Reason}, [{_M, _F, [Arg], Info} | _]) ->
    #{cause := Cause} = proplists:get_value(error_info, Info, #{}),
    Cause#{
        general => "argument specification is invalid",
        1 => io_lib:format("~tp", [Arg]),
        reason => io_lib:format(
            "command \"~ts\", argument '~ts', invalid field '~ts': ~ts",
            [format_path(Path), maps:get(name, Arg, ""), Field, Reason]
        )
    }.

format_path(Commands) ->
    lists:join(" ", Commands).

prefixes(Options) ->
    maps:from_list([{P, true} || P <- maps:get(prefixes, Options, [$-])]).

% The progname option, else the emulator's progname; a Clause program has none and uses its own name.
executable(#{progname := Prog}) when is_atom(Prog) ->
    atom_to_list(Prog);
executable(#{progname := Prog}) when is_binary(Prog) ->
    binary_to_list(Prog);
executable(#{progname := Prog}) ->
    Prog;
executable(_) ->
    case init:get_argument(progname) of
        {ok, [[Prog]]} -> Prog;
        error -> filename:rootname(filename:basename(init:script_name()))
    end.

% The handler of the matched command: a fun or {Module, Function} given the argument map, or the positional forms
% given the arguments on the path in order (Default for the missing ones).
handle(Command, ArgMap, Path, #{handler := {Mod, ModFun, Default}}) ->
    erlang:apply(Mod, ModFun, positional(Command, Path, ArgMap, Default));
handle(_Command, ArgMap, _Path, #{handler := {Mod, ModFun}}) when is_atom(Mod), is_atom(ModFun) ->
    Mod:ModFun(ArgMap);
handle(Command, ArgMap, Path, #{handler := {Fun, Default}}) when is_function(Fun) ->
    erlang:apply(Fun, positional(Command, Path, ArgMap, Default));
handle(_Command, ArgMap, _Path, #{handler := Handler}) when is_function(Handler, 1) ->
    Handler(ArgMap).

positional(Command, Path, ArgMap, Default) ->
    [maps:get(Name, ArgMap, Default) || #{name := Name} <- path_arguments(Command, Path, [])].

% The arguments of every command from Command down Path.
path_arguments(Command, [], Acc) ->
    Acc ++ maps:get(arguments, Command, []);
path_arguments(Command, [Name | Tail], Acc) ->
    Next = maps:get(Name, maps:get(commands, Command, Name)),
    path_arguments(Next, Tail, Acc ++ maps:get(arguments, Command, [])).
