%% mnesia tables as qlc sources (plan 21 step MIM1E, docs/libraries/mnesia.md#query-tables): mnesia:table/1,2 hands
%% qlc a table read in chunks of mnesia:select/4 in the caller's activity, key lookups through mnesia:read/3 and
%% index lookups through mnesia:index_read/3, with OTP's options, information and printed form.
-module(clause_mnesia_qlc).
-export([table/2, fixtable/3]).

-include("clause_mnesia.hrl").

% The query handle: mnesia's options {traverse, _}, {lock, _} and {n_objects, _}, any other option is qlc's.
table(Tab, Opts) ->
    {[Traverse, Lock, NObjects], QlcOptions} = options(Opts, [{traverse, select}, {lock, read}, {n_objects, 100}]),
    Select =
        case Traverse of
            {select, Ms} -> fun() -> chunks(mnesia:select(Tab, Ms, NObjects, Lock)) end;
            select -> fun(Ms) -> chunks(mnesia:select(Tab, Ms, NObjects, Lock)) end;
            _ -> erlang:error({badarg, {Traverse, [Tab, Opts]}})
        end,
    Funs = [
        {pre_fun, fun(Args) -> pre(Args, Tab) end},
        {post_fun, fun() -> post(Tab) end},
        {info_fun, fun(Tag) -> info(Tab, Tag) end},
        {parent_fun, fun() -> {mnesia_activity, mnesia:get_activity_id()} end},
        {format_fun, fun(Kind) -> format(Kind, Tab, NObjects, Lock, Opts) end}
    ],
    qlc:table(Select, Funs ++ lookup(Traverse, Tab, Lock) ++ QlcOptions).

% Each of mnesia's options, its first value or the default, and the options left for qlc.
options(Opts, Defaults) when is_list(Opts) ->
    lists:mapfoldl(
        fun({Key, Default}, Rest) ->
            case lists:keyfind(Key, 1, Rest) of
                {Key, Value} -> {Value, lists:keydelete(Key, 1, Rest)};
                false -> {Default, Rest}
            end
        end,
        Opts,
        Defaults
    );
options(Opt, Defaults) ->
    options([Opt], Defaults).

chunks('$end_of_table') -> [];
chunks({[], Cont}) -> chunks(mnesia:select(Cont));
chunks({Records, Cont}) -> Records ++ fun() -> chunks(mnesia:select(Cont)) end.

% Keys of the key position are read, those of an indexed position looked up in the index; not with an explicit select.
lookup({select, _}, _, _) ->
    [];
lookup(_, Tab, Lock) ->
    Lookup = fun
        (2, Keys) -> lists:flatmap(fun(Key) -> mnesia:read(Tab, Key, Lock) end, Keys);
        (Pos, Keys) -> lists:flatmap(fun(Key) -> mnesia:index_read(Tab, Key, Pos) end, Keys)
    end,
    [{lookup_fun, Lookup}].

% Before a traversal: the caller's activity, or the activity of the process that made the cursor; a dirty traversal of
% a hash table fixes it.
pre(Args, Tab) ->
    Tid =
        case get(mnesia_activity_state) of
            undefined -> parent_activity(Args);
            {_, ActivityId, _} -> ActivityId
        end,
    element(1, Tid) =:= tid orelse fix(Tab, true),
    ok.

parent_activity(Args) ->
    case lists:keyfind(parent_value, 1, Args) of
        {parent_value, {mnesia_activity, {_, Tid, _} = Activity}} ->
            mnesia:put_activity_id(Activity),
            Tid;
        _ ->
            mnesia:abort(no_transaction)
    end.

post(Tab) ->
    case get(mnesia_activity_state) of
        {_, #tid{}, _} -> ok;
        _ -> fix(Tab, false)
    end.

fix(Tab, Fix) ->
    case ets:match(?GVAR, {{Tab, setorbag}, '$1'}) of
        [[ordered_set]] -> ok;
        _ -> mnesia:dirty_rpc(Tab, ?MODULE, fixtable, [Tab, Fix, self()])
    end.

% A table fixed for a traversal in the calling process, which a process exit releases.
fixtable(Tab, Fix, _Pid) ->
    case clause_mnesia_db:kind(Tab) of
        ets -> ets:safe_fixtable(Tab, Fix);
        dets -> dets:safe_fixtable(Tab, Fix)
    end,
    node().

% qlc's questions about the table; uniqueness and order are asked of a {Tab, type} variable mnesia does not keep, so
% they abort as OTP's do.
info(Tab, num_of_objects) -> mnesia:dirty_rpc(Tab, mnesia, raw_table_info, [Tab, size]);
info(_, keypos) -> 2;
info(_, is_unique_objects) -> true;
info(Tab, is_unique_keys) -> lists:member(clause_mnesia_monitor:val({Tab, type}), [set, ordered_set]);
info(Tab, is_sorted_objects) -> sorted(clause_mnesia_monitor:val({Tab, type}));
info(Tab, indices) -> clause_mnesia_monitor:val({Tab, index});
info(_, _) -> undefined.

sorted(ordered_set) -> ascending;
sorted(_) -> no.

% qlc:info/1's form of a traversal, an explicit select and the lookups.
format(all, Tab, NObjects, Lock, Opts) ->
    {mnesia, table, [Tab, [{n_objects, NObjects}, {lock, Lock} | Opts]]};
format({match_spec, Ms}, Tab, NObjects, Lock, Opts) ->
    {mnesia, table, [Tab, [{traverse, {select, Ms}}, {n_objects, NObjects}, {lock, Lock} | Opts]]};
format({lookup, 2, Keys}, Tab, _, Lock, _) ->
    io_lib:format("lists:flatmap(fun(V) -> ~w:read(~w, V, ~w) end, ~w)", [mnesia, Tab, Lock, Keys]);
format({lookup, Pos, Keys}, Tab, _, _, _) ->
    io_lib:format("lists:flatmap(fun(V) -> ~w:index_read(~w, V, ~w) end, ~w)", [mnesia, Tab, Pos, Keys]).
