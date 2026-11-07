%% Schema merges with other nodes' mnesia, as OTP's (plan 21 step MIM1H5,
%% docs/libraries/mnesia-protocol.md#schema-merge-mim1h5): change_config(extra_db_nodes, Nodes) and the start's
%% extra_db_nodes connect this node to the nodes that run mnesia, then merge its schema with theirs in schema
%% transactions that lock the schema on every node and commit the merged definitions there (clause_mnesia_commit),
%% again until nothing is left to merge. The controllers then tell each other which tables they hold.
-module(clause_mnesia_merge).
-export([connect_nodes/1, changes/1]).

-include("clause_mnesia.hrl").

-define(ALL, '______WHOLETABLE_____').

% The merge, from a process of its own: ok, or {aborted, Reason} when a merge fails. A schema transaction runs even
% when no node is left to merge, as in OTP.
connect_nodes(Nodes) ->
    {Pid, Ref} = spawn_monitor(fun() -> exit({merged, connect(Nodes)}) end),
    receive
        {'DOWN', Ref, process, Pid, {merged, Result}} -> Result;
        {'DOWN', Ref, process, Pid, Reason} -> {aborted, Reason}
    end.

connect(Nodes) ->
    gen_server:abcast([node() | Nodes], mnesia_controller, {merging_schema, node()}),
    _ = clause_mnesia_recover:connect_nodes(Nodes),
    Result = merge_loop(false),
    _ = gen_server:multi_call([node() | val(recover_nodes)], mnesia_controller, {schema_is_merged, [], late_merge, []}),
    Result.

% A merge, then another, until the schema transaction finds nothing to merge; each merge tells both sides' controllers
% that the other side runs, and after the last one this node's controller hears that it runs.
merge_loop(Merged) ->
    case mnesia:transaction(fun merge/0) of
        {atomic, {merged, Old, New}} ->
            gen_server:abcast(Old, mnesia_controller, {im_running, node(), New}),
            gen_server:abcast(New, mnesia_controller, {im_running, node(), Old}),
            merge_loop(true);
        {atomic, not_merged} ->
            Merged andalso gen_server:cast(mnesia_controller, {im_running, node(), [node()]}),
            ok;
        {aborted, _} = Aborted ->
            Aborted
    end.

% The schema transaction: the schema locked on this node, the running ones and those to merge; the first node to merge
% gives its definitions. The other nodes' locks go when it ends: participants release theirs.
merge() ->
    {_, Tid, _} = get(mnesia_activity_state),
    Running = val({current, db_nodes}),
    ToMerge = val(recover_nodes) -- Running,
    mnesia:lock({table, schema}, write),
    Locked = lock_remote(Tid, lists:delete(node(), Running ++ ToMerge), []),
    try merged(Tid, Running, ToMerge) of
        not_merged = Result ->
            [{mnesia_tm, N} ! {Tid, simple_commit} || N <- Locked],
            Result;
        {merged, _, RemoteRunning} = Result ->
            [{mnesia_tm, N} ! {Tid, simple_commit} || N <- Locked -- (Running ++ RemoteRunning)],
            Result
    catch
        Class:Reason:Stack ->
            [{mnesia_locker, N} ! {release_tid, Tid} || N <- Locked],
            erlang:raise(Class, Reason, Stack)
    end.

% The schema's write lock on each node, in OTP's raw form; a refusal releases those taken and aborts.
lock_remote(_, [], Locked) ->
    Locked;
lock_remote(Tid, [Node | Rest], Locked) ->
    case clause_mnesia_locker:remote_request(Node, {write, Tid, {schema, ?ALL}}) of
        granted ->
            lock_remote(Tid, Rest, [Node | Locked]);
        {not_granted, Reason} ->
            [{mnesia_locker, N} ! {release_tid, Tid} || N <- Locked],
            mnesia:abort(Reason)
    end.

merged(_, _, []) ->
    not_merged;
merged(Tid, Running, [Node | _]) ->
    case rpc:call(Node, mnesia_controller, get_remote_cstructs, []) of
        {cstructs, Cstructs, RemoteRunning} ->
            _ = clause_mnesia_recover:connect_nodes(RemoteRunning),
            Ops = ops(Node, Cstructs, Running, RemoteRunning),
            {ok, Changes} = changes(Ops),
            ok = clause_mnesia_commit:coordinate(Tid, Ops, Changes, lists:delete(node(), Running) ++ RemoteRunning),
            {merged, Running, RemoteRunning};
        Other ->
            mnesia:abort({merge_schema_failed, Node, Other})
    end.

% OTP's operations of a merge: the other side announced running, then each of its definitions merged with this
% node's, then the definitions only this node has.
ops(Node, Cstructs, Running, RemoteRunning) ->
    Remote = lists:keyfind(schema, #cstruct.name, Cstructs),
    Announce = {op, announce_im_running, node(), cs2list(Remote), Running, RemoteRunning},
    Local = [val({Tab, cstruct}) || Tab <- val({schema, tables}), not lists:keymember(Tab, #cstruct.name, Cstructs)],
    Merged = [merged_cstruct(Cs, Node) || Cs <- Cstructs] ++ Local,
    [Announce | [{op, merge_schema, cs2list(Cs)} || Cs <- Merged]].

% The schema's definition has the nodes of both and a new major version of this node's; another table known on both
% nodes must have the same cookie, or the merge fails with OTP's message.
merged_cstruct(#cstruct{name = schema} = Remote, _) ->
    Local = val({schema, cstruct}),
    {{Major, _}, _} = newer(Local, Remote),
    (nodes_of(Local, Remote))#cstruct{version = {{Major + 1, 0}, {node(), erlang:timestamp()}}};
merged_cstruct(#cstruct{name = Tab, cookie = Cookie} = Remote, Node) ->
    case ets:member(schema, Tab) of
        false ->
            Remote;
        true ->
            case val({Tab, cstruct}) of
                #cstruct{cookie = Cookie} = Local -> (nodes_of(Remote, Local))#cstruct{version = newer(Local, Remote)};
                Local -> throw(bad_cookie(Tab, Local, Node, Remote))
            end
    end.

% Cs with the nodes of both definitions, the other's first.
nodes_of(Cs, Other) ->
    Cs#cstruct{
        ram_copies = union(Other#cstruct.ram_copies, Cs#cstruct.ram_copies),
        disc_copies = union(Other#cstruct.disc_copies, Cs#cstruct.disc_copies),
        disc_only_copies = union(Other#cstruct.disc_only_copies, Cs#cstruct.disc_only_copies)
    }.

union(First, Second) -> First ++ (Second -- First).

newer(#cstruct{version = A}, #cstruct{version = B}) -> max(A, B).

bad_cookie(Tab, Local, Node, Remote) ->
    Format = "Bad cookie in table definition ~w: ~w = ~w, ~w = ~w~n",
    lists:flatten(io_lib:format(Format, [Tab, node(), Local, Node, Remote])).

% The changes a commit's operations make here: the merged definitions, then the nodes that run with this one; another
% operation is refused until MIM1H8.
changes(Ops) ->
    try [change(Op) || Op <- Ops] of
        Changes -> {ok, [C || {merge, _} = C <- Changes] ++ [C || {announce, _, _} = C <- Changes]}
    catch
        throw:Reason -> {error, Reason}
    end.

change({op, merge_schema, List}) -> {merge, clause_mnesia_schema:list2cs(List)};
change({op, announce_im_running, _, _, Running, RemoteRunning}) -> {announce, Running, RemoteRunning};
change(Op) -> throw({not_supported, Op}).

cs2list(Cs) -> clause_mnesia_schema:cs2list(Cs).

val(Name) -> clause_mnesia_monitor:val(Name).
