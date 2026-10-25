-module(c).
-export([
    appcall/4,
    bi/1,
    bt/1,
    c/1,
    c/2,
    c/3,
    cd/1,
    display_info/1,
    erlangrc/0,
    erlangrc/1,
    flush/0,
    h/1,
    h/2,
    h/3,
    h1/1,
    h1/2,
    h1/3,
    hcb/1,
    hcb/2,
    hcb/3,
    help/0,
    ht/1,
    ht/2,
    ht/3,
    i/0,
    i/1,
    i/3,
    l/1,
    lc/1,
    lc_batch/0,
    lc_batch/1,
    lm/0,
    ls/0,
    ls/1,
    m/0,
    m/1,
    memory/0,
    memory/1,
    mm/0,
    nc/1,
    nc/2,
    ni/0,
    nl/1,
    nregs/0,
    pi/1,
    pi/3,
    pid/3,
    pwd/0,
    q/0,
    regs/0,
    uptime/0,
    xm/1,
    y/1,
    y/2
]).

% The shell's command line interface module (plan 21 step LIB74, docs/library.md#interactive-shell), written from
% OTP's documentation. A Clause program has no compiler, tools or parsetools: c/1,2,3, lc/1, nc/1,2, xm/1 and
% y/1,2 report the failed call as OTP does when the application is missing, and loading answers come from code.

-define(NATIVE_FORMAT, <<"application/erlang+html">>).

%% Print the commands of this module.
help() ->
    io:put_chars(<<
        "bt(Pid)    -- stack backtrace for a process\n"
        "c(Mod)     -- compile and load module or file <Mod>\n"
        "cd(Dir)    -- change working directory\n"
        "flush()    -- flush any messages sent to the shell\n"
        "h(M)       -- module documentation\n"
        "h(M,F)     -- module function documentation\n"
        "h(M,F,A)   -- module function arity documentation\n"
        "hcb(Mod)   -- help about a module's callbacks\n"
        "hcb(Mod,CB) -- help about callback in module\n"
        "hcb(Mod,CB,Arity) -- help about callback with arity in module\n"
        "ht(Mod)    -- help about a module's types\n"
        "ht(Mod,Type) -- help about type in module\n"
        "ht(Mod,Type,Arity) -- help about type with arity in module\n"
        "help()     -- help info\n"
        "i()        -- information about the system\n"
        "i(X,Y,Z)   -- deprecated alias for pi(X,Y,Z)\n"
        "l(Module)  -- load or reload module\n"
        "lc([File]) -- compile a list of Erlang modules\n"
        "lm()       -- load all modified modules\n"
        "ls()       -- list files in the current directory\n"
        "ls(Dir)    -- list files in directory <Dir>\n"
        "m()        -- which modules are loaded\n"
        "m(Mod)     -- information about module <Mod>\n"
        "memory()   -- memory allocation information\n"
        "memory(T)  -- memory allocation information of type <T>\n"
        "mm()       -- list all modified modules\n"
        "nc(File)   -- compile and load code in <File> on all nodes\n"
        "ni()       -- information about the networked system\n"
        "nl(Module) -- load module on all nodes\n"
        "nregs()    -- information about all registered processes\n"
        "pi(Pid)    -- information about process <Pid>\n"
        "pi(X,Y,Z)  -- information about pid <X,Y,Z>\n"
        "pid(X,Y,Z) -- convert X,Y,Z to a Pid\n"
        "pwd()      -- print working directory\n"
        "q()        -- quit - shorthand for init:stop()\n"
        "regs()     -- information about registered processes\n"
        "uptime()   -- print node uptime\n"
        "xm(M)      -- cross reference check a module\n"
        "y(File)    -- generate a Yecc parser\n"
    >>).

%% --- Compiling and loading ---

c(Module) -> c(Module, []).

%% Compile and load a module or source file with options.
c(Module, Option) when not is_list(Option) ->
    c(Module, [Option]);
