%% mnesia's schema (plan 21 step MIM1A, docs/libraries/mnesia.md#schema): table definitions checked as OTP checks
%% them, the properties each table keeps in mnesia_gvar, and the changes of schema transactions, which
%% clause_mnesia_change prepares and the mnesia_monitor process, owner of every table, applies here. On a disc schema
%% (MIM1D) the changes also reach the directory's files (clause_mnesia_disc).
-module(clause_mnesia_schema).
-export([
    cstruct/1,
    verify/1,
    cs2list/1,
    list2cs/1,
    schema_cstruct/0,
    init_schema/0,
    init_schema/1,
    load_table/1,
    apply_change/2,
    storage/1,
    storage/2,
    properties/1,
    attribute_position/2,
    print/1
]).

-include("clause_mnesia.hrl").

%% The fields a table definition may name (create_table/2's options).
-define(FIELDS, record_info(fields, cstruct)).
%% The properties a table keeps as it runs, which a changed definition leaves alone (MIM1C).
-define(RUNTIME, [checkpoints, commit_work, index, load_by_force, subscribers]).

% A table definition ({Option, Value} list with name) as a cstruct, with OTP's checks of the options.
cstruct(Def) when is_list(Def) ->
    Name = option(unknown, name, Def, must),
    check_duplicates(Name, option_keys(Name, Def)),
    Pick = fun(Key, Default) -> option(Name, Key, Def, Default) end,
    Type = Pick(type, set),
    {Ram, Disc, DiscOnly} = {Pick(ram_copies, []), Pick(disc_copies, []), Pick(disc_only_copies, [])},
    LocalContent = Pick(local_content, false),
    RecordName = Pick(record_name, Name),
    Attributes = Pick(attributes, [key, val]),
    Snmp = Pick(snmp, []),
    LoadOrder = Pick(load_order, 0),
    AccessMode = Pick(access_mode, read_write),
    Majority = Pick(majority, false),
    UserProperties = listed(Pick(user_properties, []), fun(V) -> {bad_type, Name, {user_properties, V}} end),
    Cookie = Pick(cookie, unique_cookie()),
    Version = Pick(version, {{2, 0}, []}),
    Index = listed(Pick(index, []), fun(V) -> {bad_type, Name, {index, [V]}} end),
    Frag = listed(Pick(frag_properties, []), fun(V) -> {badarg, Name, {frag_properties, V}} end),
    Storage = Pick(storage_properties, []),
    check_storage_properties(Name, Storage),
    Cs = #cstruct{
        name = Name,
        type = Type,
        ram_copies = default_nodes(Ram, Disc, DiscOnly),
        disc_copies = Disc,
        disc_only_copies = DiscOnly,
        index = Index,
        snmp = Snmp,
        load_order = LoadOrder,
        access_mode = AccessMode,
        majority = Majority,
        local_content = LocalContent,
        record_name = RecordName,
        attributes = Attributes,
        user_properties = lists:sort(UserProperties),
        frag_properties = lists:sort(Frag),
        storage_properties = lists:sort(Storage),
        cookie = Cookie,
        version = Version
    },
    case Index of
        [] -> Cs;
        [_ | _] -> Cs#cstruct{index = expand_index(Cs)}
    end;
cstruct(Other) ->
    abort({badarg, Other}).

% An option's value: the default when absent, bad_type when its tuple is not a pair.
option(Tab, Key, Def, Default) ->
    case find_option(Key, Def) of
        false when Default =:= must -> abort({badarg, Tab, "Missing key", Key, Def});
        false -> Default;
        {Key, Value} -> Value;
        Bad -> abort({bad_type, Tab, Bad})
    end.

% The first tuple naming Key in a definition; the search ends quietly at an improper tail, as OTP's keysearch does.
find_option(Key, [Option | _]) when is_tuple(Option), tuple_size(Option) > 0, element(1, Option) =:= Key -> Option;
find_option(Key, [_ | Rest]) -> find_option(Key, Rest);
find_option(_, _) -> false.

% A table on no node is a table on this node's RAM.
default_nodes([], [], []) -> [node()];
default_nodes(Ram, _, _) -> Ram.

