%% mnesia's access module for fragmented tables (plan 21 step MIM1G, docs/libraries/mnesia.md#fragmented-tables), as
%% activity/4's access module: each access of a fragmented table goes to the fragment its key hashes to, and an access
%% without a key to every fragment. Fragment N of table Tab is the table Tab_fragN, the first is Tab itself. The base
%% table's frag_properties hold the hash module and state; mnesia_gvar keeps them as {Tab, frag_hash}, OTP's
%% #frag_state{}. Also the schema side: the fragments of a new table and the changes of change_table_frag/2.
-module(mnesia_frag).
-export([lock/4, write/5, delete/5, delete_object/5, read/5, match_object/5, all_keys/4]).
-export([select/5, select/6, select_cont/3, select_reverse/5, select_reverse/6]).
-export([index_match_object/6, index_read/6, foldl/6, foldr/6, table_info/4, first/3, next/4, prev/4, last/3]).
-export([clear_table/4]).
-export([change_table_frag/2, remove_node/2, expand_cstruct/1, lookup_frag_hash/1, lookup_foreigners/1]).
-export([frag_names/1, set_frag_hash/2, local_select/4, remote_select/4]).

-include("clause_mnesia.hrl").

%% A fragmented table's hashing, as OTP keeps it in mnesia_gvar: the foreign key ({Tab, Pos}) or undefined, the number
%% of fragments, and the hash module and its state.
-record(frag_state, {foreign_key, n_fragments, hash_module, hash_state}).

-define(DEFAULT_HASH, mnesia_frag_hash).

%% Access: each function reaches the fragment of the key, or every fragment.

lock(Aid, Opaque, {table, Tab}, Kind) ->
    case frag_names(Tab) of
        [Tab] -> mnesia:lock(Aid, Opaque, {table, Tab}, Kind);
        Frags -> lists:usort(lists:append([mnesia:lock(Aid, Opaque, {table, F}, Kind) || F <- Frags]))
    end;
lock(Aid, Opaque, Item, Kind) ->
    mnesia:lock(Aid, Opaque, Item, Kind).

write(Aid, Opaque, Tab, Rec, Kind) ->
    mnesia:write(Aid, Opaque, record_to_frag_name(Tab, Rec), Rec, Kind).

delete(Aid, Opaque, Tab, Key, Kind) ->
    mnesia:delete(Aid, Opaque, key_to_frag_name(Tab, Key), Key, Kind).

delete_object(Aid, Opaque, Tab, Rec, Kind) ->
    mnesia:delete_object(Aid, Opaque, record_to_frag_name(Tab, Rec), Rec, Kind).

read(Aid, Opaque, Tab, Key, Kind) ->
    mnesia:read(Aid, Opaque, key_to_frag_name(Tab, Key), Key, Kind).

match_object(Aid, Opaque, Tab, Pattern, Kind) ->
    select(Aid, Opaque, Tab, [{Pattern, [], ['$_']}], Kind).

select(Aid, Opaque, Tab, Spec, Kind) ->
    whole_select(Aid, Opaque, Tab, Spec, Kind).

select(Aid, Opaque, Tab, Spec, Limit, Kind) ->
    chunked_select(Aid, Opaque, Tab, Spec, Limit, Kind).

select_reverse(Aid, Opaque, Tab, Spec, Kind) ->
    select(Aid, Opaque, Tab, Spec, Kind).

select_reverse(Aid, Opaque, Tab, Spec, Limit, Kind) ->
    select(Aid, Opaque, Tab, Spec, Limit, Kind).

all_keys(Aid, Opaque, Tab, Kind) ->
    lists:append([mnesia:all_keys(Aid, Opaque, F, Kind) || F <- frag_names(Tab)]).

clear_table(Aid, Opaque, Tab, Pattern) ->
    [mnesia:clear_table(Aid, Opaque, F, Pattern) || F <- frag_names(Tab)],
    ok.

index_match_object(Aid, Opaque, Tab, Pattern, Attr, Kind) ->
    lists:append([mnesia:index_match_object(Aid, Opaque, F, Pattern, Attr, Kind) || F <- frag_names(Tab)]).

index_read(Aid, Opaque, Tab, Key, Attr, Kind) ->
    lists:append([mnesia:index_read(Aid, Opaque, F, Key, Attr, Kind) || F <- frag_names(Tab)]).

foldl(Aid, Opaque, Fun, Acc, Tab, Kind) ->
    lists:foldl(fun(F, A) -> mnesia:foldl(Aid, Opaque, Fun, A, F, Kind) end, Acc, frag_names(Tab)).

foldr(Aid, Opaque, Fun, Acc, Tab, Kind) ->
    lists:foldr(fun(F, A) -> mnesia:foldr(Aid, Opaque, Fun, A, F, Kind) end, Acc, frag_names(Tab)).