c(Module, Options) when is_atom(Module) ->
    Suffix =
        case filename:extension(Module) of
            "" -> source_suffix(Options);
            S -> S
        end,
    Source = filename:rootname(Module, Suffix) ++ Suffix,
    case filelib:is_file(Source) of
        true -> compile_and_load(Source, Options);
        false -> c(Module, Options, fun(_) -> true end)
    end;
c(Module, Options) ->
    compile_and_load(Module, Options).

%% Recompile a module from the source its object file names, keeping the original options Filter accepts.
c(Module, Options, Filter) when is_atom(Module) ->
    case find_beam(Module) of
        Beam when is_list(Beam) -> recompile(Module, Options, Filter, Beam);
        Error -> {error, Error}
    end.

recompile(Module, Options, Filter, Beam) ->
    case compile_info(Module, Beam) of
        Info when is_list(Info) ->
            case find_source(Beam, Info) of
                Source when is_list(Source) ->
                    Keep = fun(Opt) -> not is_outdir_option(Opt) andalso Filter(Opt) end,
                    Old = proplists:get_value(options, Info, []),
                    All = Options ++ [{outdir, filename:dirname(Beam)}] ++ lists:filter(Keep, Old),
                    io:format("Recompiling ~ts\n", [Source]),
                    compile_and_load(Source, All);
                Error ->
                    Error
            end;
        Error ->
            Error
    end.

find_beam(Module) ->
    case code:which(Module) of
        Beam when is_list(Beam), Beam =/= "" ->
            case not erlang:module_loaded(Module) orelse filelib:is_file(Beam) of
                true -> Beam;
                false -> beam_on_path(Module)
            end;
        Other when Other =:= ""; Other =:= cover_compiled ->
            beam_on_path(Module);
        Error ->
            Error
    end.

beam_on_path(Module) ->
    code:where_is_file(atom_to_list(Module) ++ code:objfile_extension()).

compile_info(Module, Beam) ->
    case erlang:module_loaded(Module) of
        true ->
            try erlang:get_module_info(Module, compile) of
                Info when is_list(Info) -> Info;
                _ -> []
            catch
                _:_ -> []
            end;
        false ->
            case beam_lib:chunks(Beam, [compile_info]) of
                {ok, {_, [{compile_info, Info}]}} -> Info;
                Error -> Error
            end
    end.

find_source(Beam, Info) ->
    case proplists:get_value(source, Info) of
        Source when is_list(Source) ->
            case filelib:is_file(Source) of
                true -> Source;
                false -> source_near(Beam)
            end;
        _ ->
            source_near(Beam)
    end.

source_near(Beam) ->
    case filelib:find_source(Beam) of
        {ok, Source} -> Source;
        _ -> {error, no_source}
    end.

compile_and_load(File, Options0) when is_list(Options0) ->
    Options = [report_errors, report_warnings | ensure_outdir(Options0)],
    case appcall(compiler, compile, file, [File, Options]) of
        {ok, Mod} -> load_compiled(Mod, File, Options);
        {ok, Mod, _} -> load_compiled(Mod, File, Options);
        Other -> Other
    end;
compile_and_load(File, Option) ->
    compile_and_load(File, [Option]).

ensure_outdir(Options) ->
    {[Outdir | _], Rest} = lists:partition(fun is_outdir_option/1, Options ++ [{outdir, "."}]),
    [Outdir | Rest].

is_outdir_option({outdir, _}) -> true;
is_outdir_option(_) -> false.

source_suffix([from_core | _]) -> ".core";
source_suffix([from_asm | _]) -> ".S";
source_suffix([_ | Options]) -> source_suffix(Options);
source_suffix([]) -> ".erl".

load_compiled(Mod, File, Options) ->
    {outdir, Dir} = lists:keyfind(outdir, 1, Options),
    Base = filename:basename(File, source_suffix(Options)),
    case atom_to_list(Mod) of
        Base ->
            code:purge(Mod),
            case code:load_abs(filename:join(Dir, Base), Mod) of
                {error, _} = Error -> Error;
                _ -> {ok, Mod}
            end;
        _ ->
            io:format("** Module name '~p' does not match file name '~tp' **~n", [Mod, File]),
            {error, badfile}
    end.

