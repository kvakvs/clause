%% Clause's gen (plan 21 step LIB21, docs/library.md): what the generic behaviours share. Starting a named process
%% through proc_lib, calls and replies in OTP's message shapes ({Label, {Pid, Tag}, Request} with an alias tag
%% [alias | Ref], answered with {Tag, Reply}), request ids and their collections, stopping, names and debug options.
-module(gen).
-export([
    '@wait_response_recv_opt'/3,
    call/3,
    call/4,
    check_response/2,
    check_response/3,
    debug_options/2,
    format_status/4,
    format_status_header/2,
    get_parent/0,
    get_proc_name/1,
    hibernate_after/1,
    init_it/6,
    init_it/7,
    name/1,
    receive_response/2,
    receive_response/3,
    reply/2,
    reqids_add/3,
    reqids_new/0,
    reqids_size/1,
    reqids_to_list/1,
    send_request/3,
    send_request/5,
    start/5,
    start/6,
    stop/1,
    stop/3,
    unregister_name/1,
    wait_response/2,
    wait_response/3
]).
-include("gen.types.hrl").

-define(MAX_TIMEOUT, 4294967295).
-define(DEFAULT_TIMEOUT, 5000).

%% Starting.

start(GenMod, Linkage, Name, Mod, Args, Options) ->
    case where(Name) of
        undefined -> spawn_start(GenMod, Linkage, [Name], Mod, Args, Options);
        Pid -> {error, {already_started, Pid}}
    end.

start(GenMod, Linkage, Mod, Args, Options) ->
    spawn_start(GenMod, Linkage, [], Mod, Args, Options).

%% Starts the process in init_it/6,7 (with the name when there is one) as proc_lib start, start_link or
%% start_monitor.
spawn_start(GenMod, Linkage, Name, Mod, Args, Options) ->
    Parent =
        case Linkage of
            nolink -> self;
            _ -> self()
        end,
    InitArgs = [GenMod, self(), Parent] ++ Name ++ [Mod, Args, Options],
    Timeout = option(timeout, Options, infinity),
    SpawnOptions = option(spawn_opt, Options, []),
    case Linkage of
        link ->
            proc_lib:start_link(?MODULE, init_it, InitArgs, Timeout, SpawnOptions);
        monitor ->
            monitored(proc_lib:start_monitor(?MODULE, init_it, InitArgs, Timeout, SpawnOptions));
        _ ->
            proc_lib:start(?MODULE, init_it, InitArgs, Timeout, SpawnOptions)
    end.

%% A failed monitored start waits for the process to end.
monitored({{ok, Pid}, Ref}) when is_pid(Pid), is_reference(Ref) ->
    {ok, {Pid, Ref}};
monitored({Error, Ref}) when is_reference(Ref) ->
    receive
        {'DOWN', Ref, process, _, _} -> ok
    end,
    Error.

init_it(GenMod, Starter, Parent, Mod, Args, Options) ->
    GenMod:init_it(Starter, Parent, self(), Mod, Args, Options).

init_it(GenMod, Starter, Parent, Name, Mod, Args, Options) ->
    case register_name(Name) of
        true -> GenMod:init_it(Starter, Parent, Name, Mod, Args, Options);
        {false, Pid} -> proc_lib:init_fail(Starter, {error, {already_started, Pid}}, {exit, normal})
    end.

option(Key, Options, Default) ->
    case lists:keyfind(Key, 1, Options) of
        {_, Value} -> Value;
        false -> Default
    end.

hibernate_after(Options) -> option(hibernate_after, Options, infinity).

debug_options(Name, Options) ->
    case lists:keyfind(debug, 1, Options) of
        {_, DebugOptions} ->
            try
                sys:debug_options(DebugOptions)
            catch
                _:_ ->
                    error_logger:format("~tp: ignoring erroneous debug options - ~tp~n", [
                        Name, DebugOptions
                    ]),
                    []
            end;
        false ->
            []
    end.

%% Calls.

call(Process, Label, Request) -> call(Process, Label, Request, ?DEFAULT_TIMEOUT).

call(Process, Label, Request, Timeout) when
    is_pid(Process), Timeout =:= infinity;
    is_pid(Process), is_integer(Timeout), Timeout >= 0
->
    do_call(Process, Label, Request, Timeout);
call(Process, Label, Request, Timeout) when
    Timeout =:= infinity; is_integer(Timeout), Timeout >= 0
->
    for_process(Process, fun(Pid) -> do_call(Pid, Label, Request, Timeout) end).

do_call(Process, _, _, _) when Process =:= self() ->
    exit(calling_self);
