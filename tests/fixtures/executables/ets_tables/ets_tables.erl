-module(ets_tables).
-export([main/1, id/1]).

%% ETS tables (plan 21 step LIB10A): creation options and their errors, info items, names, the basic reads and
%% writes of set and ordered_set tables, key positions, access rights from other processes and deletion. Table
%% identifiers, owners and memory differ between runs and runtimes, so they are printed as checks.
main(_) ->
    creation(),
    options(),
    info_items(),
    names(),
    basics(set),
    basics(ordered_set),
    keys(),
    terms(set),
    terms(ordered_set),
    access(),
    deletion(),
    io:format("ets_tables: ok~n").

id(X) -> X.

show(Label, Values) -> io:format("~p: ~w~n", [Label, Values]).

attempt(F) ->
    try F() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

%% The info list without the items that differ between runs.
stable_info(T) ->
    [Item || {Key, _} = Item <- ets:info(T), not lists:member(Key, [id, memory, owner])].

creation() ->
    T = ets:new(plain, []),
    N = ets:new(named, [named_table]),
    show(creation, [
        is_reference(T),
        N,
        [Key || {Key, _} <- ets:info(T)],
        stable_info(T),
        stable_info(N),
        ets:info(T, owner) =:= self(),
        ets:info(T, id) =:= T,
        ets:info(N, id) =:= ets:whereis(named),
        is_integer(ets:info(T, memory)),
        attempt(fun() -> ets:new(named, [named_table]) end),
        attempt(fun() -> ets:new(id("plain"), []) end),
        attempt(fun() -> ets:new(plain, id(set)) end),
        attempt(fun() -> ets:new(plain, id([set | bag])) end)
    ]),
    ets:delete(T),
    ets:delete(N).

option(Options) ->
    case attempt(fun() -> ets:new(option, Options) end) of
        {ok, T} ->
            Info = stable_info(T),
            ets:delete(T),
            Info;
        Error ->
            Error
    end.

options() ->
    Self = self(),
    show(options, [
        option([bag]),
        option([duplicate_bag, public]),
        option([ordered_set, private]),
        option([set, bag]),
        option([public, private]),
        option([{keypos, 3}]),
        option([compressed]),
        option([{write_concurrency, true}]),
        option([{write_concurrency, auto}]),
        option([ordered_set, {write_concurrency, true}]),
        option([{read_concurrency, true}, {write_concurrency, true}]),
        option([{decentralized_counters, true}]),
        option([{decentralized_counters, false}, {write_concurrency, auto}]),
        option([{heir, none}]),
        option([{heir, Self, data}]) =:= option([{heir, none}]),
        lists:keyfind(
            heir,
            1,
            option([
                {heir,
                    spawn(fun() ->
                        receive
                            stop -> ok
                        end
                    end), data}
            ])
        ) =/=
            {heir, none},
        option([{keypos, 0}]),
        option([{keypos, id(a)}]),
        option([id(sorted)]),
        option([{heir, id(nobody), data}]),
        option([{heir, id(nobody)}]),
        lists:keyfind(heir, 1, option([{heir, Self}])) =:= {heir, Self},
        option([{read_concurrency, id('maybe')}]),
        option([{write_concurrency, id(sometimes)}]),
        option([{id(keypos), 1, 2}])
    ]).

info_items() ->
    T = ets:new(items, [ordered_set, {keypos, 2}]),
    ets:insert(T, [{a, 1}, {b, 2}]),
    Items = [
        name,
        type,
        size,
        keypos,
        protection,
        named_table,
        node,
        heir,
        compressed,
        read_concurrency,
        write_concurrency,
        decentralized_counters,
        fixed
    ],
    Memory = ets:info(T, memory),
    ets:insert(T, {c, 3}),
    show(info_items, [
        [ets:info(T, Item) || Item <- Items],
        ets:info(T, memory) > Memory,
        ets:info(T, safe_fixed),
        ets:info(T, safe_fixed_monotonic_time),
        attempt(fun() -> ets:info(T, id(colour)) end),
        attempt(fun() -> ets:info(id(42)) end),
        attempt(fun() -> ets:info(id(42), size) end),
        ets:info(no_such_table),
        ets:info(no_such_table, size)
    ]),
    ets:delete(T),
    show(info_deleted, [ets:info(T), ets:info(T, size), ets:info(T, name)]).