%% Compile files with the options @i Dir, @o Dir and @d Name[=Term] among them.
lc(Args) ->
    case catch split(Args, [], []) of
        error ->
            error;
        {Options, Files} ->
            All = [report_errors, report_warnings | lists:reverse(Options)],
            lists:foreach(fun(File) -> appcall(compiler, compile, file, [File, All]) end, lists:reverse(Files))
    end.

lc_batch() ->
    io:format("Error: no files to compile~n"),
    halt(1).

lc_batch(Args) ->
    try split(Args, [], []) of
        {Options, Files} ->
            All = [report_errors, report_warnings | lists:reverse(Options)],
            Results = [appcall(compiler, compile, file, [File, All]) || File <- lists:reverse(Files)],
            halt(
                case lists:member(error, Results) of
                    true -> 1;
                    false -> 0
                end
            )
    catch
        throw:error -> halt(1)
    end.

split(['@i', Dir | T], Options, Files) -> split(T, [{i, atom_to_list(Dir)} | Options], Files);
split(['@o', Dir | T], Options, Files) -> split(T, [{outdir, atom_to_list(Dir)} | Options], Files);
split(['@d', Def | T], Options, Files) -> split(T, [define(atom_to_list(Def), []) | Options], Files);
split([File | T], Options, Files) -> split(T, Options, [File | Files]);
split([], Options, Files) -> {Options, Files}.

define([$= | T], Name) -> {d, list_to_atom(lists:reverse(Name)), make_term(T)};
define([H | T], Name) -> define(T, [H | Name]);
define([], Name) -> {d, list_to_atom(lists:reverse(Name))}.

make_term(Text) ->
    case erl_scan:string(Text) of
        {ok, Tokens, _} ->
            case erl_parse:parse_term(Tokens ++ [{dot, erl_anno:new(1)}]) of
                {ok, Term} -> Term;
                {error, {_, _, Reason}} -> bad_term(Reason, Text)
            end;
        {error, {_, _, Reason}, _} ->
            bad_term(Reason, Text)
    end.

bad_term(Reason, Text) ->
    io:format("~ts: ~ts~n", [Reason, Text]),
    throw(error).

%% Compile a file and load it on every node.
nc(File) -> nc(File, []).

nc(File, Options0) when is_list(Options0) ->
    Options = Options0 ++ [report_errors, report_warnings],
    case appcall(compiler, compile, file, [File, Options]) of
        {ok, Mod} ->
            Dir =
                case lists:keyfind(outdir, 1, Options) of
                    {outdir, D} -> D;
                    false -> "."
                end,
            Object = filename:join(Dir, filename:basename(File, ".erl") ++ code:objfile_extension()),
            case file:read_file(Object) of
                {ok, Bin} ->
                    rpc:eval_everywhere(code, load_binary, [Mod, Object, Bin]),
                    {ok, Mod};
                Other ->
                    Other
            end;
        Other ->
            Other
    end;
nc(File, Option) when is_atom(Option) ->
    nc(File, [Option]).

%% Purge and load a module.
l(Mod) ->
    code:purge(Mod),
    code:load_file(Mod).

%% Load a module on every node.
nl(Mod) ->
    case code:get_object_code(Mod) of
        {_, Bin, File} -> rpc:eval_everywhere(code, load_binary, [Mod, File, Bin]);
        Other -> Other
    end.

%% The modules whose object files changed since they were loaded, and loading them.
mm() -> code:modified_modules().

lm() -> [l(M) || M <- mm()].

%% Cross reference check a module (tools) and generate a parser (parsetools).
xm(ModSpec) -> appcall(tools, xref, m, [ModSpec]).

y(File) -> y(File, []).

y(File, Options) -> appcall(parsetools, yecc, file, [File, Options]).

%% Call M:F(Args) of another application; a missing module is reported as OTP does.
appcall(App, M, F, Args) ->
    try
        apply(M, F, Args)
    catch
        error:undef:S ->
            case S of
                [{M, F, Args, _} | _] ->
                    io:format("Call to ~w:~w/~w in application ~w failed.\n", [M, F, length(Args), App]);
                Stack ->
                    erlang:raise(error, undef, Stack)
            end
    end.