% A list option's value; Error(Value) is the abort reason of anything else.
listed(Value, _) when is_list(Value) -> Value;
listed(Value, Error) -> abort(Error(Value)).

% storage_properties: ets and dets options that mnesia sets itself are refused.
check_storage_properties(Name, Storage) ->
    check_backend_options(Name, ets, proplists:get_value(ets, Storage, []), [
        set, ordered_set, bag, duplicate_bag, public, private, protected, keypos, named_table
    ]),
    check_backend_options(Name, dets, proplists:get_value(dets, Storage, []), [type, keypos, repair, access, file]).

check_backend_options(Name, Backend, Options, _) when not is_list(Options) ->
    abort({badarg, Name, {Backend, Options}});
check_backend_options(Name, _, Options, Reserved) ->
    lists:foreach(fun(Option) -> check_backend_option(Name, Option, Reserved) end, Options).

check_backend_option(Name, Option, Reserved) when is_atom(Option) ->
    lists:member(Option, Reserved) andalso abort({badarg, Name, Option});
check_backend_option(Name, Option, Reserved) when is_tuple(Option) ->
    tuple_size(Option) > 0 andalso lists:member(element(1, Option), Reserved) andalso abort({badarg, Name, Option});
check_backend_option(Name, Option, _) ->
    abort({badarg, Name, Option}).

% The option keys of a definition; each must name a cstruct field.
option_keys(Name, [{Key, _} | Rest]) ->
    Field =
        if
            is_tuple(Key) -> element(1, Key);
            true -> Key
        end,
    lists:member(Field, ?FIELDS) orelse abort({badarg, Name, Key}),
    [Key | option_keys(Name, Rest)];
option_keys(_, []) ->
    [];
option_keys(Name, Rest) ->
    abort({badarg, Name, Rest}).

check_duplicates(Name, Keys) ->
    case length(lists:usort(Keys)) =:= length(Keys) of
        true -> ok;
        false -> abort({badarg, Name, "Duplicate keys", Keys})
    end.

