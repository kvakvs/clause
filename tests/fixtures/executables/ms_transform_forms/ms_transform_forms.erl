%% ms_transform over literal abstract forms (plan 21 step LIB72): each case is the forms of one function of a
%% snippet module, with its records, as epp gives them; the shell cases are the clauses of shell funs.
-module(ms_transform_forms).
-export([main/1]).

main(_) ->
    [transform(Name, Forms) || {Name, Forms} <- cases()],
    Bindings = [{'X', 5}, {'Y', <<"yz">>}],
    [
        io:format("shell: ~w~n", [
            exit_reason(fun() -> ms_transform:transform_from_shell(D, Cs, Bindings) end)
        ])
     || {D, Cs} <- shells()
    ],
    io:format("info: ~w~n", [ms_transform:parse_transform_info()]),
    Codes = [1, 2, 3, 4, 5, 16, 116, {6, "V"}, {7, r}, {8, r, f}, {9, r, f}, {10, '$1'}, {11, 'A'}],
    Generic = [
        {17, f, 1},
        {117, f, 1},
        {22, m, f, 1},
        {122, m, f, 1},
        {18, "case"},
        {118, "if"},
        {19, r, f}
    ],
    More = [
        {119, r, f},
        {20, r},
        {120, r},
        {21, r, f},
        {121, r, f},
        {23, 'A'},
        {123, 'A'},
        {24, '!'},
        {124, '!'}
    ],
    Unknown = [{50, 'X'}, 0, 17, {17, f}, {5, x}, {200, x}, nope, "text"],
    [
        io:format("error ~w: ~ts~n", [C, ms_transform:format_error(C)])
     || C <- Codes ++ Generic ++ More ++ Unknown
    ],
    io:format("bad forms: ~w~n", [
        exit_reason(fun() -> ms_transform:parse_transform(not_a_list, []) end)
    ]),
    io:format("ms_transform_forms: ok~n").

transform(Name, Forms) ->
    case ms_transform:parse_transform(Forms, []) of
        {error, [{File, [{Location, Module, Code}]}], []} ->
            io:format("~w: error ~ts ~w ~w ~w: ~ts~n", [
                Name, File, Location, Module, Code, Module:format_error(Code)
            ]);
        {warning, Transformed, Warnings} ->
            io:format("~w: ~w~n", [Name, lists:last(Transformed)]),
            [
                io:format("~w: warning ~ts ~w: ~ts~n", [Name, File, Location, M:format_error(W)])
             || {File, [{Location, M, W}]} <- Warnings
            ];
        Transformed ->
            io:format("~w: ~w~n", [Name, Transformed -- Forms])
    end.

exit_reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        exit:{Reason, _} -> {exit, Reason}
    end.