%% --- Documentation ---

%% Print a module's, function's or callback's documentation; linked Clause modules have none.
h(Module) -> docs(Module, fun(Docs) -> format_docs(shell_docs:render(Module, Docs)) end).
h(Module, Function) -> docs(Module, fun(Docs) -> format_docs(shell_docs:render(Module, Function, Docs)) end).
h(Module, Function, Arity) ->
    docs(Module, fun(Docs) -> format_docs(shell_docs:render(Module, Function, Arity, Docs)) end).

h1(Module) -> docs(Module, fun(Docs) -> shell_docs:render(Module, Docs) end).
h1(Module, Function) -> docs(Module, fun(Docs) -> shell_docs:render(Module, Function, Docs) end).
h1(Module, Function, Arity) -> docs(Module, fun(Docs) -> shell_docs:render(Module, Function, Arity, Docs) end).

ht(Module) -> docs(Module, fun(Docs) -> format_docs(shell_docs:render_type(Module, Docs)) end).
ht(Module, Type) -> docs(Module, fun(Docs) -> format_docs(shell_docs:render_type(Module, Type, Docs)) end).
ht(Module, Type, Arity) ->
    docs(Module, fun(Docs) -> format_docs(shell_docs:render_type(Module, Type, Arity, Docs)) end).

hcb(Module) -> docs(Module, fun(Docs) -> format_docs(shell_docs:render_callback(Module, Docs)) end).
hcb(Module, Callback) -> docs(Module, fun(Docs) -> format_docs(shell_docs:render_callback(Module, Callback, Docs)) end).
hcb(Module, Callback, Arity) ->
    docs(Module, fun(Docs) -> format_docs(shell_docs:render_callback(Module, Callback, Arity, Docs)) end).

% Render a module's documentation chunk when its format is renderable.
docs(Module, Render) ->
    case code:get_doc(Module) of
        {ok, {docs_v1, _, _, Format, _, _, _} = Docs} when is_binary(Format) ->
            case Format =:= ?NATIVE_FORMAT orelse binary:part(Format, 0, min(5, byte_size(Format))) =:= <<"text/">> of
                true -> Render(Docs);
                false -> {error, {unknown_format, Format}}
            end;
        {ok, {docs_v1, _, _, Format, _, _, _}} ->
            {error, {unknown_format, Format}};
        Error ->
            Error
    end.

format_docs({error, _} = E) ->
    E;
format_docs(Docs) ->
    {match, Lines} = re:run(Docs, "(.+\n|\n)", [unicode, global, {capture, all_but_first, binary}]),
    _ = paged_output(
        fun(Line, _) ->
            io:format("~ts", Line),
            {1, undefined}
        end,
        undefined,
        Lines
    ),
    ok.

%% --- Processes ---

%% Information about the processes of the node, with totals.
i() -> i(processes()).

ni() -> i(all_processes()).

i(Ps) ->
    iformat("Pid", "Initial Call", "Heap", "Reds", "Msgs"),
    iformat("Registered", "Current Function", "Stack", "", ""),
    Print = fun(Pid, {R, M, H, S}) ->
        {A, B, C, D} = display_info(Pid),
        {2, {R + A, M + B, H + C, S + D}}
    end,
    case paged_output(Print, {0, 0, 0, 0}, Ps) of
        {R, M, H, S} ->
            iformat("Total", "", w(H), w(R), w(M)),
            iformat("", "", w(S), "", "");
        less ->
            ok
    end.

% Print items while the terminal has rows, then ask whether to go on.
paged_output(Print, Acc, Items) ->
    Limit =
        case io:rows() of
            {ok, Rows} -> Rows - 2;
            _ -> 100
        end,
    paged_output(Print, 0, Limit, Acc, Items).

paged_output(Print, Line, Limit, Acc, Items) when Line >= Limit ->
    case more() of
        more -> paged_output(Print, 0, Limit, Acc, Items);
        less -> less
    end;
