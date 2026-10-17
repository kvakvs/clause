-module(pretty_print).
-export([main/1]).

%% io_lib (plan 21 step LIB4D): pretty printing, bounded rendering, binary rendering, and native records.
main(_) ->
    show(print, [reason(fun() -> io_lib:print(T) end) || T <- terms()]),
    show(print4, [
        reason(fun() -> io_lib:print(T, C, L, D) end)
     || {T, C, L, D} <- print4_cases()
    ]),
    show(format, [reason(fun() -> io_lib:format(F, A) end) || {F, A} <- formats()]),
    show(format_limited, [
        reason(fun() -> io_lib:format("~p", [limit_term()], [{chars_limit, N}]) end)
     || N <- limits()
    ]),
    show(format_limited_mixed, [
        reason(fun() ->
            io_lib:format(
                "p=~p P=~P w=~w W=~W s=~s",
                [limit_term(), limit_term(), 3, limit_term(), limit_term(), 3, "tail"],
                [{chars_limit, N}]
            )
        end)
     || N <- limits()
    ]),
    show(prefixes, prefixes()),
    show(write_limited, write_limited()),
    show(bwrite_limited, bwrite_limited()),
    show(write_bin_limited, [
        reason(fun() -> io_lib:write_bin(limit_term(), -1, unicode, undefined, N) end)
     || N <- [1, 5, 10, 20, 40]
    ]),
    show(bformat, [reason(fun() -> io_lib:bformat(F, A) end) || {F, A} <- bformats()]),
    show(bformat_limited, [
        reason(fun() -> io_lib:bformat("~p", [limit_term()], [{chars_limit, N}]) end)
     || N <- [1, 5, 10, 20, 40]
    ]),
    show(errors, [
        reason(fun() -> io_lib:format(id("~-10p"), [limit_term()]) end),
        reason(fun() -> io_lib:print(limit_term(), 1, 80, id(foo)) end)
    ]),
    show(build_text, [
        reason(fun() ->
            io_lib:build_text(io_lib:scan_format("~p ~w ~P", [T, T, T, 2]), [{chars_limit, N}])
        end)
     || T <- [limit_term()], N <- [10, 30]
    ]),
    show(prompts, [
        reason(fun() -> io_lib:format_prompt(P) end)
     || P <- [{x}, {"~p", [limit_term()]}, [a | b], {"~P~n", [limit_term(), 3]}]
    ]),
    native_records(),
    readable_layouts(),
    direct_output(),
    io:format("pretty_print: ok~n").

terms() ->
    [
        atom,
        'quoted atom',
        '',
        0,
        -123456,
        1234567890123456789012345678901234567890,
        0.0,
        -12345.6789,
        1.0e100,
        "printable string",
        [0, 1, 2, 31, 127, 200, 201],
        "a\nb\"c",
        "\t",
        [200, 201],
        [],
        [a, [b, [c, [d, [e]]]]],
        [a, b | tail],
        {},
        {short, 1, [a, b]},
        long_nested_tuple(),
        {x, long_values()},
        {tagged, long_values()},
        {this_is_a_very_long_tag, long_values()},
        #{},
        #{1 => #{2 => #{3 => value}}, 4 => [a, b, c]},
        #{1 => long_values(), 2 => {nested, long_values()}},
        <<>>,
        <<"printable binary">>,
        <<"\x{400}\x{401}"/utf8>>,
        long_binary(),
        <<1:3>>,
        <<255, 1:5>>,
        [{record, 1, "field"}, {point, 10, 20}],
        [{N, N * N} || N <- lists:seq(1, 30)]
    ].

print4_cases() ->
    T = limit_term(),
    B = long_binary(),
    [
        {T, 1, 0, -1},
        {T, 10, 20, 1},
        {T, 40, 40, 2},
        {T, 75, 80, 3},
        {T, 1, 20, 5},
        {B, 10, 40, -1},
        {long_nested_tuple(), 40, 80, 5},
        {{this_is_a_very_long_tag, long_values()}, 75, 0, 2}
    ].