do_call(Process, Label, Request, infinity) when is_pid(Process), node(Process) =:= node() ->
    %% Waiting without a timeout ends only with the reply or the 'DOWN', so a plain monitor tag will do.
    Ref = erlang:monitor(process, Process),
    Process ! {Label, {self(), Ref}, Request},
    receive
        {Ref, Reply} ->
            erlang:demonitor(Ref, [flush]),
            {ok, Reply};
        {'DOWN', Ref, _, _, Reason} ->
            exit(Reason)
    end;
do_call(Process, Label, Request, Timeout) ->
    %% The alias drops a reply that comes after the timeout.
    Ref = erlang:monitor(process, Process, [{alias, demonitor}]),
    Tag = [alias | Ref],
    erlang:send(Process, {Label, {self(), Tag}, Request}, [noconnect]),
    receive
        {[alias | Ref], Reply} ->
            erlang:demonitor(Ref, [flush]),
            {ok, Reply};
        {'DOWN', Ref, _, _, noconnection} ->
            exit({nodedown, node_of(Process)});
        {'DOWN', Ref, _, _, Reason} ->
            exit(Reason)
    after Timeout ->
        erlang:demonitor(Ref, [flush]),
        receive
            {[alias | Ref], Reply} -> {ok, Reply}
        after 0 -> exit(timeout)
        end
    end.

node_of({_, Node}) when is_atom(Node) -> Node;
node_of(Pid) when is_pid(Pid) -> node(Pid).

reply({_, [alias | Alias] = Tag}, Reply) when is_reference(Alias) ->
    Alias ! {Tag, Reply},
    ok;
reply({_, [[alias | Alias] | _] = Tag}, Reply) when is_reference(Alias) ->
    Alias ! {Tag, Reply},
    ok;
reply({To, Tag}, Reply) ->
    try
        To ! {Tag, Reply},
        ok
    catch
        _:_ -> ok
    end.

%% Request ids: a request is an alias monitor; its reply or 'DOWN' is collected later.

send_request(Process, Label, Request) when is_pid(Process) ->
    request(Process, Label, Request);
send_request(Process, Label, Request) ->
    try
        for_process(Process, fun(Pid) -> request(Pid, Label, Request) end)
    catch
        exit:Reason ->
            %% An unknown name fails later, as a 'DOWN' of a request that never went out.
            Ref = erlang:make_ref(),
            self() ! {'DOWN', Ref, process, Process, Reason},
            Ref
    end.

send_request(Process, Label, Request, RequestLabel, Collection) when is_map(Collection) ->
    maps:put(send_request(Process, Label, Request), RequestLabel, Collection).

request(Process, Label, Request) ->
    Ref = erlang:monitor(process, Process, [{alias, demonitor}]),
    _ = erlang:send(Process, {Label, {self(), [alias | Ref]}, Request}, [noconnect]),
    Ref.

wait_response(Ref, Timeout) ->
    Wait = timeout_value(Timeout),
    receive
        {[alias | Ref], Reply} ->
            erlang:demonitor(Ref, [flush]),
            {reply, Reply};
        {'DOWN', Ref, _, Object, Reason} ->
            {error, {Reason, Object}}
    after Wait -> timeout
    end.

wait_response(Collection, Timeout, Delete) when map_size(Collection) =:= 0, is_boolean(Delete) ->
    _ = timeout_value(Timeout),
    no_request;
wait_response(Collection, Timeout, Delete) when is_map(Collection), is_boolean(Delete) ->
    Wait = timeout_value(Timeout),
    receive
        {[alias | Ref], _} = Msg when is_map_key(Ref, Collection) ->
            collected(Msg, Collection, Delete);
        {'DOWN', Ref, _, _, _} = Msg when is_map_key(Ref, Collection) ->
            collected(Msg, Collection, Delete)
    after Wait -> timeout
    end.

%% As wait_response/2, abandoning the request at the timeout.
receive_response(Ref, Timeout) ->
    Wait = timeout_value(Timeout),
    receive
        {[alias | Ref], Reply} ->
            erlang:demonitor(Ref, [flush]),
            {reply, Reply};
        {'DOWN', Ref, _, Object, Reason} ->
            {error, {Reason, Object}}
    after Wait ->
        erlang:demonitor(Ref, [flush]),
        receive
            {[alias | Ref], Reply} -> {reply, Reply}
        after 0 -> timeout
        end
    end.

receive_response(Collection, Timeout, Delete) when map_size(Collection) =:= 0, is_boolean(Delete) ->
    _ = timeout_value(Timeout),
    no_request;