paged_output(Print, Line, Limit, Acc, [H | T]) ->
    {Lines, NewAcc} = Print(H, Acc),
    paged_output(Print, Line + Lines, Limit, NewAcc, T);
paged_output(_, _, _, Acc, []) ->
    Acc.

more() ->
    case get_line('more (y/n)? (y) ', "y\n") of
        "c\n" -> more;
        "y\n" -> more;
        "q\n" -> less;
        "n\n" -> less;
        _ -> more()
    end.

get_line(Prompt, Default) ->
    case io:get_line(Prompt) of
        "\n" -> Default;
        Line when is_binary(Line) -> unicode:characters_to_list(Line);
        Line -> Line
    end.

%% Print two lines about a process; {Reductions, Messages, HeapSize, StackSize} for the totals. Clause keeps no
%% current function and no initial call but proc_lib's.
display_info(Pid) ->
    case process_info(Pid, [reductions, message_queue_len, heap_size, stack_size, registered_name, dictionary]) of
        undefined ->
            {0, 0, 0, 0};
        Info ->
            Reds = fetch(reductions, Info),
            LM = fetch(message_queue_len, Info),
            HS = fetch(heap_size, Info),
            SS = fetch(stack_size, Info),
            iformat(w(Pid), mfa_string(initial_call(Info)), w(HS), w(Reds), w(LM)),
            iformat(label(fetch(registered_name, Info), Info), mfa_string(undefined), w(SS), "", ""),
            {Reds, LM, HS, SS}
    end.

label([], Info) ->
    case lists:keyfind('$process_label', 1, dictionary(Info)) of
        {_, Label} -> format_label(Label);
        false -> ""
    end;
label(Name, _) ->
    Name.

format_label(Label) when is_list(Label); is_binary(Label) ->
    try unicode:characters_to_binary(Label) of
        {error, _, _} -> io_lib:format("~0.tp", [Label]);
        Text -> Text
    catch
        _:_ -> io_lib:format("~0.tp", [Label])
    end;
format_label(Label) ->
    io_lib:format("~0.tp", [Label]).

% proc_lib's initial call, the one process_info reports for its processes.
initial_call(Info) ->
    case lists:keyfind('$initial_call', 1, dictionary(Info)) of
        {_, Call} -> Call;
        false -> undefined
    end.

dictionary(Info) ->
    case lists:keyfind(dictionary, 1, Info) of
        {_, Dictionary} -> Dictionary;
        false -> []
    end.

mfa_string({M, F, A}) -> io_lib:format("~w:~tw/~w", [M, F, A]);
mfa_string(X) -> w(X).

iformat(A1, A2, A3, A4, A5) ->
    io:format("~-21ts ~-33ts ~8s ~8s ~4s~n", [A1, A2, A3, A4, A5]).

all_processes() ->
    case is_alive() of
        true -> lists:flatmap(fun(N) -> rpc:call(N, erlang, processes, []) end, [node() | nodes()]);
        false -> processes()
    end.

fetch(Key, Info) ->
    case lists:keyfind(Key, 1, Info) of
        {_, Value} -> Value;
        false -> 0
    end.

%% The pid <X.Y.Z>.
pid(X, Y, Z) ->
    list_to_pid("<" ++ integer_to_list(X) ++ "." ++ integer_to_list(Y) ++ "." ++ integer_to_list(Z) ++ ">").

i(X, Y, Z) -> pi(X, Y, Z).

pi(X, Y, Z) -> pi(pid(X, Y, Z)).

%% process_info/1 of a process.
pi(Pid) ->
    case is_alive() of
        true -> rpc:call(node(Pid), erlang, process_info, [Pid]);
        false -> process_info(Pid)
    end.

%% Stop the node.
q() -> init:stop().

%% Print a process's backtrace; undefined when there is none.
bt(Pid) ->
    try erlang:process_display(Pid, backtrace) of
        _ -> ok
    catch
        _:_ -> undefined
    end.

%% The registered processes and ports of this node, or of every node.
regs() -> print_node_regs({node(), registered()}).