names() ->
    T = ets:new(first_name, [named_table, public]),
    Ref = ets:whereis(first_name),
    U = ets:new(unnamed, []),
    show(names, [
        T,
        is_reference(Ref),
        ets:whereis(no_such_table),
        attempt(fun() -> ets:whereis(id("first_name")) end),
        lists:member(first_name, ets:all()),
        lists:member(Ref, ets:all()),
        lists:member(U, ets:all()),
        ets:insert(first_name, {k, v}),
        ets:rename(first_name, second_name),
        ets:whereis(first_name),
        ets:whereis(second_name) =:= Ref,
        ets:lookup(Ref, k),
        ets:lookup(second_name, k),
        attempt(fun() -> ets:lookup(first_name, k) end),
        ets:rename(U, renamed) =:= U,
        ets:info(U, name),
        ets:whereis(renamed),
        attempt(fun() -> ets:rename(Ref, unnamed_other) =:= unnamed_other end),
        ets:new(blocker, [named_table]),
        attempt(fun() -> ets:rename(second_name, blocker) end),
        attempt(fun() -> ets:rename(second_name, id("text")) end),
        attempt(fun() -> ets:rename(no_such_table, other) end),
        lists:member(unnamed_other, ets:all()),
        ets:info(Ref, named_table)
    ]),
    ets:delete(Ref),
    ets:delete(U),
    ets:delete(blocker).

basics(Type) ->
    T = ets:new(basics, [Type]),
    show(Type, [
        ets:insert(T, {1, one}),
        ets:insert(T, [{2, two}, {3, three}]),
        ets:insert(T, []),
        ets:lookup(T, 1),
        ets:lookup(T, 4),
        ets:member(T, 2),
        ets:member(T, 4),
        ets:insert(T, {1, uno}),
        ets:lookup(T, 1),
        ets:insert(T, {1.0, float}),
        ets:lookup(T, 1),
        ets:lookup(T, 1.0),
        ets:member(T, 1.0),
        ets:info(T, size),
        ets:insert_new(T, {4, four}),
        ets:insert_new(T, {4, again}),
        ets:lookup(T, 4),
        ets:insert_new(T, [{5, five}, {4, clash}]),
        ets:member(T, 5),
        ets:insert_new(T, [{5, five}, {6, six}]),
        ets:insert_new(T, []),
        ets:delete(T, 2),
        ets:delete(T, 2),
        ets:lookup(T, 2),
        ets:info(T, size),
        attempt(fun() -> ets:insert(T, id(notuple)) end),
        attempt(fun() -> ets:insert(T, id({})) end),
        attempt(fun() -> ets:insert(T, id([{7, seven}, bad])) end),
        ets:member(T, 7),
        attempt(fun() -> ets:insert(T, id([{8, eight} | {9, nine}])) end),
        ets:member(T, 8),
        attempt(fun() -> ets:insert_new(T, id([{8, eight}, bad])) end),
        ets:member(T, 8),
        attempt(fun() -> ets:insert_new(T, id(notuple)) end),
        lists:sort(ets:tab2list(T))
    ]),
    ets:delete(T).