receive_response(Collection, Timeout, Delete) when is_map(Collection), is_boolean(Delete) ->
    Wait = timeout_value(Timeout),
    receive
        {[alias | Ref], _} = Msg when is_map_key(Ref, Collection) ->
            collected(Msg, Collection, Delete);
        {'DOWN', Ref, _, _, _} = Msg when is_map_key(Ref, Collection) ->
            collected(Msg, Collection, Delete)
    after Wait ->
        maps:foreach(fun abandon/2, Collection),
        drop_replies(Collection),
        timeout
    end.

abandon(Ref, _) when is_reference(Ref) -> erlang:demonitor(Ref, [flush]);
abandon(_, _) -> error(badarg).

drop_replies(Collection) ->
    receive
        {[alias | Ref], _} when is_map_key(Ref, Collection) -> drop_replies(Collection)
    after 0 -> ok
    end.

check_response(Msg, Ref) when is_reference(Ref) ->
    case Msg of
        {[alias | Ref], Reply} ->
            erlang:demonitor(Ref, [flush]),
            {reply, Reply};
        {'DOWN', Ref, _, Object, Reason} ->
            {error, {Reason, Object}};
        _ ->
            no_reply
    end;
check_response(_, _) ->
    error(badarg).

check_response(_, Collection, Delete) when map_size(Collection) =:= 0, is_boolean(Delete) ->
    no_request;
check_response(Msg, Collection, Delete) when is_map(Collection), is_boolean(Delete) ->
    case Msg of
        {[alias | Ref], _} when is_map_key(Ref, Collection) -> collected(Msg, Collection, Delete);
        {'DOWN', Ref, _, _, _} when is_map_key(Ref, Collection) ->
            collected(Msg, Collection, Delete);
        _ ->
            no_reply
    end.

%% {Response, Label, Collection} of a collected reply or 'DOWN', the request removed when Delete is set.
collected({[alias | Ref], Reply}, Collection, Delete) ->
    _ = erlang:demonitor(Ref, [flush]),
    labelled({reply, Reply}, Ref, Collection, Delete);
collected({'DOWN', Ref, _, Object, Reason}, Collection, Delete) ->
    labelled({error, {Reason, Object}}, Ref, Collection, Delete).

labelled(Response, Ref, Collection, false) ->
    {Response, maps:get(Ref, Collection), Collection};
labelled(Response, Ref, Collection, true) ->
    {Label, Rest} = maps:take(Ref, Collection),
    {Response, Label, Rest}.

%% A receive timeout from a relative or {abs, Time} timeout; badarg for anything else.
timeout_value(infinity) ->
    infinity;
timeout_value(Timeout) when is_integer(Timeout), 0 =< Timeout, Timeout =< ?MAX_TIMEOUT ->
    Timeout;
timeout_value({abs, Time}) when is_integer(Time) ->
    case Time - erlang:monotonic_time(millisecond) of
        Left when Left < 0 -> 0;
        Left when Left > ?MAX_TIMEOUT -> error(badarg);
        Left -> Left
    end;
timeout_value(_) ->
    error(badarg).

reqids_new() -> maps:new().

reqids_size(Collection) when is_map(Collection) -> maps:size(Collection);
reqids_size(_) -> error(badarg).

reqids_add(Ref, _, Collection) when is_reference(Ref), is_map_key(Ref, Collection) -> error(badarg);
reqids_add(Ref, Label, Collection) when is_reference(Ref), is_map(Collection) ->
    maps:put(Ref, Label, Collection);
reqids_add(_, _, _) ->
    error(badarg).

reqids_to_list(Collection) when is_map(Collection) -> maps:to_list(Collection);
reqids_to_list(_) -> error(badarg).

%% Created for LIB21: never called; OTP's compiler hint for receive optimizations.
'@wait_response_recv_opt'(Process, Label, Request) ->
    _ = wait_response(send_request(Process, Label, Request), infinity),
    _ = receive_response(send_request(Process, Label, Request), infinity),
    ok.

%% Stopping.

stop(Process) -> stop(Process, normal, infinity).

stop(Process, Reason, Timeout) when Timeout =:= infinity; is_integer(Timeout), Timeout >= 0 ->
    for_process(Process, fun(Pid) -> proc_lib:stop(Pid, Reason, Timeout) end).

%% Names.

%% Applies Fun to the pid a server reference names; noproc when none, nodedown for another node.
for_process(Pid, Fun) when is_pid(Pid) ->
    Fun(Pid);
for_process(Name, Fun) when is_atom(Name) ->
    case whereis(Name) of
        undefined -> exit(noproc);
        Pid -> Fun(Pid)
    end;
for_process({global, _} = Process, Fun) ->
    found(where(Process), Fun);