nregs() -> lists:foreach(fun print_node_regs/1, all_regs()).

all_regs() ->
    case is_alive() of
        true -> [{N, rpc:call(N, erlang, registered, [])} || N <- [node() | nodes()]];
        false -> [{node(), registered()}]
    end.

print_node_regs({Node, Names}) when is_list(Names) ->
    {Pids, Ports} = registered_items(lists:sort(Names), [], []),
    io:format("~n** Registered procs on node ~w **~n", [Node]),
    procformat("Name", "Pid", "Initial Call", "Reds", "Msgs"),
    lists:foreach(fun({Name, Info, Pid}) -> procline(Name, Info, Pid) end, Pids),
    io:format("~n** Registered ports on node ~w **~n", [Node]),
    portformat("Name", "Id", "Command"),
    lists:foreach(fun({Name, Info, Id}) -> portline(Name, Info, Id) end, Ports).

registered_items([], Pids, Ports) ->
    {lists:reverse(Pids), lists:reverse(Ports)};
registered_items([Name | Names], Pids, Ports) ->
    case whereis(Name) of
        Pid when is_pid(Pid) -> registered_items(Names, [{Name, process_info(Pid), Pid} | Pids], Ports);
        Id when is_port(Id) -> registered_items(Names, Pids, [{Name, [erlang:port_info(Id, name)], Id} | Ports]);
        undefined -> registered_items(Names, Pids, Ports)
    end.

procline(Name, Info0, Pid) ->
    Info =
        case Info0 of
            undefined -> [];
            _ -> Info0
        end,
    procformat(
        io_lib:format("~tw", [Name]),
        io_lib:format("~w", [Pid]),
        io_lib:format("~ts", [mfa_string(initial_call(Info))]),
        integer_to_list(fetch(reductions, Info)),
        integer_to_list(fetch(message_queue_len, Info))
    ).

procformat(Name, Pid, Call, Reds, LM) ->
    io:format("~-21ts ~-12s ~-25ts ~12s ~4s~n", [Name, Pid, Call, Reds, LM]).

portline(Name, Info, Id) ->
    Command =
        case lists:keyfind(name, 1, [I || I <- Info, is_tuple(I)]) of
            {name, C} -> C;
            false -> 0
        end,
    portformat(io_lib:format("~tw", [Name]), erlang:port_to_list(Id), Command).

portformat(Name, Id, Command) ->
    io:format("~-21ts ~-15s ~-40ts~n", [Name, Id, Command]).

%% Print every message in the caller's mailbox.
flush() ->
    receive
        X ->
            case lists:keyfind(encoding, 1, io:getopts()) of
                {encoding, unicode} -> io:format("Shell got ~tp~n", [X]);
                _ -> io:format("Shell got ~p~n", [X])
            end,
            flush()
    after 0 -> ok
    end.

%% --- Modules ---

%% The loaded modules with their files.
m() ->
    mformat("Module", "File"),
    lists:foreach(fun({Mod, File}) -> mformat(Mod, File) end, lists:sort(code:all_loaded())).

mformat(A1, A2) ->
    io:format("~-20s  ~ts\n", [A1, A2]).

%% Information about a module: its md5, object file, compiler options and exports.
m(M) ->
    L = M:module_info(),
    {exports, E} = lists:keyfind(exports, 1, L),
    Options =
        case lists:keyfind(compile, 1, L) of
            {compile, I} -> proplists:get_value(options, I, []);
            false -> []
        end,
    io:format("Module: ~w~n", [M]),
    case lists:keyfind(md5, 1, L) of
        {md5, <<MD5:128>>} -> io:format("MD5: ~.16b~n", [MD5]);
        _ -> ok
    end,
    case code:is_loaded(M) of
        {file, File} -> io:format("Object file: ~ts\n", [File]);
        _ -> ignore
    end,
    io:format("Compiler options:  ~p~n", [Options]),
    io:format("Exports: ~n", []),
    print_exports(lists:keysort(1, E)).

% Exports in one column, or two columns for more than 16.
print_exports(X) when length(X) > 16 ->
    Mid = length(X) div 2,
    {L1, L2} = lists:split(Mid, X),
    print_columns(L1, L2);