cases() ->
    [
        {heads, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {4, 1}, heads, 0, [
                {clause, {4, 1}, [], [], [
                    {cons, {5, 5},
                        {call, {5, 6},
                            {remote, {5, 9}, {atom, {5, 6}, ets}, {atom, {5, 10}, fun2ms}}, [
                                {'fun', {5, 17},
                                    {clauses, [
                                        {clause, {5, 20},
                                            [
                                                {tuple, {5, 21}, [
                                                    {var, {5, 22}, 'A'}, {var, {5, 25}, 'B'}
                                                ]}
                                            ],
                                            [
                                                [
                                                    {op, {5, 36}, '>', {var, {5, 34}, 'A'},
                                                        {integer, {5, 38}, 1}}
                                                ]
                                            ],
                                            [{var, {5, 43}, 'B'}]}
                                    ]}}
                            ]},
                        {cons, {6, 6},
                            {call, {6, 6},
                                {remote, {6, 9}, {atom, {6, 6}, ets}, {atom, {6, 10}, fun2ms}}, [
                                    {'fun', {6, 17},
                                        {clauses, [
                                            {clause, {6, 20}, [{var, {6, 21}, 'X'}], [], [
                                                {var, {6, 27}, 'X'}
                                            ]}
                                        ]}}
                                ]},
                            {cons, {7, 6},
                                {call, {7, 6},
                                    {remote, {7, 9}, {atom, {7, 6}, ets}, {atom, {7, 10}, fun2ms}},
                                    [
                                        {'fun', {7, 17},
                                            {clauses, [
                                                {clause, {7, 20},
                                                    [
                                                        {match, {7, 21},
                                                            {record, {7, 21}, r, [
                                                                {record_field, {7, 24},
                                                                    {atom, {7, 24}, a},
                                                                    {var, {7, 28}, 'A'}},
                                                                {record_field, {7, 31},
                                                                    {var, {7, 31}, '_'},
                                                                    {atom, {7, 35}, '$z'}}
                                                            ]},
                                                            {var, {7, 43}, 'R'}}
                                                    ],
                                                    [
                                                        [
                                                            {op, {7, 53}, '=:=',
                                                                {var, {7, 51}, 'A'},
                                                                {integer, {7, 57}, 1}}
                                                        ]
                                                    ],
                                                    [{var, {7, 62}, 'R'}]}
                                            ]}}
                                    ]},
                                {cons, {8, 6},
                                    {call, {8, 6},
                                        {remote, {8, 9}, {atom, {8, 6}, ets},
                                            {atom, {8, 10}, fun2ms}},
                                        [
                                            {'fun', {8, 17},
                                                {clauses, [
                                                    {clause, {8, 20},
                                                        [
                                                            {match, {8, 21},
                                                                {tuple, {8, 21}, [
                                                                    {var, {8, 22}, 'A'},
                                                                    {var, {8, 25}, 'B'},
                                                                    {var, {8, 28}, '_'}
                                                                ]},
                                                                {var, {8, 33}, 'O'}}
                                                        ],
                                                        [
                                                            [
                                                                {call, {8, 41},
                                                                    {atom, {8, 41}, is_integer}, [
                                                                        {var, {8, 52}, 'A'}
                                                                    ]}
                                                            ],
                                                            [
                                                                {call, {8, 56},
                                                                    {atom, {8, 56}, is_atom}, [
                                                                        {var, {8, 64}, 'B'}
                                                                    ]}
                                                            ]
                                                        ],
                                                        [
                                                            {tuple, {8, 70}, [
                                                                {var, {8, 71}, 'O'},
                                                                {var, {8, 74}, 'B'},
                                                                {atom, {8, 77}, '$x'}
                                                            ]}
                                                        ]}
                                                ]}}
                                        ]},
                                    {cons, {9, 6},
                                        {call, {9, 6},
                                            {remote, {9, 9}, {atom, {9, 6}, ets},
                                                {atom, {9, 10}, fun2ms}},
                                            [
                                                {'fun', {9, 17},
                                                    {clauses, [
                                                        {clause, {9, 20},
                                                            [
                                                                {match, {9, 21},
                                                                    {var, {9, 21}, 'O'},
                                                                    {tuple, {9, 25}, [
                                                                        {var, {9, 26}, 'X'},
                                                                        {var, {9, 29}, '_'}
                                                                    ]}}
                                                            ],
                                                            [], [
                                                                {tuple, {9, 36}, [
                                                                    {var, {9, 37}, 'O'},
                                                                    {var, {9, 40}, 'X'}
                                                                ]}
                                                            ]}
                                                    ]}}
                                            ]},
                                        {cons, {10, 6},
                                            {call, {10, 6},
                                                {remote, {10, 9}, {atom, {10, 6}, ets},
                                                    {atom, {10, 10}, fun2ms}},
                                                [
                                                    {'fun', {10, 17},
                                                        {clauses, [
                                                            {clause, {10, 20},
                                                                [
                                                                    {tuple, {10, 21}, [
                                                                        {map, {10, 22}, [
                                                                            {map_field_exact,
                                                                                {10, 26},
                                                                                {atom, {10, 24}, k},
                                                                                {var, {10, 29},
                                                                                    'V'}}
                                                                        ]},
                                                                        {var, {10, 33}, 'W'}
                                                                    ]}
                                                                ],
                                                                [], [{var, {10, 40}, 'V'}]}
                                                        ]}}
                                                ]},
                                            {cons, {11, 6},
                                                {call, {11, 6},
                                                    {remote, {11, 9}, {atom, {11, 6}, ets},
                                                        {atom, {11, 10}, fun2ms}},
                                                    [
                                                        {'fun', {11, 17},
                                                            {clauses, [
                                                                {clause, {11, 20},
                                                                    [
                                                                        {tuple, {11, 21}, [
                                                                            {var, {11, 22}, 'A'},
                                                                            {var, {11, 25}, 'A'}
                                                                        ]}
                                                                    ],
                                                                    [], [
                                                                        {call, {11, 32},
                                                                            {atom, {11, 32},
                                                                                object},
                                                                            []}
                                                                    ]}
                                                            ]}}
                                                    ]},
                                                {cons, {12, 6},
                                                    {call, {12, 6},
                                                        {remote, {12, 9}, {atom, {12, 6}, ets},
                                                            {atom, {12, 10}, fun2ms}},
                                                        [
                                                            {'fun', {12, 17},
                                                                {clauses, [
                                                                    {clause, {12, 20},
                                                                        [
                                                                            {record, {12, 21}, r, [
                                                                                {record_field,
                                                                                    {12, 24},
                                                                                    {atom, {12, 24},
                                                                                        c},
                                                                                    {var, {12, 28},
                                                                                        'C'}}
                                                                            ]}
                                                                        ],
                                                                        [], [
                                                                            {call, {12, 35},
                                                                                {atom, {12, 35},
                                                                                    bindings},
                                                                                []}
                                                                        ]}
                                                                ]}}
                                                        ]},
                                                    {nil, {12, 50}}}}}}}}}}
                ]}
            ]}
        ]},
        {bodies, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {13, 1}, bodies, 1, [
                {clause, {13, 1}, [{var, {13, 8}, 'K'}], [], [
                    {cons, {14, 5},
                        {call, {14, 6},
                            {remote, {14, 9}, {atom, {14, 6}, ets}, {atom, {14, 10}, fun2ms}}, [
                                {'fun', {14, 17},
                                    {clauses, [
                                        {clause, {14, 20},
                                            [
                                                {tuple, {14, 21}, [
                                                    {var, {14, 22}, 'A'}, {var, {14, 25}, 'B'}
                                                ]}
                                            ],
                                            [
                                                [
                                                    {op, {14, 36}, '>', {var, {14, 34}, 'A'},
                                                        {var, {14, 38}, 'K'}},
                                                    {op, {14, 43}, '=/=', {var, {14, 41}, 'B'},
                                                        {var, {14, 47}, 'K'}}
                                                ]
                                            ],
                                            [
                                                {tuple, {14, 52}, [
                                                    {var, {14, 53}, 'A'}, {var, {14, 56}, 'K'}
                                                ]}
                                            ]}
                                    ]}}
                            ]},
                        {cons, {15, 6},
                            {call, {15, 6},
                                {remote, {15, 9}, {atom, {15, 6}, ets}, {atom, {15, 10}, fun2ms}}, [
                                    {'fun', {15, 17},
                                        {clauses, [
                                            {clause, {15, 20},
                                                [
                                                    {tuple, {15, 21}, [
                                                        {var, {15, 22}, 'A'}, {var, {15, 25}, 'B'}
                                                    ]}
                                                ],
                                                [], [
                                                    {record, {15, 32}, r, [
                                                        {record_field, {15, 35},
                                                            {atom, {15, 35}, a},
                                                            {var, {15, 39}, 'A'}},
                                                        {record_field, {15, 42},
                                                            {atom, {15, 42}, b},
                                                            {var, {15, 46}, 'B'}}
                                                    ]}
                                                ]}
                                        ]}}
                                ]},
                            {cons, {16, 6},
                                {call, {16, 6},
                                    {remote, {16, 9}, {atom, {16, 6}, ets},
                                        {atom, {16, 10}, fun2ms}},
                                    [
                                        {'fun', {16, 17},
                                            {clauses, [
                                                {clause, {16, 20},
                                                    [
                                                        {tuple, {16, 21}, [
                                                            {var, {16, 22}, 'A'},
                                                            {var, {16, 25}, 'B'}
                                                        ]}
                                                    ],
                                                    [], [
                                                        {record, {16, 32}, r, [
                                                            {record_field, {16, 35},
                                                                {atom, {16, 35}, a},
                                                                {var, {16, 39}, 'A'}},
                                                            {record_field, {16, 42},
                                                                {var, {16, 42}, '_'},
                                                                {var, {16, 46}, 'B'}}
                                                        ]}
                                                    ]}
                                            ]}}
                                    ]},
                                {cons, {17, 6},
                                    {call, {17, 6},
                                        {remote, {17, 9}, {atom, {17, 6}, ets},
                                            {atom, {17, 10}, fun2ms}},
                                        [
                                            {'fun', {17, 17},
                                                {clauses, [
                                                    {clause, {17, 20},
                                                        [
                                                            {match, {17, 21}, {var, {17, 21}, 'R'},
                                                                {record, {17, 25}, r, []}}
                                                        ],
                                                        [], [
                                                            {record, {17, 35}, {var, {17, 34}, 'R'},
                                                                r, [
                                                                    {record_field, {17, 38},
                                                                        {atom, {17, 38}, c},
                                                                        {integer, {17, 42}, 3}}
                                                                ]}
                                                        ]}
                                                ]}}
                                        ]},
                                    {cons, {18, 6},
                                        {call, {18, 6},
                                            {remote, {18, 9}, {atom, {18, 6}, ets},
                                                {atom, {18, 10}, fun2ms}},
                                            [
                                                {'fun', {18, 17},
                                                    {clauses, [
                                                        {clause, {18, 20}, [{var, {18, 21}, 'R'}],
                                                            [
                                                                [
                                                                    {op, {18, 35}, '>',
                                                                        {record_field, {18, 30},
                                                                            {var, {18, 29}, 'R'}, r,
                                                                            {atom, {18, 33}, a}},
                                                                        {integer, {18, 37}, 1}},
                                                                    {call, {18, 40},
                                                                        {atom, {18, 40}, is_record},
                                                                        [
                                                                            {var, {18, 50}, 'R'},
                                                                            {atom, {18, 53}, r}
                                                                        ]}
                                                                ]
                                                            ],
                                                            [
                                                                {record_field, {18, 60},
                                                                    {var, {18, 59}, 'R'}, r,
                                                                    {atom, {18, 63}, b}}
                                                            ]}
                                                    ]}}
                                            ]},
                                        {cons, {19, 6},
                                            {call, {19, 6},
                                                {remote, {19, 9}, {atom, {19, 6}, ets},
                                                    {atom, {19, 10}, fun2ms}},
                                                [
                                                    {'fun', {19, 17},
                                                        {clauses, [
                                                            {clause, {19, 20},
                                                                [
                                                                    {tuple, {19, 21}, [
                                                                        {var, {19, 22}, 'A'}
                                                                    ]}
                                                                ],
                                                                [
                                                                    [
                                                                        {op, {19, 52}, '=:=',
                                                                            {call, {19, 31},
                                                                                {remote, {19, 37},
                                                                                    {atom, {19, 31},
                                                                                        erlang},
                                                                                    {atom, {19, 38},
                                                                                        element}},
                                                                                [
                                                                                    {integer,
                                                                                        {19, 46},
                                                                                        1},
                                                                                    {var, {19, 49},
                                                                                        'A'}
                                                                                ]},
                                                                            {integer, {19, 56}, 1}},
                                                                        {op, {19, 76}, '>',
                                                                            {call, {19, 59},
                                                                                {remote, {19, 65},
                                                                                    {atom, {19, 59},
                                                                                        erlang},
                                                                                    {atom, {19, 66},
                                                                                        '+'}},
                                                                                [
                                                                                    {var, {19, 70},
                                                                                        'A'},
                                                                                    {integer,
                                                                                        {19, 73}, 1}
                                                                                ]},
                                                                            {integer, {19, 78}, 2}}
                                                                    ]
                                                                ],
                                                                [
                                                                    {call, {19, 83},
                                                                        {remote, {19, 89},
                                                                            {atom, {19, 83},
                                                                                erlang},
                                                                            {atom, {19, 90}, abs}},
                                                                        [{var, {19, 94}, 'A'}]}
                                                                ]}
                                                        ]}}
                                                ]},
                                            {cons, {20, 6},
                                                {call, {20, 6},
                                                    {remote, {20, 9}, {atom, {20, 6}, ets},
                                                        {atom, {20, 10}, fun2ms}},
                                                    [
                                                        {'fun', {20, 17},
                                                            {clauses, [
                                                                {clause, {20, 20},
                                                                    [
                                                                        {tuple, {20, 21}, [
                                                                            {var, {20, 22}, 'A'}
                                                                        ]}
                                                                    ],
                                                                    [
                                                                        [
                                                                            {call, {20, 31},
                                                                                {atom, {20, 31},
                                                                                    atom},
                                                                                [
                                                                                    {var, {20, 36},
                                                                                        'A'}
                                                                                ]}
                                                                        ],
                                                                        [
                                                                            {call, {20, 40},
                                                                                {atom, {20, 40},
                                                                                    integer},
                                                                                [
                                                                                    {var, {20, 48},
                                                                                        'A'}
                                                                                ]}
                                                                        ]
                                                                    ],
                                                                    [
                                                                        {cons, {20, 54},
                                                                            {var, {20, 55}, 'A'},
                                                                            {cons, {20, 58},
                                                                                {op, {20, 60}, '+',
                                                                                    {integer,
                                                                                        {20, 58},
                                                                                        1},
                                                                                    {integer,
                                                                                        {20, 62},
                                                                                        2}},
                                                                                {cons, {20, 65},
                                                                                    {op, {20, 65},
                                                                                        '-',
                                                                                        {integer,
                                                                                            {20,
                                                                                                66},
                                                                                            1}},
                                                                                    {cons, {20, 69},
                                                                                        {op,
                                                                                            {20,
                                                                                                71},
                                                                                            '+',
                                                                                            {atom,
                                                                                                {20,
                                                                                                    69},
                                                                                                x},
                                                                                            {integer,
                                                                                                {20,
                                                                                                    73},
                                                                                                1}},
                                                                                        {cons,
                                                                                            {20,
                                                                                                76},
                                                                                            {cons,
                                                                                                {20,
                                                                                                    76},
                                                                                                {var,
                                                                                                    {20,
                                                                                                        77},
                                                                                                    'A'},
                                                                                                {var,
                                                                                                    {20,
                                                                                                        81},
                                                                                                    'A'}},
                                                                                            {cons,
                                                                                                {20,
                                                                                                    85},
                                                                                                {tuple,
                                                                                                    {20,
                                                                                                        85},
                                                                                                    []},
                                                                                                {nil,
                                                                                                    {20,
                                                                                                        87}}}}}}}}
                                                                    ]}
                                                            ]}}
                                                    ]},
                                                {cons, {21, 6},
                                                    {call, {21, 6},
                                                        {remote, {21, 9}, {atom, {21, 6}, ets},
                                                            {atom, {21, 10}, fun2ms}},
                                                        [
                                                            {'fun', {21, 17},
                                                                {clauses, [
                                                                    {clause, {21, 20},
                                                                        [
                                                                            {tuple, {21, 21}, [
                                                                                {var, {21, 22}, 'A'}
                                                                            ]}
                                                                        ],
                                                                        [], [
                                                                            {record_index, {21, 29},
                                                                                r,
                                                                                {atom, {21, 32}, b}}
                                                                        ]}
                                                                ]}}
                                                        ]},
                                                    {cons, {22, 6},
                                                        {call, {22, 6},
                                                            {remote, {22, 9}, {atom, {22, 6}, ets},
                                                                {atom, {22, 10}, fun2ms}},
                                                            [
                                                                {'fun', {22, 17},
                                                                    {clauses, [
                                                                        {clause, {22, 20},
                                                                            [
                                                                                {tuple, {22, 21}, [
                                                                                    {var, {22, 22},
                                                                                        'A'}
                                                                                ]}
                                                                            ],
                                                                            [], [
                                                                                {bin, {22, 29}, [
                                                                                    {bin_element,
                                                                                        {22, 31},
                                                                                        {var,
                                                                                            {22,
                                                                                                31},
                                                                                            'K'},
                                                                                        {integer,
                                                                                            {22,
                                                                                                33},
                                                                                            8},
                                                                                        default},
                                                                                    {bin_element,
                                                                                        {22, 36},
                                                                                        {integer,
                                                                                            {22,
                                                                                                36},
                                                                                            1},
                                                                                        {integer,
                                                                                            {22,
                                                                                                38},
                                                                                            16},
                                                                                        default}
                                                                                ]}
                                                                            ]}
                                                                    ]}}
                                                            ]},
                                                        {cons, {23, 6},
                                                            {call, {23, 6},
                                                                {remote, {23, 9},
                                                                    {atom, {23, 6}, ets},
                                                                    {atom, {23, 10}, fun2ms}},
                                                                [
                                                                    {'fun', {23, 17},
                                                                        {clauses, [
                                                                            {clause, {23, 20},
                                                                                [
                                                                                    {tuple,
                                                                                        {23, 21}, [
                                                                                            {var,
                                                                                                {23,
                                                                                                    22},
                                                                                                'A'}
                                                                                        ]}
                                                                                ],
                                                                                [], [
                                                                                    {map, {23, 29},
                                                                                        [
                                                                                            {map_field_assoc,
                                                                                                {23,
                                                                                                    33},
                                                                                                {atom,
                                                                                                    {23,
                                                                                                        31},
                                                                                                    a},
                                                                                                {var,
                                                                                                    {23,
                                                                                                        36},
                                                                                                    'A'}},
                                                                                            {map_field_assoc,
                                                                                                {23,
                                                                                                    41},
                                                                                                {atom,
                                                                                                    {23,
                                                                                                        39},
                                                                                                    b},
                                                                                                {var,
                                                                                                    {23,
                                                                                                        44},
                                                                                                    'K'}}
                                                                                        ]}
                                                                                ]}
                                                                        ]}}
                                                                ]},
                                                            {cons, {24, 6},
                                                                {call, {24, 6},
                                                                    {remote, {24, 9},
                                                                        {atom, {24, 6}, ets},
                                                                        {atom, {24, 10}, fun2ms}},
                                                                    [
                                                                        {'fun', {24, 17},
                                                                            {clauses, [
                                                                                {clause, {24, 20},
                                                                                    [
                                                                                        {tuple,
                                                                                            {24,
                                                                                                21},
                                                                                            [
                                                                                                {var,
                                                                                                    {24,
                                                                                                        22},
                                                                                                    'A'}
                                                                                            ]}
                                                                                    ],
                                                                                    [
                                                                                        [
                                                                                            {op,
                                                                                                {24,
                                                                                                    43},
                                                                                                'andalso',
                                                                                                {op,
                                                                                                    {24,
                                                                                                        31},
                                                                                                    'not',
                                                                                                    {op,
                                                                                                        {24,
                                                                                                            38},
                                                                                                        '>',
                                                                                                        {var,
                                                                                                            {24,
                                                                                                                36},
                                                                                                            'A'},
                                                                                                        {integer,
                                                                                                            {24,
                                                                                                                40},
                                                                                                            1}}},
                                                                                                {atom,
                                                                                                    {24,
                                                                                                        51},
                                                                                                    true}}
                                                                                        ]
                                                                                    ],
                                                                                    [
                                                                                        {tuple,
                                                                                            {24,
                                                                                                59},
                                                                                            [
                                                                                                {atom,
                                                                                                    {24,
                                                                                                        60},
                                                                                                    const},
                                                                                                {var,
                                                                                                    {24,
                                                                                                        67},
                                                                                                    'A'}
                                                                                            ]}
                                                                                    ]}
                                                                            ]}}
                                                                    ]},
                                                                {cons, {25, 6},
                                                                    {call, {25, 6},
                                                                        {remote, {25, 9},
                                                                            {atom, {25, 6}, ets},
                                                                            {atom, {25, 10},
                                                                                fun2ms}},
                                                                        [
                                                                            {'fun', {25, 17},
                                                                                {clauses, [
                                                                                    {clause,
                                                                                        {25, 20},
                                                                                        [
                                                                                            {tuple,
                                                                                                {25,
                                                                                                    21},
                                                                                                [
                                                                                                    {var,
                                                                                                        {25,
                                                                                                            22},
                                                                                                        'A'}
                                                                                                ]}
                                                                                        ],
                                                                                        [], [
                                                                                            {call,
                                                                                                {25,
                                                                                                    29},
                                                                                                {atom,
                                                                                                    {25,
                                                                                                        29},
                                                                                                    message},
                                                                                                [
                                                                                                    {var,
                                                                                                        {25,
                                                                                                            37},
                                                                                                        'A'}
                                                                                                ]},
                                                                                            {call,
                                                                                                {25,
                                                                                                    41},
                                                                                                {atom,
                                                                                                    {25,
                                                                                                        41},
                                                                                                    return_trace},
                                                                                                []}
                                                                                        ]}
                                                                                ]}}
                                                                        ]},
                                                                    {cons, {26, 6},
                                                                        {call, {26, 6},
                                                                            {remote, {26, 9},
                                                                                {atom, {26, 6},
                                                                                    dbg},
                                                                                {atom, {26, 10},
                                                                                    fun2ms}},
                                                                            [
                                                                                {'fun', {26, 17},
                                                                                    {clauses, [
                                                                                        {clause,
                                                                                            {26,
                                                                                                20},
                                                                                            [
                                                                                                {cons,
                                                                                                    {26,
                                                                                                        21},
                                                                                                    {var,
                                                                                                        {26,
                                                                                                            22},
                                                                                                        'A'},
                                                                                                    {cons,
                                                                                                        {26,
                                                                                                            25},
                                                                                                        {var,
                                                                                                            {26,
                                                                                                                25},
                                                                                                            'B'},
                                                                                                        {nil,
                                                                                                            {26,
                                                                                                                26}}}}
                                                                                            ],
                                                                                            [], [
                                                                                                {call,
                                                                                                    {26,
                                                                                                        32},
                                                                                                    {atom,
                                                                                                        {26,
                                                                                                            32},
                                                                                                        message},
                                                                                                    [
                                                                                                        {var,
                                                                                                            {26,
                                                                                                                40},
                                                                                                            'A'}
                                                                                                    ]}
                                                                                            ]}
                                                                                    ]}}
                                                                            ]},
                                                                        {cons, {27, 6},
                                                                            {call, {27, 6},
                                                                                {remote, {27, 9},
                                                                                    {atom, {27, 6},
                                                                                        dbg},
                                                                                    {atom, {27, 10},
                                                                                        fun2ms}},
                                                                                [
                                                                                    {'fun',
                                                                                        {27, 17},
                                                                                        {clauses, [
                                                                                            {clause,
                                                                                                {27,
                                                                                                    20},
                                                                                                [
                                                                                                    {nil,
                                                                                                        {27,
                                                                                                            21}}
                                                                                                ],
                                                                                                [],
                                                                                                [
                                                                                                    {atom,
                                                                                                        {27,
                                                                                                            28},
                                                                                                        true}
                                                                                                ]}
                                                                                        ]}}
                                                                                ]},
                                                                            {cons, {28, 6},
                                                                                {call, {28, 6},
                                                                                    {remote,
                                                                                        {28, 9},
                                                                                        {atom,
                                                                                            {28, 6},
                                                                                            dbg},
                                                                                        {atom,
                                                                                            {28,
                                                                                                10},
                                                                                            fun2ms}},
                                                                                    [
                                                                                        {'fun',
                                                                                            {28,
                                                                                                17},
                                                                                            {clauses,
                                                                                                [
                                                                                                    {clause,
                                                                                                        {28,
                                                                                                            20},
                                                                                                        [
                                                                                                            {var,
                                                                                                                {28,
                                                                                                                    21},
                                                                                                                'Args'}
                                                                                                        ],
                                                                                                        [
                                                                                                            [
                                                                                                                {op,
                                                                                                                    {28,
                                                                                                                        45},
                                                                                                                    '>',
                                                                                                                    {call,
                                                                                                                        {28,
                                                                                                                            32},
                                                                                                                        {atom,
                                                                                                                            {28,
                                                                                                                                32},
                                                                                                                            length},
                                                                                                                        [
                                                                                                                            {var,
                                                                                                                                {28,
                                                                                                                                    39},
                                                                                                                                'Args'}
                                                                                                                        ]},
                                                                                                                    {integer,
                                                                                                                        {28,
                                                                                                                            47},
                                                                                                                        1}}
                                                                                                            ]
                                                                                                        ],
                                                                                                        [
                                                                                                            {call,
                                                                                                                {28,
                                                                                                                    52},
                                                                                                                {atom,
                                                                                                                    {28,
                                                                                                                        52},
                                                                                                                    caller},
                                                                                                                []}
                                                                                                        ]}
                                                                                                ]}}
                                                                                    ]},
                                                                                {nil, {28, 65}}}}}}}}}}}}}}}}}
                ]}
            ]}
        ]},
        {scopes, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {29, 1}, scopes, 1, [
                {clause, {29, 1}, [{var, {29, 8}, 'X'}], [], [
                    {match, {30, 5}, {var, {30, 5}, 'Y'}, {integer, {30, 9}, 1}},
                    {match, {31, 5}, {var, {31, 5}, 'F'},
                        {'fun', {31, 9},
                            {clauses, [
                                {clause, {31, 12}, [{var, {31, 13}, 'Z'}], [], [
                                    {call, {31, 19},
                                        {remote, {31, 22}, {atom, {31, 19}, ets},
                                            {atom, {31, 23}, fun2ms}},
                                        [
                                            {'fun', {31, 30},
                                                {clauses, [
                                                    {clause, {31, 33},
                                                        [
                                                            {tuple, {31, 34}, [
                                                                {var, {31, 35}, 'Z'},
                                                                {var, {31, 38}, 'W'}
                                                            ]}
                                                        ],
                                                        [], [{var, {31, 45}, 'W'}]}
                                                ]}}
                                        ]}
                                ]}
                            ]}}},
                    {'case', {32, 5}, {var, {32, 10}, 'X'}, [
                        {clause, {33, 9},
                            [{tuple, {33, 9}, [{var, {33, 10}, 'P'}, {var, {33, 13}, 'Q'}]}], [], [
                                {var, {33, 19}, 'Q'}
                            ]},
                        {clause, {34, 9}, [{var, {34, 9}, 'P'}], [], [{var, {34, 14}, 'P'}]}
                    ]},
                    {cons, {36, 5}, {var, {36, 6}, 'F'},
                        {cons, {36, 9},
                            {call, {36, 9},
                                {remote, {36, 12}, {atom, {36, 9}, ets}, {atom, {36, 13}, fun2ms}},
                                [
                                    {'fun', {36, 20},
                                        {clauses, [
                                            {clause, {36, 23},
                                                [
                                                    {tuple, {36, 24}, [
                                                        {var, {36, 25}, 'X'},
                                                        {var, {36, 28}, 'Y'},
                                                        {var, {36, 31}, 'P'}
                                                    ]}
                                                ],
                                                [], [{var, {36, 38}, 'X'}]}
                                        ]}}
                                ]},
                            {nil, {36, 44}}}}
                ]}
            ]}
        ]},
        {nofun, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {37, 1}, nofun, 1, [
                {clause, {37, 1}, [{var, {37, 7}, 'F'}], [], [
                    {call, {37, 13},
                        {remote, {37, 16}, {atom, {37, 13}, ets}, {atom, {37, 17}, fun2ms}}, [
                            {var, {37, 24}, 'F'}
                        ]}
                ]}
            ]}
        ]},
        {ets_head, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {38, 1}, ets_head, 0, [
                {clause, {38, 1}, [], [], [
                    {call, {38, 15},
                        {remote, {38, 18}, {atom, {38, 15}, ets}, {atom, {38, 19}, fun2ms}}, [
                            {'fun', {38, 26},
                                {clauses, [
                                    {clause, {38, 29},
                                        [{cons, {38, 30}, {var, {38, 31}, 'A'}, {nil, {38, 32}}}],
                                        [], [{var, {38, 38}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {dbg_head, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {39, 1}, dbg_head, 0, [
                {clause, {39, 1}, [], [], [
                    {call, {39, 15},
                        {remote, {39, 18}, {atom, {39, 15}, dbg}, {atom, {39, 19}, fun2ms}}, [
                            {'fun', {39, 26},
                                {clauses, [
                                    {clause, {39, 29}, [{tuple, {39, 30}, [{var, {39, 31}, 'A'}]}],
                                        [], [{var, {39, 38}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_match, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {40, 1}, head_match, 0, [
                {clause, {40, 1}, [], [], [
                    {call, {40, 17},
                        {remote, {40, 20}, {atom, {40, 17}, ets}, {atom, {40, 21}, fun2ms}}, [
                            {'fun', {40, 28},
                                {clauses, [
                                    {clause, {40, 31},
                                        [
                                            {tuple, {40, 32}, [
                                                {match, {40, 33}, {var, {40, 33}, 'A'},
                                                    {tuple, {40, 37}, [{var, {40, 38}, 'B'}]}}
                                            ]}
                                        ],
                                        [], [{var, {40, 46}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {guard_match, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {41, 1}, guard_match, 0, [
                {clause, {41, 1}, [], [], [
                    {call, {41, 18},
                        {remote, {41, 21}, {atom, {41, 18}, ets}, {atom, {41, 22}, fun2ms}}, [
                            {'fun', {41, 29},
                                {clauses, [
                                    {clause, {41, 32}, [{tuple, {41, 33}, [{var, {41, 34}, 'A'}]}],
                                        [
                                            [
                                                {match, {41, 44}, {var, {41, 44}, 'A'},
                                                    {integer, {41, 48}, 1}}
                                            ]
                                        ],
                                        [{var, {41, 54}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_match, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {42, 1}, body_match, 0, [
                {clause, {42, 1}, [], [], [
                    {call, {42, 17},
                        {remote, {42, 20}, {atom, {42, 17}, ets}, {atom, {42, 21}, fun2ms}}, [
                            {'fun', {42, 28},
                                {clauses, [
                                    {clause, {42, 31}, [{tuple, {42, 32}, [{var, {42, 33}, 'A'}]}],
                                        [], [
                                            {match, {42, 40}, {var, {42, 40}, 'B'},
                                                {var, {42, 44}, 'A'}}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {guard_local, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {43, 1}, guard_local, 0, [
                {clause, {43, 1}, [], [], [
                    {call, {43, 18},
                        {remote, {43, 21}, {atom, {43, 18}, ets}, {atom, {43, 22}, fun2ms}}, [
                            {'fun', {43, 29},
                                {clauses, [
                                    {clause, {43, 32}, [{tuple, {43, 33}, [{var, {43, 34}, 'A'}]}],
                                        [
                                            [
                                                {call, {43, 43}, {atom, {43, 43}, foo}, [
                                                    {var, {43, 47}, 'A'}
                                                ]}
                                            ]
                                        ],
                                        [{var, {43, 53}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_local, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {44, 1}, body_local, 0, [
                {clause, {44, 1}, [], [], [
                    {call, {44, 17},
                        {remote, {44, 20}, {atom, {44, 17}, ets}, {atom, {44, 21}, fun2ms}}, [
                            {'fun', {44, 28},
                                {clauses, [
                                    {clause, {44, 31}, [{tuple, {44, 32}, [{var, {44, 33}, 'A'}]}],
                                        [], [
                                            {call, {44, 40}, {atom, {44, 40}, foo}, [
                                                {var, {44, 44}, 'A'}
                                            ]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {guard_remote, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {45, 1}, guard_remote, 0, [
                {clause, {45, 1}, [], [], [
                    {call, {45, 19},
                        {remote, {45, 22}, {atom, {45, 19}, ets}, {atom, {45, 23}, fun2ms}}, [
                            {'fun', {45, 30},
                                {clauses, [
                                    {clause, {45, 33}, [{tuple, {45, 34}, [{var, {45, 35}, 'A'}]}],
                                        [
                                            [
                                                {call, {45, 44},
                                                    {remote, {45, 49}, {atom, {45, 44}, lists},
                                                        {atom, {45, 50}, foo}},
                                                    [{var, {45, 54}, 'A'}]}
                                            ]
                                        ],
                                        [{var, {45, 60}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_remote, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {46, 1}, body_remote, 0, [
                {clause, {46, 1}, [], [], [
                    {call, {46, 18},
                        {remote, {46, 21}, {atom, {46, 18}, ets}, {atom, {46, 22}, fun2ms}}, [
                            {'fun', {46, 29},
                                {clauses, [
                                    {clause, {46, 32}, [{tuple, {46, 33}, [{var, {46, 34}, 'A'}]}],
                                        [], [
                                            {call, {46, 41},
                                                {remote, {46, 47}, {atom, {46, 41}, erlang},
                                                    {atom, {46, 48}, spawn}},
                                                [{var, {46, 54}, 'A'}]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_case, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {47, 1}, body_case, 0, [
                {clause, {47, 1}, [], [], [
                    {call, {47, 16},
                        {remote, {47, 19}, {atom, {47, 16}, ets}, {atom, {47, 20}, fun2ms}}, [
                            {'fun', {47, 27},
                                {clauses, [
                                    {clause, {47, 30}, [{tuple, {47, 31}, [{var, {47, 32}, 'A'}]}],
                                        [], [
                                            {'case', {47, 39}, {var, {47, 44}, 'A'}, [
                                                {clause, {47, 49}, [{var, {47, 49}, '_'}], [], [
                                                    {var, {47, 54}, 'A'}
                                                ]}
                                            ]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {guard_lc, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {48, 1}, guard_lc, 0, [
                {clause, {48, 1}, [], [], [
                    {call, {48, 15},
                        {remote, {48, 18}, {atom, {48, 15}, ets}, {atom, {48, 19}, fun2ms}}, [
                            {'fun', {48, 26},
                                {clauses, [
                                    {clause, {48, 29}, [{tuple, {48, 30}, [{var, {48, 31}, 'A'}]}],
                                        [
                                            [
                                                {lc, {48, 40}, {var, {48, 41}, 'X'}, [
                                                    {generate, {48, 48}, {var, {48, 46}, 'X'},
                                                        {var, {48, 51}, 'A'}}
                                                ]}
                                            ]
                                        ],
                                        [{var, {48, 57}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_bad_record, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {49, 1}, head_bad_record, 0, [
                {clause, {49, 1}, [], [], [
                    {call, {49, 22},
                        {remote, {49, 25}, {atom, {49, 22}, ets}, {atom, {49, 26}, fun2ms}}, [
                            {'fun', {49, 33},
                                {clauses, [
                                    {clause, {49, 36},
                                        [
                                            {record, {49, 37}, nope, [
                                                {record_field, {49, 43}, {atom, {49, 43}, a},
                                                    {var, {49, 47}, 'A'}}
                                            ]}
                                        ],
                                        [], [{var, {49, 54}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_bad_field, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {50, 1}, head_bad_field, 0, [
                {clause, {50, 1}, [], [], [
                    {call, {50, 21},
                        {remote, {50, 24}, {atom, {50, 21}, ets}, {atom, {50, 25}, fun2ms}}, [
                            {'fun', {50, 32},
                                {clauses, [
                                    {clause, {50, 35},
                                        [
                                            {record, {50, 36}, r, [
                                                {record_field, {50, 39}, {atom, {50, 39}, d},
                                                    {var, {50, 43}, 'A'}}
                                            ]}
                                        ],
                                        [], [{var, {50, 50}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_multi_field, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {51, 1}, head_multi_field, 0, [
                {clause, {51, 1}, [], [], [
                    {call, {51, 23},
                        {remote, {51, 26}, {atom, {51, 23}, ets}, {atom, {51, 27}, fun2ms}}, [
                            {'fun', {51, 34},
                                {clauses, [
                                    {clause, {51, 37},
                                        [
                                            {record, {51, 38}, r, [
                                                {record_field, {51, 41}, {atom, {51, 41}, a},
                                                    {var, {51, 45}, 'A'}},
                                                {record_field, {51, 48}, {atom, {51, 48}, a},
                                                    {var, {51, 52}, 'B'}}
                                            ]}
                                        ],
                                        [], [{var, {51, 59}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_multi_default, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {52, 1}, head_multi_default, 0, [
                {clause, {52, 1}, [], [], [
                    {call, {52, 25},
                        {remote, {52, 28}, {atom, {52, 25}, ets}, {atom, {52, 29}, fun2ms}}, [
                            {'fun', {52, 36},
                                {clauses, [
                                    {clause, {52, 39},
                                        [
                                            {record, {52, 40}, r, [
                                                {record_field, {52, 43}, {var, {52, 43}, '_'},
                                                    {var, {52, 47}, 'A'}},
                                                {record_field, {52, 50}, {var, {52, 50}, '_'},
                                                    {var, {52, 54}, 'B'}}
                                            ]}
                                        ],
                                        [], [{var, {52, 61}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_dollar, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {53, 1}, head_dollar, 0, [
                {clause, {53, 1}, [], [], [
                    {call, {53, 18},
                        {remote, {53, 21}, {atom, {53, 18}, ets}, {atom, {53, 22}, fun2ms}}, [
                            {'fun', {53, 29},
                                {clauses, [
                                    {clause, {53, 32},
                                        [{tuple, {53, 33}, [{atom, {53, 34}, '$1'}]}], [], [
                                            {integer, {53, 44}, 1}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_dollar_sign, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {54, 1}, head_dollar_sign, 0, [
                {clause, {54, 1}, [], [], [
                    {call, {54, 23},
                        {remote, {54, 26}, {atom, {54, 23}, ets}, {atom, {54, 27}, fun2ms}}, [
                            {'fun', {54, 34},
                                {clauses, [
                                    {clause, {54, 37},
                                        [{tuple, {54, 38}, [{atom, {54, 39}, '$-3'}]}], [], [
                                            {integer, {54, 50}, 1}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_binary, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {55, 1}, head_binary, 0, [
                {clause, {55, 1}, [], [], [
                    {call, {55, 18},
                        {remote, {55, 21}, {atom, {55, 18}, ets}, {atom, {55, 22}, fun2ms}}, [
                            {'fun', {55, 29},
                                {clauses, [
                                    {clause, {55, 32},
                                        [
                                            {tuple, {55, 33}, [
                                                {bin, {55, 34}, [
                                                    {bin_element, {55, 36}, {var, {55, 36}, 'A'},
                                                        {integer, {55, 38}, 8}, default}
                                                ]}
                                            ]}
                                        ],
                                        [], [{var, {55, 47}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {guard_bad_record, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {56, 1}, guard_bad_record, 0, [
                {clause, {56, 1}, [], [], [
                    {call, {56, 23},
                        {remote, {56, 26}, {atom, {56, 23}, ets}, {atom, {56, 27}, fun2ms}}, [
                            {'fun', {56, 34},
                                {clauses, [
                                    {clause, {56, 37}, [{tuple, {56, 38}, [{var, {56, 39}, 'A'}]}],
                                        [
                                            [
                                                {call, {56, 48}, {atom, {56, 48}, is_record}, [
                                                    {var, {56, 58}, 'A'}, {atom, {56, 61}, nope}
                                                ]}
                                            ]
                                        ],
                                        [{var, {56, 70}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {guard_bad_field, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {57, 1}, guard_bad_field, 0, [
                {clause, {57, 1}, [], [], [
                    {call, {57, 22},
                        {remote, {57, 25}, {atom, {57, 22}, ets}, {atom, {57, 26}, fun2ms}}, [
                            {'fun', {57, 33},
                                {clauses, [
                                    {clause, {57, 36}, [{tuple, {57, 37}, [{var, {57, 38}, 'A'}]}],
                                        [
                                            [
                                                {op, {57, 53}, '=:=',
                                                    {record_field, {57, 48}, {var, {57, 47}, 'A'},
                                                        r, {atom, {57, 51}, d}},
                                                    {integer, {57, 57}, 1}}
                                            ]
                                        ],
                                        [{var, {57, 62}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_bad_record, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {58, 1}, body_bad_record, 0, [
                {clause, {58, 1}, [], [], [
                    {call, {58, 22},
                        {remote, {58, 25}, {atom, {58, 22}, ets}, {atom, {58, 26}, fun2ms}}, [
                            {'fun', {58, 33},
                                {clauses, [
                                    {clause, {58, 36}, [{tuple, {58, 37}, [{var, {58, 38}, 'A'}]}],
                                        [], [
                                            {record, {58, 45}, nope, [
                                                {record_field, {58, 51}, {atom, {58, 51}, a},
                                                    {var, {58, 55}, 'A'}}
                                            ]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_multi_field, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {59, 1}, body_multi_field, 0, [
                {clause, {59, 1}, [], [], [
                    {call, {59, 23},
                        {remote, {59, 26}, {atom, {59, 23}, ets}, {atom, {59, 27}, fun2ms}}, [
                            {'fun', {59, 34},
                                {clauses, [
                                    {clause, {59, 37}, [{tuple, {59, 38}, [{var, {59, 39}, 'A'}]}],
                                        [], [
                                            {record, {59, 46}, r, [
                                                {record_field, {59, 49}, {atom, {59, 49}, a},
                                                    {var, {59, 53}, 'A'}},
                                                {record_field, {59, 56}, {atom, {59, 56}, a},
                                                    {var, {59, 60}, 'A'}}
                                            ]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_bad_field, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {60, 1}, body_bad_field, 0, [
                {clause, {60, 1}, [], [], [
                    {call, {60, 21},
                        {remote, {60, 24}, {atom, {60, 21}, ets}, {atom, {60, 25}, fun2ms}}, [
                            {'fun', {60, 32},
                                {clauses, [
                                    {clause, {60, 35}, [{tuple, {60, 36}, [{var, {60, 37}, 'A'}]}],
                                        [], [
                                            {record, {60, 44}, r, [
                                                {record_field, {60, 47}, {atom, {60, 47}, d},
                                                    {var, {60, 51}, 'A'}}
                                            ]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_bad_index, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {61, 1}, body_bad_index, 0, [
                {clause, {61, 1}, [], [], [
                    {call, {61, 21},
                        {remote, {61, 24}, {atom, {61, 21}, ets}, {atom, {61, 25}, fun2ms}}, [
                            {'fun', {61, 32},
                                {clauses, [
                                    {clause, {61, 35}, [{tuple, {61, 36}, [{var, {61, 37}, 'A'}]}],
                                        [], [{record_index, {61, 44}, r, {atom, {61, 47}, d}}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_binary, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {62, 1}, body_binary, 0, [
                {clause, {62, 1}, [], [], [
                    {call, {62, 18},
                        {remote, {62, 21}, {atom, {62, 18}, ets}, {atom, {62, 22}, fun2ms}}, [
                            {'fun', {62, 29},
                                {clauses, [
                                    {clause, {62, 32}, [{tuple, {62, 33}, [{var, {62, 34}, 'A'}]}],
                                        [], [
                                            {bin, {62, 41}, [
                                                {bin_element, {62, 43}, {var, {62, 43}, 'A'},
                                                    {integer, {62, 45}, 8}, default}
                                            ]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {body_fun, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {63, 1}, body_fun, 0, [
                {clause, {63, 1}, [], [], [
                    {call, {63, 15},
                        {remote, {63, 18}, {atom, {63, 15}, ets}, {atom, {63, 19}, fun2ms}}, [
                            {'fun', {63, 26},
                                {clauses, [
                                    {clause, {63, 29}, [{tuple, {63, 30}, [{var, {63, 31}, 'A'}]}],
                                        [], [
                                            {'fun', {63, 38},
                                                {clauses, [
                                                    {clause, {63, 41}, [], [], [
                                                        {var, {63, 47}, 'A'}
                                                    ]}
                                                ]}}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_map, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {64, 1}, head_map, 0, [
                {clause, {64, 1}, [], [], [
                    {call, {64, 15},
                        {remote, {64, 18}, {atom, {64, 15}, ets}, {atom, {64, 19}, fun2ms}}, [
                            {'fun', {64, 26},
                                {clauses, [
                                    {clause, {64, 29},
                                        [
                                            {map, {64, 30}, [
                                                {map_field_exact, {64, 34}, {atom, {64, 32}, k},
                                                    {var, {64, 37}, 'V'}}
                                            ]}
                                        ],
                                        [], [{var, {64, 44}, 'V'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {head_pair_match, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {65, 1}, head_pair_match, 0, [
                {clause, {65, 1}, [], [], [
                    {call, {65, 22},
                        {remote, {65, 25}, {atom, {65, 22}, ets}, {atom, {65, 26}, fun2ms}}, [
                            {'fun', {65, 33},
                                {clauses, [
                                    {clause, {65, 36},
                                        [
                                            {match, {65, 37},
                                                {tuple, {65, 37}, [
                                                    {var, {65, 38}, 'X'}, {var, {65, 41}, 'Y'}
                                                ]},
                                                {tuple, {65, 46}, [
                                                    {var, {65, 47}, '_'}, {var, {65, 50}, 'Z'}
                                                ]}}
                                        ],
                                        [], [
                                            {tuple, {65, 57}, [
                                                {var, {65, 58}, 'X'},
                                                {var, {65, 61}, 'Y'},
                                                {var, {65, 64}, 'Z'}
                                            ]}
                                        ]}
                                ]}}
                        ]}
                ]}
            ]}
        ]},
        {two_params, [
            {attribute, {1, 1}, file, {
                [115, 110, 105, 112, 112, 101, 116, 115, 46, 101, 114, 108], 1
            }},
            {attribute, {2, 2}, record,
                {r, [
                    {record_field, {2, 13}, {atom, {2, 13}, a}},
                    {record_field, {2, 16}, {atom, {2, 16}, b}, {integer, {2, 20}, 2}},
                    {record_field, {2, 23}, {atom, {2, 23}, c}}
                ]}},
            {attribute, {3, 2}, record, {s, [{record_field, {3, 13}, {atom, {3, 13}, x}}]}},
            {function, {66, 1}, two_params, 0, [
                {clause, {66, 1}, [], [], [
                    {call, {66, 17},
                        {remote, {66, 20}, {atom, {66, 17}, ets}, {atom, {66, 21}, fun2ms}}, [
                            {'fun', {66, 28},
                                {clauses, [
                                    {clause, {66, 31}, [{var, {66, 32}, 'A'}, {var, {66, 35}, 'B'}],
                                        [], [{var, {66, 41}, 'A'}]}
                                ]}}
                        ]}
                ]}
            ]}
        ]}
    ].

shells() ->
    [
        {ets, [
            {clause, 1, [{tuple, 1, [{var, 1, 'A'}, {var, 1, 'B'}]}],
                [[{op, 1, '>', {var, 1, 'A'}, {var, 1, 'X'}}]], [
                    {tuple, 1, [{var, 1, 'B'}, {var, 1, 'Y'}]}
                ]}
        ]},
        {ets, [
            {clause, 1, [{tuple, 1, [{var, 1, 'A'}, {var, 1, 'B'}]}], [], [
                {bin, 1, [
                    {bin_element, 1, {var, 1, 'X'}, {integer, 1, 16}, [little]},
                    {bin_element, 1, {float, 1, 3.5}, default, [float]},
                    {bin_element, 1, {string, 1, [97, 98]}, default, default},
                    {bin_element, 1, {var, 1, 'Y'}, default, [binary]}
                ]}
            ]}
        ]},
        {ets, [
            {clause, 1, [{tuple, 1, [{var, 1, 'A'}]}], [], [
                {tuple, 1, [{var, 1, 'Z'}, {var, 1, 'A'}]}
            ]}
        ]},
        {dbg, [
            {clause, 1, [{cons, 1, {var, 1, 'A'}, {nil, 1}}], [], [
                {call, 1, {atom, 1, message}, [{var, 1, 'X'}]}
            ]}
        ]},
        {ets, [
            {clause, 1, [{tuple, 1, [{var, 1, 'A'}]}], [], [
                {map, 1, [{map_field_assoc, 1, {atom, 1, a}, {var, 1, 'X'}}]}
            ]}
        ]},
        {ets, [{clause, 1, [{var, 1, 'A'}, {var, 1, 'B'}], [], [{var, 1, 'A'}]}]}
    ].