% The index list with attribute names turned into positions and the default index type added.
expand_index(#cstruct{name = Tab, index = Index, attributes = Attributes}) ->
    lists:map(
        fun
            (P) when is_integer(P); is_atom(P) -> {attribute_position(P, Attributes), ordered};
            ({A} = P) when is_atom(A) -> {P, ordered};
            ({P, Type}) -> {attribute_position(P, Attributes), Type};
            (_) -> abort({bad_type, Tab, {index, Index}})
        end,
        Index
    ).

% An attribute's record position (the key is 2); positions and index plugin names stay as they are.
attribute_position({_} = P, _) -> P;
attribute_position(P, _) when is_integer(P) -> P;
attribute_position(Attribute, Attributes) when is_atom(Attribute) -> position(Attribute, Attributes, 2);
attribute_position(Attribute, _) -> abort({bad_type, Attribute}).

position(Attribute, [Attribute | _], P) -> P;
position(Attribute, [_ | Rest], P) -> position(Attribute, Rest, P + 1);
position(Attribute, _, _) -> abort({bad_type, Attribute}).

% A table definition's checks in OTP's order; index positions expanded.
verify(Cs0) ->
    check(Cs0),
    Cs = Cs0#cstruct{index = expand_index(Cs0)},
    check(Cs),
    Cs.

check(#cstruct{name = Tab} = Cs) ->
    check_nodes(Cs),
    is_atom(Tab) orelse abort({bad_type, Tab}),
    check_type(Cs),
    is_atom(Cs#cstruct.record_name) orelse abort({bad_type, Tab, {record_name, Cs#cstruct.record_name}}),
    check_attributes(Tab, Cs#cstruct.attributes),
    check_index(Cs),
    check_flags(Cs),
    snmp_struct(Cs#cstruct.snmp) orelse abort({badarg, Tab, {snmp, Cs#cstruct.snmp}}),
    [abort({bad_type, Tab, {user_properties, [P]}}) || P <- Cs#cstruct.user_properties, not is_property(P)],
    check_cookie(Tab, Cs#cstruct.cookie),
    check_version(Tab, Cs#cstruct.version).

check_nodes(#cstruct{name = Tab, ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly} = Cs) ->
    is_list(Ram) orelse abort({bad_type, Tab, {ram_copies, Ram}}),
    is_list(Disc) orelse abort({bad_type, Tab, {disc_copies, Disc}}),
    case Tab of
        schema -> DiscOnly =:= [] orelse abort({bad_type, Tab, {disc_only_copies, DiscOnly}});
        _ -> is_list(DiscOnly) orelse abort({bad_type, Tab, {disc_only_copies, DiscOnly}})
    end,
    is_integer(Cs#cstruct.load_order) orelse abort({bad_type, Tab, {load_order, Cs#cstruct.load_order}}),
    Nodes = Ram ++ Disc ++ DiscOnly,
    length(lists:usort(Nodes)) =:= length(Nodes) orelse abort({combine_error, Tab, Nodes}),
    [abort({bad_type, Tab, N}) || N <- Nodes, not is_atom(N)],
    ok.

check_type(#cstruct{name = Tab, type = Type, disc_only_copies = DiscOnly}) ->
    lists:member(Type, [set, bag, ordered_set]) orelse abort({bad_type, Tab, {type, Type}}),
    Type =:= ordered_set andalso DiscOnly =/= [] andalso
        abort({bad_type, Tab, {not_supported, Type, disc_only_copies}}).

% At least two attributes, all atoms but snmp, none twice.
check_attributes(Tab, Attributes) when is_list(Attributes), length(Attributes) >= 2 ->
    lists:foldl(
        fun
            (snmp, _) ->
                abort({bad_type, Tab, {attributes, [snmp]}});
            (Attribute, Seen) ->
                is_atom(Attribute) orelse abort({bad_type, Tab, {attributes, [Attribute]}}),
                lists:member(Attribute, Seen) andalso abort({combine_error, Tab, {attributes, [Attribute | Seen]}}),
                [Attribute | Seen]
        end,
        [],
        Attributes
    );
check_attributes(Tab, Attributes) ->
    abort({bad_type, Tab, {attributes, Attributes}}).

% Each index names a position after the key (or the key itself with allow_index_on_key), bag or ordered.
check_index(#cstruct{name = Tab, index = Index, attributes = Attributes}) ->
    is_list(Index) orelse abort({bad_type, Tab, {index, Index}}),
    Arity = length(Attributes) + 1,
    OnKey = clause_mnesia_monitor:get_env(allow_index_on_key),
    [abort({bad_type, Tab, {index, [P]}}) || P <- Index, not good_index(P, OnKey, Arity)],
    ok.

good_index({P, Type}, OnKey, Arity) -> (Type =:= bag orelse Type =:= ordered) andalso good_position(P, OnKey, Arity);
good_index(P, OnKey, Arity) -> good_position(P, OnKey, Arity).

good_position(P, true, Arity) when is_integer(P) -> P >= 0 andalso P =< Arity;
good_position(P, false, Arity) when is_integer(P) -> P > 2 andalso P =< Arity;
good_position(_, _, _) -> false.

check_flags(#cstruct{name = Tab, local_content = Local, access_mode = Access, majority = Majority}) ->
    lists:member(Local, [true, false]) orelse abort({bad_type, Tab, {local_content, Local}}),
    lists:member(Access, [read_write, read_only]) orelse abort({bad_type, Tab, {access_mode, Access}}),
    lists:member(Majority, [true, false]) orelse abort({bad_type, Tab, {majority, Majority}}),
    Majority andalso Local andalso abort({combine_error, Tab, [{local_content, true}, {majority, true}]}).

is_property(P) -> is_tuple(P) andalso tuple_size(P) >= 1.

% An SNMP structure: none, or the key's types (integer, string, fix_string), alone or as a tuple.
snmp_struct([]) -> true;
snmp_struct([{key, Types}]) when is_tuple(Types) -> snmp_types(tuple_to_list(Types));
snmp_struct([{key, Type}]) -> snmp_types([Type]);
snmp_struct(_) -> false.

snmp_types(Types) -> lists:all(fun(T) -> lists:member(T, [integer, string, fix_string]) end, Types).

check_cookie(_, {{A, B, C}, _}) when is_integer(A), is_integer(B), is_integer(C) -> ok;
check_cookie(Tab, Cookie) -> abort({bad_type, Tab, {cookie, Cookie}}).

check_version(_, {{Major, Minor}, _}) when is_integer(Major), is_integer(Minor) -> ok;
check_version(Tab, Version) -> abort({bad_type, Tab, {version, Version}}).

% A cstruct as the schema table's {Field, Value} list.
cs2list(#cstruct{} = Cs) ->
    Pairs = lists:zip(?FIELDS, tl(tuple_to_list(Cs))),
    lists:append([field(F, V) || {F, V} <- Pairs]).

field(index, Index) -> [{index, [listed_index(P) || P <- Index]}];
field(external_copies, Copies) -> [{Alias, Nodes} || {{Alias, _}, Nodes} <- Copies];
field(Field, Value) -> [{Field, Value}].

listed_index({_, _} = P) -> P;
listed_index(P) -> {P, ordered}.

% A schema record's {Field, Value} list as a cstruct (MIM1D); a list without a cookie gets a new one.
list2cs(List) ->
    Defaults = tl(tuple_to_list(#cstruct{})),
    Cs = list_to_tuple([cstruct | [proplists:get_value(F, List, D) || {F, D} <- lists:zip(?FIELDS, Defaults)]]),
    case Cs#cstruct.cookie of
        undefined -> Cs#cstruct{external_copies = [], cookie = unique_cookie()};
        _ -> Cs#cstruct{external_copies = []}
    end.

% A table cookie unique to this node and moment.
unique_cookie() ->
    {{erlang:monotonic_time() + erlang:time_offset(), erlang:unique_integer(), 1}, node()}.

% The schema table's own definition.
schema_cstruct() ->
    #cstruct{
        name = schema,
        record_name = schema,
        attributes = [table, cstruct],
        ram_copies = [node()],
        cookie = unique_cookie()
    }.

% The schema table and its properties, as mnesia starts with an empty RAM schema.
init_schema() ->
    init_schema(schema_cstruct()).

% The schema table with the schema's definition, from schema.DAT on a disc schema (MIM1D).
init_schema(Cs) ->
    ets:new(schema, [set, public, named_table, {keypos, 2}]),
    set({schema, tables}, []),
    set({schema, local_tables}, []),
    insert(Cs, initial).

% A table of the disc schema at start, with its records from the directory (MIM1D).
load_table(Cs) ->
    insert(Cs, local_only).

% A schema transaction's change, applied in mnesia_monitor, which owns the tables (MIM1C), with the schema table's
% events of transaction Tid as OTP reports them (MIM1E): the record of a new table or a new copy written twice, before
% and after it is stored, a deleted table's record deleted, and any other change's record written once.
apply_change({Twice, #cstruct{name = Tab} = Cs}, Tid) when Twice =:= create; Twice =:= add_copy ->
    Record = {schema, Tab, cs2list(Cs)},
    schema_event(Tid, Record, write),
    case Twice of
        create -> insert(Cs, {dumper, create_table});
        add_copy -> redefine(Cs)
    end,
    schema_event(Tid, Record, write);
apply_change({delete, Tab} = Change, Tid) ->
    [schema_event(Tid, Record, delete) || Record <- ets:lookup(schema, Tab)],
    apply_change(Change);
apply_change({dump, _} = Change, _) ->
    apply_change(Change);
apply_change({restore, Plan, Restore}, Tid) ->
    clause_mnesia_backup:restore_tables(Plan, Restore, Tid);
apply_change({records, Moves}, Tid) ->
    move_records(Moves, Tid);
apply_change(Change, Tid) ->
    Cs = element(2, Change),
    schema_event(Tid, {schema, Cs#cstruct.name, cs2list(Cs)}, write),
    apply_change(Change).

% Records moved between a fragmented table's fragments (MIM1G), whose disc_copies fragments are dumped.
move_records(Moves, Tid) ->
    [clause_mnesia_access:update(Tid, Frag, Key, Update) || {Frag, Key, Update} <- Moves],
    clause_mnesia_disc:use_dir() andalso
        [
            clause_mnesia_disc:ets2dcd(F)
         || F <- lists:usort([F || {F, _, _} <- Moves]), val({F, storage_type}) =:= disc_copies
        ],
    ok.

% A schema record changed by transaction Tid: the schema's checkpoints retain the old one (MIM1F), its subscribers
% get the event.
schema_event(Tid, {schema, Tab, _} = Record, Op) ->
    clause_mnesia_checkpoint:retain(schema, Tab, Op),
    clause_mnesia_subscr:report_schema_event(Tid, Record, Op).

apply_change({delete, Tab}) ->
    drop_replica(Tab),
    clause_mnesia_disc:deleted(Tab),
    ets:match_delete(?GVAR, {{Tab, '_'}, '_'}),
    ets:match_delete(?GVAR, {{Tab, '_', '_'}, '_'}),
    ets:delete(schema, Tab),
    set({schema, tables}, lists:delete(Tab, val({schema, tables})));
apply_change({add_index, #cstruct{name = Tab} = Cs, Index}) ->
    redefine(Cs),
    clause_mnesia_index:add(Tab, Index);
apply_change({del_index, #cstruct{name = Tab} = Cs, Pos}) ->
    redefine(Cs),
    clause_mnesia_index:drop(Tab, Pos);
apply_change({transform, #cstruct{name = Tab, type = Type} = Cs, Changes}) ->
    Positions = lists:reverse([P || {{P, _}, _} <- (val({Tab, index_info}))#index.pos_list]),
    [clause_mnesia_index:drop(Tab, P) || P <- Positions],
    [transform_key(Tab, Type, Change) || Change <- Changes],
    redefine(Cs),
    [clause_mnesia_index:add(Tab, {P, ordered}) || P <- Positions],
    val({Tab, storage_type}) =:= disc_copies andalso clause_mnesia_disc:ets2dcd(Tab);
apply_change({redefine, Cs}) ->
    redefine(Cs);
apply_change({dump, Tab}) ->
    clause_mnesia_disc:ets2dcd(Tab).

% A key's transformed records replace its old ones; a bag's key keeps only its written records, as in OTP.
transform_key(Tab, bag, {Key, Written, _}) ->
    clause_mnesia_db:delete(Tab, Key),
    clause_mnesia_db:insert(Tab, Written);
transform_key(Tab, _, {Key, [], _}) ->
    clause_mnesia_db:delete(Tab, Key);
transform_key(Tab, _, {_, Written, _}) ->
    clause_mnesia_db:insert(Tab, Written).

% A changed definition replaces the schema record and the properties it sets; a replica on this node comes or goes
% with its node lists, and its writers follow the access mode and majority.
redefine(#cstruct{name = Tab} = Cs) ->
    ets:insert(schema, {schema, Tab, cs2list(Cs)}),
    [set({Tab, Item}, Value) || {Item, Value} <- definition_properties(Cs), not lists:member(Item, ?RUNTIME)],
    mnesia_frag:set_frag_hash(Tab, Cs#cstruct.frag_properties),
    ets:match_delete(?GVAR, {{Tab, user_property, '_'}, '_'}),
    set_user_properties(Cs),
    case {val({Tab, storage_type}), storage(Cs)} of
        {unknown, unknown} ->
            ok;
        {unknown, Storage} ->
            make_storage(Cs, Storage, {dumper, add_table_copy}),
            loaded(Cs, Storage, {dumper, add_table_copy});
        {_, unknown} ->
            drop_replica(Tab),
            clause_mnesia_disc:deleted(Tab),
            [set({Tab, Item}, Value) || {Item, Value} <- absent_properties()];
        {Storage, Storage} ->
            [set({Tab, Item}, Value) || {Item, Value} <- writer_properties(Cs, Storage)];
        {Old, New} ->
            converted(Cs, Old, New),
            set({Tab, storage_type}, New),
            [set({Tab, Item}, Value) || {Item, Value} <- writer_properties(Cs, New)]
    end,
    clause_mnesia_snmp:sync(Cs, val({Tab, storage_type})).

% A replica changes its storage type (MIM1D): the schema moves to or from the directory; a table's records move
% between ets and dets, with indexes of the new kind, and its disc files follow.
converted(#cstruct{name = schema}, _, disc_copies) ->
    clause_mnesia_disc:to_disc_schema();
converted(#cstruct{name = schema}, _, ram_copies) ->
    clause_mnesia_disc:to_ram_schema();
converted(#cstruct{name = Tab} = Cs, Old, New) when Old =:= disc_only_copies; New =:= disc_only_copies ->
    Records = clause_mnesia_db:match_object(Tab, '_'),
    Positions = clause_mnesia_index:drop_all(Tab),
    close_storage(Tab, Old),
    clause_mnesia_disc:deleted(Tab),
    make_storage(Cs, New, {dumper, change_table_copy_type}),
    clause_mnesia_db:insert(Tab, Records),
    New =:= disc_copies andalso clause_mnesia_disc:ets2dcd(Tab),
    [clause_mnesia_index:add(Tab, P) || P <- Positions];
converted(#cstruct{name = Tab}, ram_copies, disc_copies) ->
    clause_mnesia_disc:ets2dcd(Tab);
converted(#cstruct{name = Tab}, disc_copies, ram_copies) ->
    clause_mnesia_disc:deleted(Tab).

% This node's replica of a table goes: its checkpoints (MIM1F), its storage, its index tables and its properties as a
% local table.
drop_replica(Tab) ->
    case val({Tab, storage_type}) of
        unknown ->
            ok;
        Storage ->
            clause_mnesia_checkpoint:table_gone(Tab),
            clause_mnesia_snmp:drop(Tab),
            clause_mnesia_index:drop_all(Tab),
            Tab =/= schema andalso close_storage(Tab, Storage),
            ets:delete(?GVAR, {Tab, index_info}),
            set({Tab, commit_work}, []),
            set({schema, local_tables}, lists:delete(Tab, val({schema, local_tables})))
    end.

close_storage(Tab, disc_only_copies) -> dets:close(Tab);
close_storage(Tab, _) -> ets:delete(Tab).

% A new table: its record in the schema, its properties and, on this node, its storage and indexes.
insert(#cstruct{name = Tab} = Cs, Reason) ->
    ets:insert(schema, {schema, Tab, cs2list(Cs)}),
    [set({Tab, Item}, Value) || {Item, Value} <- definition_properties(Cs)],
    mnesia_frag:set_frag_hash(Tab, Cs#cstruct.frag_properties),
    set({schema, tables}, [Tab | val({schema, tables})]),
    set_user_properties(Cs),
    case storage(Cs) of
        unknown ->
            [set({Tab, Item}, Value) || {Item, Value} <- absent_properties()];
        Storage ->
            make_storage(Cs, Storage, Reason),
            loaded(Cs, Storage, Reason),
            clause_mnesia_snmp:sync(Cs, Storage)
    end.

% A replica loaded on this node: its properties, its place among the local tables and its indexes.
loaded(#cstruct{name = Tab} = Cs, Storage, Reason) ->
    [set({Tab, Item}, Value) || {Item, Value} <- loaded_properties(Cs, Storage, Reason)],
    set({schema, local_tables}, [Tab | val({schema, local_tables})]),
    [clause_mnesia_index:add(Tab, P) || P <- Cs#cstruct.index].

% read_table_property/2's rows: each user property under its key (MIM1C).
set_user_properties(#cstruct{name = Tab, user_properties = Properties}) ->
    [set({Tab, user_property, element(1, P)}, P) || P <- Properties].

% A replica's ets or dets table (the schema's ets table exists already), with its records from the directory at
% start, or its new files (MIM1D).
make_storage(#cstruct{name = schema}, _, _) ->
    ok;
make_storage(#cstruct{name = Tab} = Cs, disc_only_copies, _) ->
    set({Tab, storage_type}, disc_only_copies),
    clause_mnesia_disc:open_dat(Cs);
make_storage(#cstruct{name = Tab, type = Type, storage_properties = Properties}, Storage, Reason) ->
    set({Tab, storage_type}, Storage),
    ets:new(Tab, [Type, public, named_table, {keypos, 2} | proplists:get_value(ets, Properties, [])]),
    case Reason of
        local_only -> clause_mnesia_disc:load(Tab, Storage);
        _ -> clause_mnesia_disc:created(Tab, Storage)
    end.

% A node's storage of a table: ram_copies, disc_copies, disc_only_copies or unknown.
storage(Cs) ->
    storage(node(), Cs).

storage(Node, #cstruct{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly}) ->
    case {lists:member(Node, Ram), lists:member(Node, Disc), lists:member(Node, DiscOnly)} of
        {true, _, _} -> ram_copies;
        {_, true, _} -> disc_copies;
        {_, _, true} -> disc_only_copies;
        _ -> unknown
    end.

% The properties a definition sets on every node.
definition_properties(#cstruct{attributes = Attributes, record_name = RecordName} = Cs) ->
    Arity = length(Attributes) + 1,
    [
        {cstruct, Cs},
        {access_mode, Cs#cstruct.access_mode},
        {all_nodes, Cs#cstruct.ram_copies ++ Cs#cstruct.disc_copies ++ Cs#cstruct.disc_only_copies},
        {arity, Arity},
        {attributes, Attributes},
        {checkpoints, []},
        {commit_work, []},
        {cookie, Cs#cstruct.cookie},
        {disc_copies, Cs#cstruct.disc_copies},
        {disc_only_copies, Cs#cstruct.disc_only_copies},
        {external_copies, Cs#cstruct.external_copies},
        {frag_properties, Cs#cstruct.frag_properties},
        {index, [P || {P, _} <- Cs#cstruct.index]},
        {load_by_force, false},
        {load_order, Cs#cstruct.load_order},
        {local_content, Cs#cstruct.local_content},
        {majority, Cs#cstruct.majority},
        {ram_copies, Cs#cstruct.ram_copies},
        {record_name, RecordName},
        {record_validation, {RecordName, Arity, Cs#cstruct.type}},
        {setorbag, Cs#cstruct.type},
        {snmp, Cs#cstruct.snmp},
        {storage_properties, Cs#cstruct.storage_properties},
        {subscribers, []},
        {user_properties, Cs#cstruct.user_properties},
        {version, Cs#cstruct.version},
        {wild_pattern, list_to_tuple([RecordName | lists:duplicate(Arity - 1, '_')])}
    ].

% The properties of a table without a replica on this node.
absent_properties() ->
    [
        {active_replicas, []},
        {load_node, unknown},
        {load_reason, unknown},
        {storage_type, unknown},
        {where_to_commit, []},
        {where_to_read, nowhere},
        {where_to_write, []}
    ].

% The properties of a table loaded on this node.
loaded_properties(#cstruct{type = Type} = Cs, Storage, Reason) ->
    Node = node(),
    [
        {active_replicas, [Node]},
        {index_info, #index{setorbag = Type}},
        {load_node, Node},
        {load_reason, Reason},
        {storage_type, Storage},
        {where_to_read, Node}
        | writer_properties(Cs, Storage)
    ].

% Where a loaded table is written: nowhere when read_only.
writer_properties(#cstruct{access_mode = Access, majority = Majority}, Storage) ->
    Writers =
        case Access of
            read_write -> [node()];
            read_only -> []
        end,
    [
        {where_to_commit, [{N, Storage} || N <- Writers]},
        {where_to_wlock, {Writers, Majority}},
        {where_to_write, Writers}
    ].

% A table's {Item, Value} properties as schema/1 prints them: its mnesia_gvar rows with size, memory and master
% nodes, sorted; [] for an unknown table.
properties(Tab) ->
    case ets:match_object(?GVAR, {{Tab, '_'}, '_'}) of
        [] ->
            [];
        Rows ->
            Measured = [{size, mnesia:table_info(Tab, size)}, {memory, mnesia:table_info(Tab, memory)}],
            lists:sort(
                [{master_nodes, mnesia:table_info(Tab, master_nodes)} | Measured] ++
                    [{Item, Value} || {{_, Item}, Value} <- Rows]
            )
    end.

% schema/1: a table's properties on the terminal.
print(Tab) ->
    Properties =
        try
            properties(Tab)
        catch
            error:_ -> abort({no_exists, Tab, all})
        end,
    io:format("-- Properties for ~tw table --- ~n", [Tab]),
    [io:format("~-20tw -> ~tp~n", [Item, Value]) || {Item, Value} <- Properties, Item =/= cstruct],
    io:format("~n", []).

val(Name) -> clause_mnesia_monitor:val(Name).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).

abort(Reason) -> mnesia:abort(Reason).