for_process({via, _, _} = Process, Fun) ->
    found(where(Process), Fun);
for_process({Name, Node}, Fun) when Node =:= node() ->
    for_process(Name, Fun);
for_process({_, Node} = Process, Fun) when is_atom(Node) ->
    case node() of
        nonode@nohost -> exit({nodedown, Node});
        _ -> Fun(Process)
    end.

found(undefined, _) -> exit(noproc);
found(Pid, Fun) -> Fun(Pid).

where({global, Name}) -> global:whereis_name(Name);
where({via, Module, Name}) -> Module:whereis_name(Name);
where({local, Name}) -> whereis(Name);
where(Name) -> error(badarg, [Name]).

register_name({local, Name} = Process) ->
    try
        register(Name, self())
    catch
        error:_ -> {false, where(Process)}
    end;
register_name({global, Name} = Process) ->
    registered(global:register_name(Name, self()), Process);
register_name({via, Module, Name} = Process) ->
    registered(Module:register_name(Name, self()), Process).

registered(yes, _) -> true;
registered(no, Process) -> {false, where(Process)}.

name({local, Name}) -> Name;
name({global, Name}) -> Name;
name({via, _, Name}) -> Name;
name(Pid) when is_pid(Pid) -> Pid.

unregister_name({local, Name}) ->
    try unregister(Name) of
        _ -> ok
    catch
        _:_ -> ok
    end;
unregister_name({global, Name}) ->
    _ = global:unregister_name(Name),
    ok;
unregister_name({via, Module, Name}) ->
    _ = Module:unregister_name(Name),
    ok;
unregister_name(Pid) when is_pid(Pid) ->
    ok.

%% The name an enter_loop process must already have.
get_proc_name(Pid) when is_pid(Pid) ->
    Pid;
get_proc_name({local, Name}) ->
    case process_info(self(), registered_name) of
        {registered_name, Name} -> Name;
        _ -> exit(process_not_registered)
    end;
get_proc_name({global, Name}) ->
    Self = self(),
    case global:whereis_name(Name) of
        Self -> Name;
        _ -> exit(process_not_registered_globally)
    end;
get_proc_name({via, Module, Name}) ->
    Self = self(),
    case Module:whereis_name(Name) of
        Self -> Name;
        _ -> exit({process_not_registered_via, Module})
    end.

get_parent() ->
    case get('$ancestors') of
        [Parent | _] when is_pid(Parent) -> Parent;
        [Parent | _] when is_atom(Parent) -> pid_of_name(Parent);
        _ -> exit(process_was_not_started_by_proc_lib)
    end.

pid_of_name(Name) ->
    case whereis(Name) of
        undefined ->
            case global:whereis_name(Name) of
                undefined -> exit(could_not_find_registered_name);
                Pid -> Pid
            end;
        Pid ->
            Pid
    end.

%% Status formatting.

format_status_header(Line, Pid) when is_pid(Pid) -> lists:concat([Line, " ", pid_to_list(Pid)]);
format_status_header(Line, Name) when is_atom(Name) -> lists:concat([Line, " ", Name]);
format_status_header(Line, Name) -> {Line, Name}.

%% Status after the callback module's format_status/1 (or the deprecated format_status/2, whose result lands in
%% '$status'); a failing callback leaves an 'EXIT' explanation.
format_status(Mod, Opt, Status, Args) ->
    case
        {
            erlang:function_exported(Mod, format_status, 1),
            erlang:function_exported(Mod, format_status, 2)
        }
    of
        {true, _} -> status_map(Mod, Status);
        {false, true} when is_list(Args) -> status_list(Mod, Opt, Status, Args);
        {false, _} -> Status
    end.

status_map(Mod, Status) ->
    try Mod:format_status(Status) of
        New when is_map(New) ->
            Merged = maps:merge(Status, New),
            case maps:size(Merged) =:= maps:size(New) of
                true ->
                    Merged;
                false ->
                    Status#{
                        'EXIT' => atom_to_list(Mod) ++
                            ":format_status/1 returned a map with unknown keys"
                    }
            end;
        _ ->
            Status#{'EXIT' => atom_to_list(Mod) ++ ":format_status/1 did not return a map"}
    catch
        _:_ -> Status#{'EXIT' => atom_to_list(Mod) ++ ":format_status/1 crashed"}
    end.

status_list(Mod, Opt, Status, Args) ->
    try Mod:format_status(Opt, Args) of
        Result -> Status#{'$status' => Result}
    catch
        throw:Result -> Status#{'$status' => Result};
        _:_ -> Status#{'EXIT' => atom_to_list(Mod) ++ ":format_status/2 crashed"}
    end.