formats() ->
    T = limit_term(),
    Map = #{1 => one, 2 => two, 3 => three},
    [
        {"~p", [T]},
        {"~P", [T, 1]},
        {"~P", [T, 2]},
        {"~P", [T, 3]},
        {"~P", [T, 4]},
        {"~P", [T, 5]},
        {"~40p", [T]},
        {"~.10p", [T]},
        {"~20.5p", [T]},
        {"~tp|~tp|~tp", [[1024, 1025], '\x{400}', <<"\x{400}"/utf8>>]},
        {"~lp", ["abc"]},
        {"~kp", [Map]},
        {"~Kp", [reversed, Map]},
        {"~Kp", [ordered, Map]},
        {"first=~p second=~p~nthird=~p~n", [long_values(), T, long_nested_tuple()]},
        {"~p", [lists:duplicate(120, $a)]},
        {"~p", [[lists:duplicate(50, $b), lists:duplicate(50, $c)]]},
        {"~P", [<<"abcdefghijklmnop">>, 3]},
        {"~tP", [<<"a\x{400}bcdefghij"/utf8>>, 3]},
        {"~tP", [<<"\x{400}bcdefghij"/utf8>>, 3]},
        {"~P", [[1, 2 | c], 2]},
        {"~P", [<<1, 2, 3, 4, 5, 6>>, 3]},
        {"~P", [{a, <<1:3>>, #{1 => 2}}, 2]}
    ].

%% Strings and binaries cut by chars_limit: the printable prefix paths of lists and binaries.
prefixes() ->
    Long = lists:duplicate(30, $a),
    Wide = "ab\x{400}cdefghijklmnopqrstuvwxyz",
    [
        reason(fun() -> io_lib:format(F, [X], [{chars_limit, N}]) end)
     || F <- ["~p", "~tp"],
        X <- [
            Long,
            Wide,
            [Long, Long],
            list_to_binary(Long),
            <<"ab\x{400}cdefghijklmnop"/utf8>>,
            [1, 2 | c]
        ],
        N <- [3, 8, 20]
    ].

write_limited() ->
    [
        reason(fun() -> io_lib:write(limit_term(), [{chars_limit, N}]) end)
     || N <- [1, 5, 10, 20, 40]
    ] ++
        [
            reason(fun() -> io_lib:write(limit_term(), [{chars_limit, N}, {depth, D}]) end)
         || N <- [1, 5, 10, 20, 40], D <- [-1, 3]
        ] ++
        [
            reason(fun() ->
                io_lib:write(
                    ['\x{400}', [1025, 1026], limit_term()],
                    [{encoding, unicode}, {chars_limit, N}]
                )
            end)
         || N <- [1, 5, 10, 20, 40]
        ].

bwrite_limited() ->
    [
        reason(fun() -> io_lib:bwrite(limit_term(), [{chars_limit, N}]) end)
     || N <- [1, 5, 10, 20, 40]
    ] ++
        [
            reason(fun() -> io_lib:bwrite(limit_term(), [{chars_limit, N}, {depth, D}]) end)
         || N <- [1, 5, 10, 20, 40], D <- [-1, 3]
        ] ++
        [
            reason(fun() ->
                io_lib:bwrite(
                    ['\x{400}', [1025, 1026], limit_term()],
                    [{encoding, unicode}, {chars_limit, N}]
                )
            end)
         || N <- [1, 5, 10, 20, 40]
        ].

bformats() ->
    T = limit_term(),
    [
        {"~p", [T]},
        {"~p", [long_binary()]},
        {"binary=~p tail=~p~nnext=~p", [long_binary(), long_values(), T]}
    ].

native_records() ->
    Records = pretty_records:values(),
    show(native_print, [reason(fun() -> io_lib:print(R) end) || R <- Records]),
    show(native_print4, [
        reason(fun() -> io_lib:print(R, 1, L, -1) end)
     || R <- Records, L <- [20, 40]
    ]),
    show(native_depth, [reason(fun() -> io_lib:format("~P", [R, 2]) end) || R <- Records]),
    show(native_limited, [
        reason(fun() -> io_lib:write(R, [{chars_limit, 10}]) end)
     || R <- Records
    ]).

readable_layouts() ->
    io:put_chars(["readable tuple:\n", io_lib:print(long_nested_tuple(), 1, 32, -1), $\n]),
    io:put_chars(["readable binary:\n", io_lib:print(long_binary(), 1, 36, -1), $\n]),
    io:put_chars(["readable tag:\n", io_lib:print({tagged, long_values()}, 1, 24, -1), $\n]).

direct_output() ->
    io:format("~p~n", [long_nested_tuple()]),
    io:format("~P~n", [long_nested_tuple(), 4]),
    io:format("~p~n", [long_binary()]).

limit_term() ->
    {root, [
        {one, [1, 2, 3, 4, 5]},
        {two, #{1 => [a, b, c], 2 => {deep, {deeper, [x, y, z]}}}},
        {three, long_values()}
    ]}.

long_nested_tuple() ->
    {outer_container,
        {
            inner_container,
            [
                {alpha, 1, 2, 3},
                {beta, [one, two, three, four]},
                {gamma, {delta, {epsilon, [six, seven, eight]}}}
            ]
        }}.

long_values() ->
    [alpha, beta, gamma, delta, epsilon, zeta, eta, theta, iota, kappa].

long_binary() -> list_to_binary(lists:seq(0, 200)).

limits() -> [0, 1, 5, 10, 20, 50, 100].

id(X) -> X.

show(Name, Value) -> io:format("~w: ~w~n", [Name, Value]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> {error, Reason};
        throw:Thrown -> {thrown, Thrown}
    end.