keys() ->
    T = ets:new(keys, [{keypos, 2}]),
    O = ets:new(keys, [ordered_set, {keypos, 3}]),
    show(keys, [
        ets:insert(T, {a, k1, x}),
        ets:insert(T, {b, k1}),
        ets:lookup(T, k1),
        ets:lookup(T, a),
        attempt(fun() -> ets:insert(T, id({only})) end),
        attempt(fun() -> ets:insert(O, id({a, b})) end),
        ets:insert(O, [{x, y, 2}, {x, y, 1}, {x, y, 1.0}]),
        ets:lookup(O, 1),
        lists:sort(ets:tab2list(O)),
        ets:insert_new(O, {z, z, 2.0}),
        ets:member(O, 2.0)
    ]),
    ets:delete(T),
    ets:delete(O).

%% Keys and values of every kind survive the copies in and out, including binaries the program no longer holds.
terms(Type) ->
    T = ets:new(terms, [Type]),
    Big = binary:copy(<<"0123456789">>, 20),
    Keys = [
        atom,
        -5,
        1 bsl 80,
        2.5,
        "string",
        <<"small">>,
        Big,
        {nested, [tuple, {1, 2}]},
        #{a => 1, b => [2]},
        [],
        {},
        self(),
        make_ref()
    ],
    [ets:insert(T, {Key, {value, Key}}) || Key <- Keys],
    Bins = [{bin, N, binary:copy(<<N>>, 100)} || N <- lists:seq(1, 5)],
    [ets:insert(T, Bin) || Bin <- id(Bins)],
    erlang:garbage_collect(),
    show(terms, [
        [ets:lookup(T, Key) =:= [{Key, {value, Key}}] || Key <- Keys],
        ets:lookup(T, Big) =:= [{Big, {value, Big}}],
        [byte_size(element(3, hd(ets:lookup(T, bin)))) || _ <- [x]],
        ets:info(T, size),
        lists:sort([K || {K, _} <- ets:tab2list(T), is_atom(K) orelse is_number(K)])
    ]),
    ets:delete(T).

%% Runs F in another process and returns its outcome.
elsewhere(F) ->
    Self = self(),
    Pid = spawn(fun() -> Self ! {self(), attempt(F)} end),
    receive
        {Pid, Result} -> Result
    end.

access() ->
    Public = ets:new(public, [public]),
    Protected = ets:new(protected, [protected]),
    Private = ets:new(private, [private]),
    [ets:insert(T, {k, v}) || T <- [Public, Protected, Private]],
    show(access, [
        [elsewhere(fun() -> ets:lookup(T, k) end) || T <- [Public, Protected, Private]],
        [elsewhere(fun() -> ets:insert(T, {k2, v2}) end) || T <- [Public, Protected, Private]],
        [elsewhere(fun() -> ets:member(T, k) end) || T <- [Public, Protected, Private]],
        [elsewhere(fun() -> ets:info(T, size) end) || T <- [Public, Protected, Private]],
        [elsewhere(fun() -> ets:delete(T, k) end) || T <- [Public, Protected, Private]],
        [elsewhere(fun() -> ets:rename(T, moved) =:= T end) || T <- [Public, Protected, Private]],
        [ets:info(T, size) || T <- [Public, Protected, Private]],
        elsewhere(fun() -> ets:delete(Protected) end),
        elsewhere(fun() -> ets:delete(Private) end),
        elsewhere(fun() -> ets:delete(Public) end),
        ets:info(Public, size)
    ]),
    ets:delete(Protected),
    ets:delete(Private).

deletion() ->
    T = ets:new(gone, [named_table]),
    ets:insert(gone, {k, v}),
    Ref = ets:whereis(gone),
    show(deletion, [
        ets:delete(gone),
        attempt(fun() -> ets:lookup(gone, k) end),
        attempt(fun() -> ets:lookup(Ref, k) end),
        attempt(fun() -> ets:insert(Ref, {k, v}) end),
        attempt(fun() -> ets:delete(Ref) end),
        attempt(fun() -> ets:delete(gone) end),
        attempt(fun() -> ets:delete(id(42)) end),
        ets:whereis(gone),
        lists:member(Ref, ets:all()),
        ets:new(gone, [named_table]) =:= T,
        ets:delete(gone)
    ]).