print_exports(X) ->
    lists:foreach(fun({F, A}) -> io:format("         ~tw/~w~n", [F, A]) end, X).

print_columns([], [{F, A} | T]) ->
    io:format("~-30ts~tw/~w~n", [" ", F, A]),
    print_columns([], T);
print_columns([{F1, A1} | T1], [{F2, A2} | T2]) ->
    io:format("~-30ts~tw/~w~n", [lists:flatten(io_lib:format("~tw/~w", [F1, A1])), F2, A2]),
    print_columns(T1, T2);
print_columns([], []) ->
    ok.

%% --- System ---

%% Print a system_info item.
bi(I) ->
    case erlang:system_info(I) of
        X when is_binary(X) -> io:put_chars(binary_to_list(X));
        X when is_list(X) -> io:put_chars(X);
        X -> io:format("~w", [X])
    end.

%% Run the .erlang file of the home or user configuration directory, or of the given directories.
erlangrc() ->
    case init:get_argument(home) of
        {ok, [[Home]]} -> erlangrc([Home, filename:basedir(user_config, "erlang")]);
        _ -> {error, enoent}
    end.

erlangrc([Home | _] = Paths) when is_list(Home) ->
    case file:path_eval(Paths, ".erlang") of
        {error, enoent} = Enoent ->
            Enoent;
        {error, {Line, _, _} = E} ->
            error_logger:error_msg("file:path_eval(~tp,~tp): error on line ~p: ~ts~n", [
                Paths, ".erlang", Line, file:format_error(E)
            ]),
            {error, E};
        {error, E} ->
            error_logger:error_msg("file:path_eval(~tp,~tp): ~ts~n", [Paths, ".erlang", file:format_error(E)]),
            {error, E};
        Other ->
            Other
    end.

%% erlang:memory/0,1.
memory() -> erlang:memory().

memory(TypeSpec) -> erlang:memory(TypeSpec).

%% Print how long the node has run.
uptime() ->
    {UpTime, _} = erlang:statistics(wall_clock),
    {D, {H, M, S}} = calendar:seconds_to_daystime(UpTime div 1000),
    io:format("~s~n", [
        lists:flatten([
            [io_lib:format("~p days, ", [D]) || D > 0],
            [io_lib:format("~p hours, ", [H]) || D + H > 0],
            [io_lib:format("~p minutes and ", [M]) || D + H + M > 0],
            io_lib:format("~p seconds", [S])
        ])
    ]).

%% --- Files ---

%% Print the working directory.
pwd() ->
    case file:get_cwd() of
        {ok, Dir} -> ok = io:format("~ts\n", [Dir]);
        {error, _} -> ok = io:format("Cannot determine current directory\n")
    end.

%% Change the working directory and print it.
cd(Dir) ->
    _ = file:set_cwd(Dir),
    pwd().

%% List the files of a directory in columns.
ls() -> ls(".").

ls(Dir0) ->
    case file:list_dir(Dir0) of
        {ok, Entries} ->
            ls_print(lists:sort(Entries));
        {error, enotdir} ->
            ls_print([
                if
                    is_list(Dir0) -> lists:flatten(Dir0);
                    true -> Dir0
                end
            ]);
        {error, Error} ->
            io:format("~ts\n", [file:format_error(Error)])
    end.

ls_print([]) ->
    ok;
ls_print(L) ->
    Width = min(max_length(L, 0), 40) + 5,
    ls_print(L, Width, 0).

ls_print(X, Width, Len) when Width + Len >= 80 ->
    io:nl(),
    ls_print(X, Width, 0);
ls_print([H | T], Width, Len) ->
    io:format("~-*ts", [Width, H]),
    ls_print(T, Width, Len + Width);
ls_print([], _, _) ->
    io:nl().

max_length([H | T], L) when is_atom(H) -> max_length([atom_to_list(H) | T], L);
max_length([H | T], L) -> max_length(T, max(length(H), L));
max_length([], L) -> L.

w(X) -> io_lib:write(X).