%% Walks: the fragments in turn, the first fragment first.

first(Aid, Opaque, Tab) ->
    case frag_hash(Tab) of
        none -> mnesia:first(Aid, Opaque, Tab);
        FH -> first_from(Aid, Opaque, Tab, 1, FH)
    end.

first_from(Aid, Opaque, Tab, N, #frag_state{n_fragments = Last} = FH) ->
    case mnesia:first(Aid, Opaque, n_to_frag_name(Tab, N)) of
        '$end_of_table' when N < Last -> first_from(Aid, Opaque, Tab, N + 1, FH);
        Key -> Key
    end.

last(Aid, Opaque, Tab) ->
    case frag_hash(Tab) of
        none -> mnesia:last(Aid, Opaque, Tab);
        #frag_state{n_fragments = Last} -> last_from(Aid, Opaque, Tab, Last)
    end.

last_from(Aid, Opaque, Tab, N) ->
    case mnesia:last(Aid, Opaque, n_to_frag_name(Tab, N)) of
        '$end_of_table' when N > 1 -> last_from(Aid, Opaque, Tab, N - 1);
        Key -> Key
    end.

next(Aid, Opaque, Tab, Key) ->
    case frag_hash(Tab) of
        none ->
            mnesia:next(Aid, Opaque, Tab, Key);
        #frag_state{n_fragments = Last} = FH ->
            N = key_to_n(FH, Key),
            case mnesia:next(Aid, Opaque, n_to_frag_name(Tab, N), Key) of
                '$end_of_table' when N < Last -> first_from(Aid, Opaque, Tab, N + 1, FH);
                Next -> Next
            end
    end.

prev(Aid, Opaque, Tab, Key) ->
    case frag_hash(Tab) of
        none ->
            mnesia:prev(Aid, Opaque, Tab, Key);
        FH ->
            N = key_to_n(FH, Key),
            case mnesia:prev(Aid, Opaque, n_to_frag_name(Tab, N), Key) of
                '$end_of_table' when N > 1 -> last_from(Aid, Opaque, Tab, N - 1);
                Prev -> Prev
            end
    end.

%% Selects: the fragments the specification can match, each locked.

whole_select(Aid, Opaque, Tab, Spec, Kind) ->
    case frag_hash(Tab) of
        none ->
            mnesia:select(Aid, Opaque, Tab, Spec, Kind);
        FH ->
            Names = [lock_frag(Aid, Opaque, Tab, N, Kind) || N <- frag_numbers(FH, Spec)],
            All = fun(Patched) -> lists:append([mnesia:dirty_select(Name, Patched) || Name <- Names]) end,
            mnesia:fun_select(Aid, Opaque, Tab, Spec, none, '_', All)
    end.

lock_frag(Aid, Opaque, Tab, N, Kind) ->
    Name = n_to_frag_name(Tab, N),
    mnesia:lock(Aid, Opaque, {table, Name}, Kind),
    Name.

% A select in chunks: each fragment's chunks in turn, as {frag_cont, Cont, Fragments, Args} continuations.
chunked_select(Aid, Opaque, Tab, Spec, Limit, Kind) ->
    case frag_hash(Tab) of
        none ->
            mnesia:select(Aid, Opaque, Tab, Spec, Limit, Kind);
        FH ->
            Places = [place(Aid, Opaque, Tab, N, Kind) || N <- frag_numbers(FH, Spec)],
            [{Frag, Node, Storage} | Rest] = Places,
            frag_cont(frag_select(Aid, Opaque, Frag, Node, Storage, {Spec, Kind, Limit}), Rest, {Spec, Kind, Limit})
    end.

place(Aid, Opaque, Tab, N, Kind) ->
    Name = lock_frag(Aid, Opaque, Tab, N, Kind),
    Node = clause_mnesia_monitor:val({Name, where_to_read}),
    {Name, Node, storage_at(Node, Name)}.

storage_at(Node, Tab) when Node =:= node() -> clause_mnesia_monitor:val({Tab, storage_type});
storage_at(_, _) -> unknown.

frag_select(Aid, Opaque, Frag, Node, Storage, {Spec, Kind, Limit}) ->
    Init = fun(Fixed) -> mnesia:dirty_sel_init(Node, Frag, Fixed, Limit, Storage) end,
    mnesia:fun_select(Aid, Opaque, Frag, Spec, Kind, Frag, Init, Limit, Node, Storage).

select_cont(_, _, {frag_cont, '$end_of_table', [], _}) ->
    '$end_of_table';
select_cont(Aid, Opaque, {frag_cont, '$end_of_table', [{Frag, Node, Storage} | Rest], Args}) ->
    frag_cont(frag_select(Aid, Opaque, Frag, Node, Storage, Args), Rest, Args);
select_cont(Aid, Opaque, {frag_cont, Cont, Rest, Args}) ->
    frag_cont(mnesia:select_cont(Aid, Opaque, Cont), Rest, Args);
select_cont(Aid, Opaque, Cont) ->
    mnesia:select_cont(Aid, Opaque, Cont).

frag_cont('$end_of_table', [], _) -> '$end_of_table';
frag_cont('$end_of_table', Rest, Args) -> {[], {frag_cont, '$end_of_table', Rest, Args}};
frag_cont({Records, Cont}, Rest, Args) -> {Records, {frag_cont, Cont, Rest, Args}}.

% The hash module's fragment numbers for a specification, each one of the table's.
frag_numbers(#frag_state{hash_module = Module, hash_state = State, n_fragments = N}, Spec) ->
    Numbers = Module:match_spec_to_frag_numbers(State, Spec),
    case [F || F <- Numbers, not (is_integer(F) andalso F >= 1 andalso F =< N)] of
        [] -> Numbers;
        Bad -> mnesia:abort({"match_spec_to_frag_numbers: Fragment numbers out of range", Bad, {range, 1, N}})
    end.

% A select of other nodes' fragments (MIM1H); one node has none.
local_select(ReplyTo, Ref, _RemoteNameNodes, _Spec) ->
    ReplyTo ! {local_select, Ref, ok},
    ok.

remote_select(ReplyTo, Ref, NameNodes, Spec) ->
    [
        ReplyTo ! {remote_select, Ref, Node, {ok, mnesia:dirty_select(Name, Spec)}}
     || {Name, Node} <- NameNodes, Node =:= node()
    ],
    ok.

%% Information: a fragmented table's own items, any other item of the key's fragment.

table_info(Aid, Opaque, {Tab, Key}, Item) ->
    info(Aid, Opaque, Tab, key_to_frag_name(Tab, Key), Item);
table_info(Aid, Opaque, Tab, Item) ->
    info(Aid, Opaque, Tab, Tab, Item).

info(Aid, Opaque, Tab, _, size) -> lists:sum([S || {_, S} <- frag_info(Aid, Opaque, Tab, size)]);
info(Aid, Opaque, Tab, _, memory) -> lists:sum([M || {_, M} <- frag_info(Aid, Opaque, Tab, memory)]);
info(_, _, Tab, _, base_table) -> lookup_prop(Tab, base_table);
info(_, _, Tab, _, node_pool) -> lookup_prop(Tab, node_pool);
info(_, _, Tab, _, n_fragments) -> (lookup_frag_hash(Tab))#frag_state.n_fragments;
info(_, _, Tab, _, foreign_key) -> (lookup_frag_hash(Tab))#frag_state.foreign_key;
info(_, _, Tab, _, foreigners) -> lookup_foreigners(Tab);
info(_, _, Tab, _, n_ram_copies) -> length(val({Tab, ram_copies}));
info(_, _, Tab, _, n_disc_copies) -> length(val({Tab, disc_copies}));
info(_, _, Tab, _, n_disc_only_copies) -> length(val({Tab, disc_only_copies}));
info(_, _, Tab, _, n_external_copies) -> length(val({Tab, external_copies}));
info(_, _, Tab, _, frag_names) -> frag_names(Tab);
info(_, _, Tab, _, frag_dist) -> frag_dist(Tab);
info(Aid, Opaque, Tab, _, frag_size) -> frag_info(Aid, Opaque, Tab, size);
info(Aid, Opaque, Tab, _, frag_memory) -> frag_info(Aid, Opaque, Tab, memory);
info(Aid, Opaque, _, Frag, Item) -> mnesia:table_info(Aid, Opaque, Frag, Item).

frag_info(Aid, Opaque, Tab, Item) ->
    [{F, frag_item(Aid, Opaque, F, Item)} || F <- frag_names(Tab)].

% A fragment's size or memory, asked of the node it is read from when that is another node, as OTP's (MIM1H12).
frag_item(Aid, Opaque, Frag, Item) ->
    case clause_mnesia_monitor:val({Frag, where_to_read}) of
        nowhere -> mnesia:abort({no_exists, Frag, Item});
        Node when Node =:= node() -> mnesia:table_info(Aid, Opaque, Frag, Item);
        Node -> remote_item(rpc:call(Node, mnesia, table_info, [Frag, Item]), Frag, Item)
    end.

remote_item({badrpc, _}, Frag, Item) -> mnesia:abort({no_exists, Frag, Item});
remote_item(Value, _, _) -> Value.

% Each pool node's replicas of the table's fragments, the fewest first; nodes outside the pool last.
frag_dist(Tab) ->
    Pool = lookup_prop(Tab, node_pool),
    Replicas = lists:append([frag_nodes(F) || F <- frag_names(Tab)]),
    Counts = [{N, count(N, Replicas)} || N <- Pool],
    Outside = first_seen([N || N <- Replicas, not lists:member(N, Pool)]),
    by_count(Counts) ++ [{N, count(N, Replicas)} || N <- Outside].

frag_nodes(Frag) ->
    lists:append([val({Frag, Item}) || Item <- [ram_copies, disc_copies, disc_only_copies, external_copies]]).

count(Node, Nodes) -> length([N || N <- Nodes, N =:= Node]).

first_seen([N | Rest]) -> [N | first_seen([M || M <- Rest, M =/= N])];
first_seen([]) -> [].

% Nodes in groups of equal count, the groups in increasing count, each group in pool order.
by_count(Counts) ->
    lists:append([[C || {_, K} = C <- Counts, K =:= Count] || Count <- lists:usort([K || {_, K} <- Counts])]).

%% Fragment names and hashing.

% A table's fragments: [Tab] for a table that is not fragmented.
frag_names(Tab) ->
    case frag_hash(Tab) of
        none -> [Tab];
        #frag_state{n_fragments = N} -> [n_to_frag_name(Tab, I) || I <- lists:seq(1, N)]
    end.

n_to_frag_name(Tab, 1) ->
    Tab;
n_to_frag_name(Tab, N) when is_atom(Tab), is_integer(N) ->
    list_to_atom(atom_to_list(Tab) ++ "_frag" ++ integer_to_list(N));
n_to_frag_name(Tab, N) ->
    mnesia:abort({bad_type, Tab, N}).

record_to_frag_name(Tab, Rec) ->
    case frag_hash(Tab) of
        none -> Tab;
        FH -> n_to_frag_name(Tab, key_to_n(FH, element(key_pos(FH), Rec)))
    end.

key_pos(#frag_state{foreign_key = undefined}) -> 2;
key_pos(#frag_state{foreign_key = {_, Pos}}) -> Pos.

% The fragment of a key; {Tab, ForeignKey} names the fragment of a foreign key.
key_to_frag_name({Tab, ForeignKey}, _) ->
    case clause_mnesia_monitor:val({Tab, frag_hash}) of
        #frag_state{foreign_key = {_, _}} = FH -> n_to_frag_name(Tab, key_to_n(FH, ForeignKey));
        _ -> mnesia:abort({combine_error, Tab, frag_properties, {foreign_key, undefined}})
    end;
key_to_frag_name(Tab, Key) ->
    case frag_hash(Tab) of
        none -> Tab;
        FH -> n_to_frag_name(Tab, key_to_n(FH, Key))
    end.

key_to_n(#frag_state{hash_module = Module, hash_state = State, n_fragments = Last}, Key) ->
    case Module:key_to_frag_number(State, Key) of
        N when is_integer(N), N >= 1, N =< Last -> N;
        N -> mnesia:abort({"key_to_frag_number: Fragment number out of range", N, {range, 1, Last}})
    end.

frag_hash(Tab) ->
    case ets:lookup(?GVAR, {Tab, frag_hash}) of
        [{_, FH}] -> FH;
        [] -> none
    end.

lookup_frag_hash(Tab) ->
    case frag_hash(Tab) of
        #frag_state{} = FH -> FH;
        none -> mnesia:abort({no_exists, Tab, frag_properties, frag_hash})
    end.

% The tables whose foreign key names this one.
lookup_foreigners(Tab) ->
    [T || [T] <- ets:match(?GVAR, {{'$1', frag_hash}, {'_', {Tab, '_'}, '_', '_', '_'}})].

lookup_prop(Tab, Prop) ->
    Props = val({Tab, frag_properties}),
    case lists:keyfind(Prop, 1, Props) of
        {Prop, Value} -> Value;
        false -> mnesia:abort({no_exists, Tab, Prop, {frag_properties, Props}})
    end.

% A base table's {Tab, frag_hash} row from its frag_properties; none for another table.
set_frag_hash(Tab, Props) ->
    case lists:keyfind(base_table, 1, Props) =:= {base_table, Tab} andalso lists:keyfind(hash_module, 1, Props) of
        {hash_module, Module} ->
            Pick = fun(Key) -> element(2, lists:keyfind(Key, 1, Props)) end,
            FH = #frag_state{
                foreign_key = Pick(foreign_key),
                n_fragments = Pick(n_fragments),
                hash_module = Module,
                hash_state = Pick(hash_state)
            },
            ets:insert(?GVAR, {{Tab, frag_hash}, FH});
        _ ->
            ets:delete(?GVAR, {Tab, frag_hash})
    end.

val(Name) -> clause_mnesia_monitor:val(Name).

%% Schema: the fragments of a new or activated table.

% A table definition with frag_properties as the definitions of its fragments, the last first and the base table
% last.
expand_cstruct(Cs) ->
    expand(Cs, create).

expand(#cstruct{name = Tab, frag_properties = Props} = Cs, Mode) ->
    is_list(Props) orelse mnesia:abort({badarg, Tab, Props}),
    check_keys(Tab, Props),
    {ForeignKey, N, Pool, Defaults} = pick_props(Tab, Cs, pick(Tab, foreign_key, Props, undefined)),
    is_list(Pool) andalso lists:all(fun erlang:is_atom/1, Pool) orelse mnesia:abort({bad_type, Tab, {node_pool, Pool}}),
    Copies = [copies(Tab, K, Props) || K <- [n_ram_copies, n_disc_copies, n_disc_only_copies, n_external_copies]],
    Cs2 = fragments_of(Cs, N, Mode),
    Module = pick(Tab, hash_module, Props, ?DEFAULT_HASH),
    State = init_state(Module, Tab, pick(Tab, hash_state, Props, undefined)),
    FH = #frag_state{foreign_key = ForeignKey, n_fragments = 1, hash_module = Module, hash_state = State},
    Counts =
        case lists:sum(Copies) of
            0 -> Defaults;
            _ -> Copies
        end,
    Cs2#cstruct.local_content =:= false orelse mnesia:abort({combine_error, Tab, {local_content, true}}),
    Cs2#cstruct.snmp =:= [] orelse mnesia:abort({combine_error, Tab, {snmp, Cs2#cstruct.snmp}}),
    expand_frags(N, Counts, Cs2#cstruct{frag_properties = [{base_table, Tab}]}, Pool, Pool, FH, Mode).

check_keys(Tab, Props) ->
    Valid = [
        foreign_key,
        n_fragments,
        node_pool,
        n_ram_copies,
        n_disc_copies,
        n_disc_only_copies,
        n_external_copies,
        hash_module,
        hash_state
    ],
    Keys = keys(Tab, Props, Valid),
    length(lists:usort(Keys)) =:= length(Keys) orelse mnesia:abort({badarg, Tab, "Duplicate keys", Keys}).

% Each property's key, a known one; the rest of the list from the first that is not a pair, as OTP reports it.
keys(Tab, [{Key, _} | Rest], Valid) ->
    lists:member(Key, Valid) orelse mnesia:abort({badarg, Tab, Key}),
    [Key | keys(Tab, Rest, Valid)];
keys(_, [], _) ->
    [];
keys(Tab, Rest, _) ->
    mnesia:abort({badarg, Tab, Rest}).

% A property's value, as OTP's mnesia_schema:pick/4 gives it.
pick(Tab, Key, Props, Default) ->
    case lists:keyfind(Key, 1, Props) of
        {Key, Value} -> Value;
        false -> Default;
        Bad -> mnesia:abort({bad_type, Tab, Bad})
    end.

copies(Tab, Key, Props) ->
    case pick(Tab, Key, Props, 0) of
        I when is_integer(I), I >= 0 -> I;
        Bad -> mnesia:abort({bad_type, Tab, {Key, Bad}})
    end.

% {ForeignKey, Fragments, Pool, DefaultCopies}: a foreign key takes its foreign table's fragments, pool and copies.
pick_props(Tab, #cstruct{frag_properties = Props, attributes = Attrs}, {ForeignTab, Attr} = Foreign) ->
    ForeignTab =/= Tab orelse mnesia:abort({combine_error, Tab, Foreign}),
    ForeignN = lookup_prop(ForeignTab, n_fragments),
    ForeignPool = lookup_prop(ForeignTab, node_pool),
    N = pick(Tab, n_fragments, Props, ForeignN),
    Pool = pick(Tab, node_pool, Props, ForeignPool),
    N =:= ForeignN orelse mnesia:abort({combine_error, Tab, {n_fragments, N}, ForeignTab, {n_fragments, ForeignN}}),
    Pool =:= ForeignPool orelse
        mnesia:abort({combine_error, Tab, {node_pool, Pool}, ForeignTab, {node_pool, ForeignPool}}),
    case lookup_prop(ForeignTab, foreign_key) of
        undefined -> ok;
        Other -> mnesia:abort({combine_error, Tab, "Multiple levels of foreign_key dependencies", Foreign, Other})
    end,
    Pos = clause_mnesia_schema:attribute_position(Attr, Attrs),
    Defaults = [length(val({ForeignTab, K})) || K <- [ram_copies, disc_copies, disc_only_copies, external_copies]],
    {{ForeignTab, Pos}, N, Pool, Defaults};
pick_props(Tab, #cstruct{frag_properties = Props}, undefined) ->
    N = pick(Tab, n_fragments, Props, 1),
    Pool = pick(Tab, node_pool, Props, mnesia:system_info(db_nodes)),
    {undefined, N, Pool, [1, 0, 0, 0]};
pick_props(Tab, _, Bad) ->
    mnesia:abort({bad_type, Tab, {foreign_key, Bad}}).

% A new table's fragments get their nodes from the pool; an activated table must be its only fragment.
fragments_of(#cstruct{name = Tab} = Cs, N, Mode) when is_integer(N), N >= 1 ->
    case Mode of
        create -> Cs#cstruct{ram_copies = [], disc_copies = [], disc_only_copies = [], external_copies = []};
        activate when N =:= 1 -> Cs;
        activate -> mnesia:abort({combine_error, Tab, {n_fragments, N}})
    end;
fragments_of(#cstruct{name = Tab}, N, _) ->
    mnesia:abort({bad_type, Tab, {n_fragments, N}}).

expand_frags(N, Counts, Common, Dist, Pool, FH, create) when N > 1 ->
    {Cs, Used, Rest} = place_copies(Counts, Common#cstruct{name = n_to_frag_name(Common#cstruct.name, N)}, Dist),
    {FH2, _, _} = split_state(FH),
    [Cs | expand_frags(N - 1, Counts, Common, redistribute(Cs, lists:reverse(Used), Rest, Pool), Pool, FH2, create)];
expand_frags(1, Counts, #cstruct{name = Tab} = Common, Dist, Pool, FH, Mode) ->
    Base = Common#cstruct{frag_properties = lists:sort(base_props(Tab, FH, Pool))},
    case Mode of
        activate ->
            [Base];
        create ->
            {Base2, _, _} = place_copies(Counts, Base, Dist),
            [Base2]
    end.

base_props(Tab, #frag_state{foreign_key = Foreign, hash_module = Module, hash_state = State, n_fragments = N}, Pool) ->
    [
        {base_table, Tab},
        {foreign_key, Foreign},
        {hash_module, Module},
        {hash_state, State},
        {n_fragments, N},
        {node_pool, Pool}
    ].

% A fragment's replicas on the first nodes of the distribution: {Cs, UsedCounts, RestOfDistribution}.
place_copies(Counts, Cs, Dist) ->
    Fields = [#cstruct.ram_copies, #cstruct.disc_copies, #cstruct.disc_only_copies, #cstruct.external_copies],
    Wanted = lists:append([lists:duplicate(C, F) || {C, F} <- lists:zip(Counts, Fields)]),
    place(Wanted, Cs, Dist, []).

place([], Cs, Rest, Used) ->
    {Cs, Used, Rest};
place(_, Cs, [], _) ->
    mnesia:abort({combine_error, Cs#cstruct.name, "Too few nodes in node_pool"});
place([Field | Wanted], Cs, [Head | Dist], Used) ->
    {Node, Count} =
        case Head of
            {N, C} when is_atom(N), is_integer(C), C >= 0 -> {N, C + 1};
            N when is_atom(N) -> {N, 1};
            Bad -> mnesia:abort({bad_type, Cs#cstruct.name, Bad})
        end,
    lists:member(Node, val({current, db_nodes})) orelse mnesia:abort({not_active, Cs#cstruct.name, Node}),
    place(Wanted, setelement(Field, Cs, [Node | element(Field, Cs)]), Dist, [{Node, Count} | Used]).

% The used nodes put back into the distribution, which stays sorted by count, then by pool order.
redistribute(Cs, Used, Dist, Pool) ->
    lists:foldl(fun({Node, Count}, D) -> insert_dist(Cs, Node, Count, D, Pool) end, Dist, Used).

insert_dist(_, Node, Count, [], _) ->
    [{Node, Count}];
insert_dist(Cs, Node, Count, [{Node2, Count2} = Head | Tail], Pool) when is_atom(Node2), is_integer(Count2) ->
    case before(Count, pool_pos(Node, Pool), Count2, pool_pos(Node2, Pool)) of
        true -> [{Node, Count}, Head | Tail];
        false -> [Head | insert_dist(Cs, Node, Count, Tail, Pool)]
    end;
insert_dist(Cs, Node, Count, [Node2 | Tail], Pool) when is_atom(Node2) ->
    insert_dist(Cs, Node, Count, [{Node2, 0} | Tail], Pool);
insert_dist(Cs, _, _, [Bad | _], _) ->
    mnesia:abort({bad_type, Cs#cstruct.name, Bad}).

before(Count, _, Count2, _) when Count < Count2 -> true;
before(Count, Pos, Count, Pos2) -> Pos < Pos2;
before(_, _, _, _) -> false.

pool_pos(Node, Pool) -> length(lists:takewhile(fun(N) -> N =/= Node end, Pool)) + 1.

%% Schema: change_table_frag/2's changes, as clause_mnesia_schema applies them, each base table first.

% The changes of each table: the table's own and those of its foreigners.
change_table_frag(Tab, {activate, Props}) ->
    activate(Tab, Props);
change_table_frag(Tab, deactivate) ->
    deactivate(Tab);
change_table_frag(Tab, {add_frag, Nodes}) when is_list(Nodes) ->
    multi(Tab, fun(T) -> add_frag(T, Nodes) end);
change_table_frag(Tab, {add_frag, Nodes}) ->
    mnesia:abort({bad_type, Tab, Nodes});
change_table_frag(Tab, del_frag) ->
    multi(Tab, fun del_frag/1);
change_table_frag(Tab, {add_node, Node}) ->
    multi(Tab, fun(T) -> add_node(T, Node) end);
change_table_frag(Tab, {del_node, Node}) ->
    multi(Tab, fun(T) -> del_node(T, Node) end);
change_table_frag(Tab, Change) ->
    mnesia:abort({bad_type, Tab, Change}).

multi(Tab, Change) ->
    case (lookup_frag_hash(Tab))#frag_state.foreign_key of
        undefined -> ok;
        Foreign -> mnesia:abort({combine_error, Tab, "Op only allowed via foreign table", {foreign_key, Foreign}})
    end,
    Changes = Change(Tab),
    Changes ++ lists:append([Change(T) || T <- lookup_foreigners(Tab)]).

activate(Tab, Props) ->
    Cs = changed(Tab),
    case Cs#cstruct.frag_properties of
        [] ->
            [Cs2] = expand(Cs#cstruct{frag_properties = Props}, activate),
            [{redefine, Cs2}];
        Old ->
            mnesia:abort({already_exists, Tab, {frag_properties, Old}})
    end.

deactivate(Tab) ->
    Cs = changed(Tab),
    Foreigners = lookup_foreigners(Tab),
    BaseTab = lookup_prop(Tab, base_table),
    FH = lookup_frag_hash(Tab),
    if
        BaseTab =/= Tab -> mnesia:abort({combine_error, Tab, "Not a base table"});
        Foreigners =/= [] -> mnesia:abort({combine_error, Tab, "Too many foreigners", Foreigners});
        FH#frag_state.n_fragments > 1 -> mnesia:abort({combine_error, Tab, "Too many fragments"});
        true -> [{redefine, Cs#cstruct{frag_properties = []}}]
    end.

% A new last fragment, with the records of the fragment it splits from that now hash to it.
add_frag(Tab, Nodes) ->
    Cs = changed(Tab),
    {FH2, From, Locked} = split_state(lookup_frag_hash(Tab)),
    N = FH2#frag_state.n_fragments,
    lock_frags(Tab, [I || I <- Locked, I =/= N]),
    New = Cs#cstruct{
        name = n_to_frag_name(Tab, N),
        frag_properties = [{base_table, Tab}],
        ram_copies = [],
        disc_copies = [],
        disc_only_copies = [],
        external_copies = []
    },
    Counts = [length(Cs#cstruct.ram_copies), length(Cs#cstruct.disc_copies), length(Cs#cstruct.disc_only_copies), 0],
    {New2, _, _} = place_copies(Counts, New, Nodes),
    clause_mnesia_change:check_new_table(New2),
    Moves = lists:append([moves(Tab, FH2, I, N) || I <- From]),
    [{redefine, with_hash(Cs, FH2)}, {create, New2}, {records, Moves}].

% The last fragment goes, its records into the fragments they now hash to.
del_frag(Tab) ->
    case lookup_frag_hash(Tab) of
        #frag_state{n_fragments = N} = FH when N > 1 ->
            Cs = changed(Tab),
            {FH2, From, Locked} = merge_state(FH),
            lock_frags(Tab, Locked),
            Moves = lists:append([moves(Tab, FH2, I, N) || I <- From]),
            Last = n_to_frag_name(Tab, N),
            clause_mnesia_change:check_deleted_table(Last),
            [{redefine, with_hash(Cs, FH2)}, {records, Moves}, {delete, Last}];
        _ ->
            mnesia:abort({no_exists, Tab})
    end.

add_node(Tab, Node) when is_atom(Node) ->
    Pool = lookup_prop(Tab, node_pool),
    lists:member(Node, Pool) andalso mnesia:abort({already_exists, Tab, Node}),
    Cs = next_version(val({Tab, cstruct})),
    [{redefine, with_pool(Cs, Pool ++ [Node])}];
add_node(Tab, Node) ->
    mnesia:abort({bad_type, Tab, Node}).

del_node(Tab, Node) when is_atom(Node) ->
    Cs = changed(Tab),
    Pool = lookup_prop(Tab, node_pool),
    lists:member(Node, Pool) orelse mnesia:abort({no_exists, Tab, Node}),
    [{redefine, with_pool(Cs, Pool -- [Node])}];
del_node(Tab, Node) ->
    mnesia:abort({bad_type, Tab, Node}).

% The hash module's first state. The default module is called by name, so that a program's build takes it along with
% mnesia_frag (MIM1H2).
init_state(mnesia_frag_hash, Tab, State) -> mnesia_frag_hash:init_state(Tab, State);
init_state(Module, Tab, State) -> Module:init_state(Tab, State).

% mnesia:del_table_copy(schema, Node): the node leaves a fragmented table's pool.
remove_node(Node, #cstruct{name = Tab} = Cs) ->
    case frag_hash(Tab) =/= none andalso lists:member(Node, lookup_prop(Tab, node_pool)) of
        true -> {with_pool(Cs, lookup_prop(Tab, node_pool) -- [Node]), true};
        false -> {Cs, false}
    end.

with_pool(#cstruct{frag_properties = Props} = Cs, Pool) ->
    Cs#cstruct{frag_properties = lists:keyreplace(node_pool, 1, Props, {node_pool, Pool})}.

with_hash(#cstruct{frag_properties = Props} = Cs, #frag_state{n_fragments = N, hash_module = M, hash_state = S}) ->
    New = [{n_fragments, N}, {hash_module, M}, {hash_state, S}],
    Kept = [P || {K, _} = P <- Props, not lists:keymember(K, 1, New), K =/= next_n_to_split, K =/= n_doubles],
    Cs#cstruct{frag_properties = lists:sort(Kept ++ [P || {K, _} = P <- New, lists:keymember(K, 1, Props)])}.

changed(Tab) ->
    Cs = next_version(val({Tab, cstruct})),
    clause_mnesia_change:check_active(Cs),
    Cs.

next_version(#cstruct{version = {{Major, _}, _}} = Cs) ->
    Cs#cstruct{version = {{Major + 1, 0}, {node(), erlang:timestamp()}}}.

lock_frags(Tab, Numbers) ->
    clause_mnesia_change:lock_table(write, Tab),
    [clause_mnesia_change:lock_table(write, n_to_frag_name(Tab, I)) || I <- Numbers],
    ok.

% The hash state with one fragment more: {State, FragmentsToSplit, FragmentsToLock}.
split_state(#frag_state{hash_module = Module, hash_state = State, n_fragments = N} = FH) ->
    {State2, From, More} = Module:add_frag(State),
    checked_state(FH#frag_state{n_fragments = N + 1, hash_state = State2}, From, More, N + 1, "add_frag").

merge_state(#frag_state{hash_module = Module, hash_state = State, n_fragments = N} = FH) ->
    {State2, From, More} = Module:del_frag(State),
    {FH2, From2, Locked} = checked_state(
        FH#frag_state{n_fragments = N - 1, hash_state = State2}, From, More, N, "del_frag"
    ),
    lists:member(N, From2) orelse mnesia:abort({"del_frag: Last fragment number not included", N}),
    {FH2, From2, Locked}.

checked_state(FH, From, More, N, Op) ->
    Sorted = lists:sort(From),
    Locked = lists:merge(Sorted, lists:sort(More)),
    case [F || F <- Locked, not (is_integer(F) andalso F >= 1 andalso F =< N)] of
        [] -> {FH, Sorted, Locked};
        Bad -> mnesia:abort({Op ++ ": Fragment numbers out of range", Bad, {range, 1, N}})
    end.

% A fragment's records that hash elsewhere now: a write to their new fragment and, unless the fragment goes, a delete.
moves(Tab, FH, I, Last) ->
    Frag = n_to_frag_name(Tab, I),
    Records = frag_records(Frag),
    Pos = key_pos(FH),
    lists:append([move(Tab, Frag, I, key_to_n(FH, element(Pos, R)), R, Last) || R <- Records]).

% A fragment's records, read on another node when it has no replica here (MIM1H12).
frag_records(Frag) ->
    case val({Frag, storage_type}) of
        unknown -> mnesia:dirty_match_object(Frag, val({Frag, wild_pattern}));
        _ -> clause_mnesia_db:match_object(Frag, '_')
    end.

move(_, _, I, I, _, _) ->
    [];
move(Tab, Frag, I, To, Rec, Last) ->
    Write = {n_to_frag_name(Tab, To), element(2, Rec), {write, Rec}},
    case I =:= Last andalso To =/= Last of
        true -> [Write];
        false -> [Write, {Frag, element(2, Rec), delete}]
    end.
