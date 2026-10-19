-module(ets_match_specs).
-export([main/1, id/1]).

%% Match specifications (plan 21 step LIB11): compiling and running them (match_spec_compile/1, match_spec_run/2,
%% test_ms/2, erlang:match_spec_test/3 with OTP's lint messages), and the select, match and match_object families of
%% ets with their continuations, select_count/2, select_delete/2, select_replace/2, match_delete/2,
%% repair_continuation/2 and init_table/2. Results of hash tables are printed sorted.
main(_) ->
    show(compiled, compiled()),
    show(guards, guards()),
    show(arithmetic, arithmetic()),
    show(bodies, bodies()),
    show(logic, logic()),
    show(heads, heads()),
    show(lint, lint()),
    show(trace, trace()),
    show(trace_lint, trace_lint()),
    show(select, select()),
    show(ordered_chunks, ordered_chunks()),
    show(hash_chunks, hash_chunks()),
    show(match, match()),
    show(counts, counts()),
    show(deletes, deletes()),
    show(replaces, replaces()),
    show(bound_keys, bound_keys()),
    show(continuations, continuations()),
    show(init_table, init_table()),
    show(concurrent, concurrent()),
    show(fixed, fixed()),
    show(errors, errors()),
    io:format("ets_match_specs: ok~n").

id(X) -> X.

show(Label, Value) -> io:format("~p: ~w~n", [Label, Value]).

attempt(F) ->
    try F() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

test(Term, Spec) -> attempt(fun() -> ets:test_ms(Term, Spec) end).

compiled() ->
    Spec = [{{'$1', '$2'}, [{'>', '$2', 1}], [{{'$2', '$1'}}]}],
    Compiled = ets:match_spec_compile(Spec),
    [
        ets:is_compiled_ms(Compiled),
        ets:is_compiled_ms(Spec),
        ets:is_compiled_ms(make_ref()),
        ets:match_spec_run([{a, 1}, {b, 2}, {c, 3}, x, {d}], Compiled),
        ets:match_spec_run([], Compiled),
        ets:is_compiled_ms(binary_to_term(term_to_binary(Compiled))),
        ets:match_spec_run([{e, 5}], binary_to_term(term_to_binary(Compiled))),
        attempt(fun() -> ets:match_spec_compile(id([{a}])) end),
        attempt(fun() -> ets:match_spec_run(id([{a, 1} | b]), Compiled) end),
        attempt(fun() -> ets:match_spec_run([{a, 1}], id(Spec)) end),
        attempt(fun() -> ets:match_spec_run_r([{a, 1}, {b, 2}], Compiled, [z]) end)
    ].

%% Guard functions on the object {a, 1, 2.5, "str", <<"bin">>, [x | y], #{k => v}, {t}} bound to '$1'.
guards() ->
    Object = {a, 1, 2.5, "str", <<"bin">>, [x | y], #{k => v}, {t}},
    Tests = [
        {is_atom, {element, 1, '$1'}},
        {is_integer, {element, 2, '$1'}},
        {is_float, {element, 3, '$1'}},
        {is_number, {element, 3, '$1'}},
        {is_list, {element, 4, '$1'}},
        {is_binary, {element, 5, '$1'}},
        {is_bitstring, {element, 5, '$1'}},
        {is_map, {element, 7, '$1'}},
        {is_tuple, {element, 8, '$1'}},
        {is_pid, {self}},
        {is_reference, {element, 1, '$1'}},
        {is_port, {element, 1, '$1'}},
        {is_function, {element, 1, '$1'}},
        {is_boolean, {'==', a, a}},
        {is_record, {element, 8, '$1'}, t, 1},
        {is_record, {element, 8, '$1'}, u, 1},
        {is_integer, {element, 2, '$1'}, 0, 1},
        {is_integer, {element, 2, '$1'}, 2, 3},
        {is_map_key, k, {element, 7, '$1'}},
        {'=:=', {map_get, k, {element, 7, '$1'}}, v},
        {'=:=', {map_size, {element, 7, '$1'}}, 1},
        {'=:=', {hd, {element, 6, '$1'}}, x},
        {'=:=', {tl, {element, 6, '$1'}}, y},
        {'=:=', {length, {element, 4, '$1'}}, 3},
        {'=:=', {size, {element, 5, '$1'}}, 3},
        {'=:=', {tuple_size, '$1'}, 8},
        {'=:=', {byte_size, {element, 5, '$1'}}, 3},
        {'=:=', {bit_size, {element, 5, '$1'}}, 24},
        {'=:=', {binary_part, {element, 5, '$1'}, 1, 2}, <<"in">>},
        {'=:=', {binary_part, {element, 5, '$1'}, {3, -2}}, <<"in">>},
        {'=:=', {node}, {node, {self}}},
        {'=:=', {element, 9, '$1'}, x},
        {'=:=', {length, {element, 6, '$1'}}, 2},
        {'=:=', {map_get, z, {element, 7, '$1'}}, v},
        {'=:=', {binary_part, {element, 5, '$1'}, 2, 2}, <<"n">>},
        {is_function, '$1', 2},
        {'==', 1, 1.0},
        {'=:=', 1, 1.0},
        {'/=', 1, 1.0},
        {'=/=', 1, 1.0},
        {'<', a, 1},
        {'>=', {t}, a},
        {'=<', [], [a]}
    ],
    [element(2, test(Object, [{'$1', [Guard], [true]}])) || Guard <- Tests].

arithmetic() ->
    Expressions = [
        {'+', '$1', '$2'},
        {'-', '$1', '$2'},
        {'*', '$1', '$2'},
        {'/', '$1', '$2'},
        {'div', '$1', '$2'},
        {'rem', '$1', '$2'},
        {'band', '$1', '$2'},
        {'bor', '$1', '$2'},
        {'bxor', '$1', '$2'},
        {'bsl', '$1', '$2'},
        {'bsr', '$1', '$2'},
        {'-', '$1'},
        {'+', '$2'},
        {'bnot', '$1'},
        {abs, {'-', '$1'}},
        {float, '$1'},
        {round, {'/', '$1', '$2'}},
        {trunc, {'/', '$1', '$2'}},
        {floor, {'/', '$1', '$2'}},
        {ceil, {'/', '$1', '$2'}},
        {max, '$1', '$2'},
        {min, '$1', '$2'},
        {'*', '$1', 1180591620717411303424},
        {'div', '$1', 0},
        {'+', '$1', a}
    ],
    [
        [element(2, test({X, Y}, [{{'$1', '$2'}, [], [E]}])) || E <- Expressions]
     || {X, Y} <- [{17, 5}, {-17, 3}]
    ].

bodies() ->
    Object = {k, [1, 2], #{a => 1}},
    Bodies = [
        ['$_'],
        ['$$'],
        ['$1'],
        [{{'$1', '$2', '$3'}}],
        [{{}}],
        [['$1', '$2' | '$3']],
        [#{'$1' => '$2', 1 => {const, '$3'}}],
        [#{x => {const, y}}],
        [{const, {a, '$1'}}],
        [{{{const, '$1'}, [{const, x}]}}],
        [first, second],
        [{'+', '$1', 1}],
        [{{'$1', {'+', '$1', 1}}}],
        [{'+', '$1', 1}, done],
        [{is_atom, '$1'}],
        [{'and', true, '$1'}],
        [{'andalso', true, '$1'}],
        [{'orelse', '$1', true}],
        [{'not', '$1'}],
        [{'xor', true, false}],
        [{element, 2, '$_'}],
        [{tl, {element, 2, '$_'}}],
        [{is_pid, {self}}],
        [{'=:=', {self}, {self}}],
        [[]],
        ['_'],
        [{{a, '_'}}]
    ],
    [element(2, test(Object, [{{'$1', '$2', '$3'}, [], Body}])) || Body <- Bodies].

logic() ->
    Guards = [
        [{'and', true, true}],
        [{'and', true, false}],
        [{'and', true, ok}],
        [{'or', false, true}],
        [{'or', false, false}],
        [{'or', true, ok}],
        [{'andalso', false, {'/', 1, 0}}],
        [{'andalso', true, {'/', 1, 0}}],
        [{'andalso', true, true, false}],
        [{'orelse', true, {'/', 1, 0}}],
        [{'orelse', false, false, true}],
        [{'andthen', true, true}],
        [{'andalso', ok, true}],
        [{'andalso', true, ok}],
        [{'/', 1, 0}],
        [ok],
        [true, true],
        [true, false],
        [{const, true}],
        [{'not', {'/', 1, 0}}],
        []
    ],
    [
        element(2, test({a}, [{{'$1'}, Guard, [first]}, {'_', [], [second]}]))
     || Guard <- Guards
    ].

heads() ->
    Heads = [
        {'$1', '$1'},
        {'$1', '_'},
        {'_', '$2'},
        {'$0', '$100'},
        {a, '$1'},
        {[a | '$1'], '_'},
        {[a, b], '_'},
        {'$1', [b | '_']},
        {#{k => '$1'}, '_'},
        {#{}, '_'},
        {#{k => 1}, '_'},
        {1, '_'},
        {1.0, '_'},
        {"ab", '_'},
        {<<"bin">>, '_'},
        {{'$1', '$2'}, '_'},
        '$1',
        '_',
        [],
        {}
    ],
    Objects = [{a, b}, {x, x}, {1, 2}, {[a, b], c}, {#{k => 1}, z}, {{p, q}, r}, {"ab", <<"bin">>}],
    [[element(2, test(Object, [{Head, [], ['$$']}])) || Object <- Objects] || Head <- Heads].

%% Specifications OTP rejects, with lint's messages.
lint() ->
    Specs = [
        a,
        [a],
        [{a, [], []} | b],
        [{a, b}],
        [{{'$1'}, ['$2'], [true]}],
        [{{'$1'}, [], ['$2']}],
        [{{'$1'}, [], [{foo, '$1'}]}],
        [{{'$1'}, [], [{'+', '$5', '$6'}]}],
        [{{'$1'}, [{get_tcw}], [true]}],
        [{{'$1'}, [], [{message, '$1'}]}],
        [{{'$1'}, [], [{return_trace}]}],
        [{{'$1'}, [], [{display, x}]}],
        [{{'$1'}, [], [{caller}]}],
        [{{'$1'}, [], [{const, a, b}]}],
        [{{'$1'}, [], [{'and'}]}],
        [{{'$1'}, [], [{self, x}]}],
        [{{'$1'}, [], [{1, 2}]}],
        [{{'$1'}, [], [{}]}],
        [{{'$1'}, [], [{'$1', a}]}],
        [{{'$1'}, a, [true]}],
        [{{'$1'}, [a | b], [true]}],
        [{{'$1'}, [], a}],
        [{{'$1'}, [], []}],
        [{{'$1'}, [], [a | b]}],
        [{#{'$1' => a}, [], [true]}],
        [{#{'_' => a}, [], [true]}],
        [{{'$1'}, [], [true]}, {{'$1'}, [], ['$2']}],
        [{{'$1'}, [], ['$2']}, {{'$1'}, [], ['$3']}],
        [{{'$1'}, [{is_atom, '$2'}, {is_atom, '$3'}], ['$4']}],
        [
            {{'$1'}, [], [
                {some_function_with_a_rather_long_name_here, '$1', [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]}
            ]}
        ]
    ],
    [test({x}, Spec) || Spec <- Specs].

%% The trace dialect of erlang:match_spec_test/3 on argument lists. enable_trace, disable_trace and trace with valid
%% flags are left out: OTP 29.1.1 crashes running them without a tracer.
trace() ->
    Cases = [
        {[1, 2], [{['$1', '$2'], [], [{message, {'+', '$1', '$2'}}]}]},
        {[1, 2], [{['$1', '_'], [], [{return_trace}]}]},
        {[1, 2], [{['$1' | '_'], [], [{exception_trace}]}]},
        {[1, 2], [{'$1', [], [{message, '$1'}]}]},
        {[1, 2], [{'_', [], [{message, '$_'}]}]},
        {[1, 2], [{[], [], [true]}]},
        {[], [{[], [], [{message, empty}]}]},
        {[1, 2], [{['$1'], [], [true]}, {['_', '$2'], [], [{message, '$2'}]}]},
        {[1, 2], [{['$1', '$2'], [{'>', '$1', '$2'}], [true]}]},
        {[a], [{['$1'], [], [{message, false}]}]},
        {[a], [{['$1'], [], [{message, true}]}]},
        {[a], [{['$1'], [], [{message, x}, {message, y}]}]},
        {[a], [{['$1'], [], [{caller}, {caller_line}, {message, {{caller}, {caller_line}}}]}]},
        {[a], [{['$1'], [], [{message, {is_seq_trace}}]}]},
        {[a], [{['$1'], [{is_seq_trace}], [true]}]},
        {[a], [{['$1'], [], [{message, {get_seq_token}}]}]},
        {[a], [{['$1'], [], [{message, {set_seq_token, label, 1}}]}]},
        {[a], [{['$1'], [], [{message, {enable_trace, bogus}}]}]},
        {[a], [{['$1'], [], [{message, {silent, true}}]}]},
        {[a], [{['$1'], [], [{message, {get_tcw}}]}]},
        {[a], [{['$1'], [], [{message, {set_tcw, 7}}]}]},
        {[a], [{['$1'], [{'==', {get_tcw}, 0}], [true]}]},
        {[a], [{['$1'], [], [{return_trace}, {exception_trace}]}]},
        {[a], [{['$1'], [], [{message, {{self}}}]}]},
        {[a], [{['$1'], [], [{message, {is_process_alive, {self}}}]}]}
    ],
    [
        normalize(attempt(fun() -> erlang:match_spec_test(Args, Spec, trace) end))
     || {Args, Spec} <- Cases
    ].

%% Pids are shown as 'pid' (they differ between runs).
normalize(Term) when is_pid(Term) -> pid;
normalize(Term) when is_tuple(Term) -> list_to_tuple(normalize(tuple_to_list(Term)));
normalize([Head | Tail]) -> [normalize(Head) | normalize(Tail)];
normalize(Term) -> Term.

trace_lint() ->
    Specs = [
        [{a, [], []}],
        [{[a | b], [], []}],
        [{['$1'], [{message, '$1'}], []}],
        [{['$1'], [], [{message}]}],
        [{['$1'], [], [{return_trace, x}]}],
        [{['$1'], [], [{enable_trace}]}],
        [{['$1'], [], [{trace, a}]}],
        [{['$1'], [], [{current_stacktrace, a}]}],
        [{['$1'], [], [{current_stacktrace, -1}]}],
        [{['$1'], [], [{set_seq_token, a}]}],
        [{['$1'], [{return_trace}], []}],
        [{['$1'], [{set_tcw, 1}], []}],
        [{['$1'], [], [{silent}]}],
        [{['$1'], [], [{process_dump, x}]}]
    ],
    [
        attempt(fun() -> erlang:match_spec_test([x], Spec, trace) end)
     || Spec <- Specs
    ] ++
        [
            attempt(fun() -> erlang:match_spec_test(id(not_a_list), [{'_', [], []}], trace) end),
            attempt(fun() -> erlang:match_spec_test([x], [{'_', [], []}], id(other)) end),
            attempt(fun() -> erlang:match_spec_test({x}, [{'_', [], [{message, 1}]}], table) end)
        ].

filled(Type) ->
    T = ets:new(t, [Type]),
    ets:insert(T, [
        {K, K * K,
            if
                K rem 2 =:= 0 -> even;
                true -> odd
            end}
     || K <- lists:seq(1, 10)
    ]),
    T.

sorted(List) when is_list(List) -> lists:sort(List);
sorted(Other) -> Other.

select() ->
    [
        begin
            T = filled(Type),
            Spec = [{{'$1', '$2', even}, [{'>', '$2', 10}], [{{'$2', '$1'}}]}],
            Result = [
                sorted(ets:select(T, Spec)),
                sorted(ets:select_reverse(T, Spec)),
                sorted(ets:select(T, [{'_', [], ['$_']}])),
                ets:select(T, [{{'$1', '_', '_'}, [{'>', '$1', 100}], ['$1']}]),
                ets:select(T, []),
                sorted(
                    ets:select(T, [
                        {{'$1', '_', odd}, [], ['$1']},
                        {{'$1', '_', even}, [{'<', '$1', 5}], [{{e, '$1'}}]}
                    ])
                )
            ],
            ets:delete(T),
            Result
        end
     || Type <- [set, ordered_set, bag, duplicate_bag]
    ] ++
        [ordered_select()].

%% An ordered_set's select comes in key order, select_reverse in reverse.
ordered_select() ->
    T = filled(ordered_set),
    R = [
        ets:select(T, [{{'$1', '_', '_'}, [], ['$1']}]),
        ets:select_reverse(T, [{{'$1', '_', '_'}, [], ['$1']}])
    ],
    ets:delete(T),
    R.

%% Chunks of an ordered_set come in key order, Limit at a time; a reverse select from the last key.
ordered_chunks() ->
    T = filled(ordered_set),
    Spec = [{{'$1', '_', '_'}, [], ['$1']}],
    R = [
        chunks(ets:select(T, Spec, 3)),
        chunks(ets:select_reverse(T, Spec, 4)),
        chunks(ets:select(T, Spec, 10)),
        chunks(ets:select(T, Spec, 11)),
        chunks(ets:select(T, [{{'$1', '_', even}, [], ['$1']}], 2)),
        chunks(ets:select(T, [{{'$1', '_', none}, [], ['$1']}], 2)),
        chunks(ets:match(T, {'$1', '_', odd}, 2)),
        chunks(ets:match_object(T, {'_', '_', odd}, 4)),
        chunks(ets:select(T, [{{3, '_', '_'}, [], ['$_']}], 1))
    ],
    ets:delete(T),
    R.

chunks('$end_of_table') -> ['$end_of_table'];
chunks({Matches, Continuation}) -> [Matches | chunks(next(Continuation))].

next('$end_of_table') -> '$end_of_table';
next(Continuation) -> ets:select(Continuation).

%% Chunks of hash tables: their sizes, and every result once over all chunks.
hash_chunks() ->
    [
        begin
            T = filled(Type),
            Spec = [{{'$1', '_', '_'}, [], ['$1']}],
            Chunks = chunks(ets:select(T, Spec, 3)),
            Matches = lists:droplast(Chunks),
            R = [
                [length(C) || C <- Matches],
                lists:sort(lists:append(Matches)),
                length(lists:droplast(chunks(ets:select(T, Spec, 20)))),
                chunks(ets:select(T, [{{'$1', '_', none}, [], ['$1']}], 2)),
                sorted(
                    lists:append(
                        lists:droplast(chunks(ets:select(T, [{{4, '_', '_'}, [], ['$_']}], 1)))
                    )
                )
            ],
            ets:delete(T),
            R
        end
     || Type <- [set, bag]
    ].

match() ->
    [
        begin
            T = ets:new(t, [Type]),
            ets:insert(T, [{a, 1, x}, {b, 2, y}, {c, 1, x}, {a, 3, z}, {d, [1, 2], {p, q}}]),
            R = [
                sorted(ets:match(T, {'$1', 1, '$2'})),
                sorted(ets:match(T, {'$2', 1, '$1'})),
                sorted(ets:match(T, {'_', '$1', '_'})),
                sorted(ets:match(T, {'$1', '_', '$1'})),
                sorted(ets:match(T, {'_', [1, '$1'], {'$2', '_'}})),
                sorted(ets:match_object(T, {'_', 1, '_'})),
                sorted(ets:match_object(T, {a, '_', '_'})),
                sorted(ets:match(T, '$1')),
                sorted(ets:match(T, {'_', '_'})),
                sorted(ets:match_object(T, '_'))
            ],
            ets:delete(T),
            R
        end
     || Type <- [set, ordered_set, bag]
    ].

counts() ->
    [
        begin
            T = filled(Type),
            R = [
                ets:select_count(T, [{{'_', '_', even}, [], [true]}]),
                ets:select_count(T, [{{'$1', '_', '_'}, [{'>', '$1', 3}], [true]}]),
                ets:select_count(T, [{{'$1', '_', '_'}, [], [false]}]),
                ets:select_count(T, [{{'$1', '_', '_'}, [], ['$1']}]),
                ets:select_count(T, [{{5, '_', '_'}, [], [true]}]),
                ets:select_count(T, [])
            ],
            ets:delete(T),
            R
        end
     || Type <- [set, ordered_set, bag]
    ].

deletes() ->
    [
        begin
            T = ets:new(t, [Type]),
            ets:insert(T, [{a, 1}, {a, 2}, {a, 2}, {b, 1}, {c, 3}, {d, 4}]),
            R1 = ets:select_delete(T, [{{'_', 2}, [], [true]}]),
            L1 = sorted(ets:tab2list(T)),
            R2 = ets:select_delete(T, [{{'$1', '_'}, [{'=:=', '$1', c}], [true]}]),
            R3 = ets:select_delete(T, [{{'_', '_'}, [], [false]}]),
            R4 = ets:select_delete(T, [{{d, '_'}, [], [true]}]),
            R5 = ets:match_delete(T, {b, '_'}),
            L2 = sorted(ets:tab2list(T)),
            R6 = ets:select_delete(T, [{'_', [], [true]}]),
            R = [R1, L1, R2, R3, R4, R5, L2, R6, ets:info(T, size)],
            ets:delete(T),
            R
        end
     || Type <- [set, ordered_set, bag, duplicate_bag]
    ].

replaces() ->
    [
        begin
            T = ets:new(t, [Type]),
            ets:insert(T, [{a, 1}, {b, 2}, {c, 3}]),
            R = [
                ets:select_replace(T, [
                    {{'$1', '$2'}, [{'>', '$2', 1}], [{{'$1', {'*', '$2', 10}}}]}
                ]),
                sorted(ets:tab2list(T)),
                ets:select_replace(T, [{{a, '_'}, [], [{const, {a, new}}]}]),
                ets:select_replace(T, [{{'$1', '_'}, [], [{{'$1', {element, 2, '$_'}}}]}]),
                attempt(fun() -> ets:select_replace(T, [{'$1', [], ['$1']}]) end),
                attempt(fun() -> ets:select_replace(T, [{{'_', '$1'}, [], [{{'$1', x}}]}]) end),
                sorted(ets:tab2list(T))
            ],
            ets:delete(T),
            R
        end
     || Type <- [set, ordered_set, duplicate_bag]
    ].

bound_keys() ->
    [
        begin
            T = filled(Type),
            ets:insert(T, {{tuple, key}, 0, odd}),
            R = [
                sorted(ets:select(T, [{{3, '$1', '_'}, [], ['$1']}])),
                sorted(ets:select(T, [{{3, '$1', '_'}, [], ['$1']}, {{7, '$1', '_'}, [], ['$1']}])),
                sorted(ets:select(T, [{{3, '$1', '_'}, [], ['$1']}, {{3, '$1', '_'}, [], ['$1']}])),
                sorted(ets:select(T, [{{99, '$1', '_'}, [], ['$1']}])),
                sorted(ets:select(T, [{{3.0, '$1', '_'}, [], ['$1']}])),
                sorted(ets:select(T, [{{{tuple, key}, '$1', '_'}, [], ['$1']}])),
                sorted(ets:select(T, [{{{tuple, '_'}, '$1', '_'}, [], ['$1']}])),
                sorted(ets:select(T, [{x, [], [x]}, {{2, '$1', '_'}, [], ['$1']}])),
                ets:select_count(T, [{{3, '_', '_'}, [], [true]}]),
                sorted(ets:match_object(T, {3, '_', '_'}))
            ],
            ets:delete(T),
            R
        end
     || Type <- [set, ordered_set, bag]
    ].

continuations() ->
    T = filled(ordered_set),
    Spec = [{{'$1', '_', '_'}, [], ['$1']}],
    {First, C1} = ets:select(T, Spec, 3),
    Copy = binary_to_term(term_to_binary(C1)),
    Repaired = ets:repair_continuation(Copy, Spec),
    {Second, _} = ets:select(Repaired),
    {Again, _} = ets:select(C1),
    H = filled(set),
    {_, HC} = ets:select(H, Spec, 2),
    HRepaired = ets:repair_continuation(binary_to_term(term_to_binary(HC)), Spec),
    {HSecond, _} = ets:select(HRepaired),
    R = [
        First,
        Second,
        Again,
        tuple_size(C1),
        tuple_size(HC),
        length(HSecond),
        ets:repair_continuation('$end_of_table', Spec),
        ets:select('$end_of_table'),
        ets:match('$end_of_table'),
        ets:match_object('$end_of_table'),
        ets:select_reverse('$end_of_table'),
        attempt(fun() -> ets:select(id({bogus})) end),
        attempt(fun() -> ets:select(id(bogus)) end)
    ],
    ets:delete(T),
    ets:delete(H),
    R.

%% A hash table of 64 parts (write_concurrency): selects, chunks and edits cover every part.
concurrent() ->
    T = ets:new(t, [set, public, {write_concurrency, true}]),
    ets:insert(T, [{K, K rem 3} || K <- lists:seq(1, 200)]),
    Chunks = lists:droplast(chunks(ets:select(T, [{{'$1', 0}, [], ['$1']}], 7))),
    R = [
        length(ets:select(T, [{{'_', 0}, [], [true]}])),
        lists:usort([length(C) || C <- lists:droplast(Chunks)]),
        lists:sort(lists:append(Chunks)) =:= [K || K <- lists:seq(1, 200), K rem 3 =:= 0],
        ets:select_count(T, [{{'_', 1}, [], [true]}]),
        ets:select_delete(T, [{{'_', 2}, [], [true]}]),
        ets:select_replace(T, [{{'$1', 1}, [], [{{'$1', one}}]}]),
        ets:info(T, size),
        lists:usort([V || {_, V} <- ets:tab2list(T)])
    ],
    ets:delete(T),
    R.

%% Deleting during a fixed walk keeps the walk going over the rest.
fixed() ->
    T = filled(set),
    ets:safe_fixtable(T, true),
    First = ets:first(T),
    Deleted = ets:select_delete(T, [{{'$1', '_', '_'}, [{'=/=', '$1', First}], [true]}]),
    Next = ets:next(T, First),
    ets:safe_fixtable(T, false),
    R = [Deleted, Next, ets:tab2list(T) =:= [hd(ets:lookup(T, First))], ets:info(T, size)],
    ets:delete(T),
    R.

%% An exit's reason without the stack trace it carries.
reason({exit, {Reason, Stack}}) when is_list(Stack) -> {exit, Reason};
reason(Other) -> Other.

init_table() ->
    T = ets:new(t, [ordered_set]),
    ets:insert(T, {old, 0}),
    Input = fun
        F([]) ->
            fun
                (read) -> end_of_input;
                (close) -> ok
            end;
        F([Chunk | Rest]) ->
            fun
                (read) -> {Chunk, F(Rest)};
                (close) -> ok
            end
    end,
    R1 = ets:init_table(T, Input([[{a, 1}, {b, 2}], [], [{c, 3}]])),
    L1 = ets:tab2list(T),
    R2 = attempt(fun() -> ets:init_table(T, fun(read) -> bogus end) end),
    R3 = reason(
        attempt(fun() ->
            ets:init_table(T, fun(read) -> {[not_a_tuple], fun(_) -> end_of_input end} end)
        end)
    ),
    L2 = ets:tab2list(T),
    ets:delete(T),
    [R1, L1, R2, R3, L2].

errors() ->
    Private = spawn_table(),
    T = ets:new(t, []),
    R = [
        attempt(fun() -> ets:select(T, id(bad)) end),
        attempt(fun() -> ets:select(T, id([{a}])) end),
        attempt(fun() -> ets:select(T, [{'_', [], ['$1']}]) end),
        attempt(fun() -> ets:select(T, [{'_', [], [true]}], id(0)) end),
        attempt(fun() -> ets:select(T, [{'_', [], [true]}], id(a)) end),
        attempt(fun() -> ets:select(id(no_table), [{'_', [], [true]}]) end),
        attempt(fun() -> ets:select(Private, [{'_', [], [true]}]) end),
        attempt(fun() -> ets:select_count(T, id([{a}])) end),
        attempt(fun() -> ets:select_delete(T, id([{a}])) end),
        attempt(fun() -> ets:select_replace(T, [{{'$1', '_'}, [], [{{x, '$1'}}]}]) end),
        attempt(fun() -> ets:select_replace(T, [{{'$1', '_'}, [], [{{'$1', x}}, y]}]) end),
        attempt(fun() -> ets:select_replace(id(ets:new(b, [bag])), [{'_', [], ['$_']}]) end),
        attempt(fun() -> ets:match(T, '$1', id(-1)) end),
        attempt(fun() -> ets:test_ms({a}, id(bad)) end),
        attempt(fun() ->
            ets:match_spec_run(id(bad), ets:match_spec_compile([{'_', [], [true]}]))
        end)
    ],
    Private ! stop,
    ets:delete(T),
    R.

spawn_table() ->
    Self = self(),
    spawn(fun() ->
        T = ets:new(private, [private]),
        Self ! {table, T},
        receive
            stop -> ok
        end
    end),
    receive
        {table, T} -> T
    end.
