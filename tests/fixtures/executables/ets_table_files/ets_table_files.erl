-module(ets_table_files).
-export([main/1, id/1]).

%% ets table files (plan 21 step LIB12): tab2file/2,3, file2tab/1,2 and tabfile_info/1 over disk_log halt logs in
%% OTP's format, with extended information, verification, damaged and foreign files, and a file OTP 29.1.1 wrote.
%% Identifiers, pids and memory figures differ between runs and are left out of what is printed.
main(_) ->
    show(round_trips, round_trips()),
    show(header, header()),
    show(extended, extended()),
    show(named, named()),
    show(damaged, damaged()),
    show(errors, errors()),
    show(options, options()),
    show(otp_file, otp_file()),
    show(large, large()),
    [file:delete(F) || F <- filelib_files()],
    io:format("ets_table_files: ok~n").

id(X) -> X.

show(Label, Value) -> io:format("~p: ~w~n", [Label, Value]).

attempt(F) ->
    try F() of
        Value -> {ok, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

filelib_files() ->
    [
        "t.tab",
        "x.tab",
        "n.tab",
        "cut.tab",
        "opened.tab",
        "text.tab",
        "short.tab",
        "empty.tab",
        "head.tab",
        "bad.tab",
        "otp.tab",
        "large.tab",
        "a.tab"
    ].

sorted(L) when is_list(L) -> lists:sort(L);
sorted(Other) -> Other.

objects() ->
    [{K, K * K, [K | "x"], <<K>>} || K <- lists:seq(1, 250)] ++
        [{atom_key, {nested, [1.5, #{m => 1}]}}].

round_trips() ->
    [
        begin
            T = ets:new(t, [Type]),
            ets:insert(T, objects()),
            ets:insert(T, {1, duplicate}),
            Written = ets:tab2file(T, "t.tab"),
            {ok, Loaded} = ets:file2tab("t.tab"),
            Same = sorted(ets:tab2list(Loaded)) =:= sorted(ets:tab2list(T)),
            R = [
                Written,
                Same,
                ets:info(Loaded, type),
                ets:info(Loaded, size),
                ets:info(Loaded, protection),
                ets:info(Loaded, named_table),
                ets:info(Loaded, keypos),
                Loaded =/= T
            ],
            ets:delete(T),
            ets:delete(Loaded),
            R
        end
     || Type <- [set, ordered_set, bag, duplicate_bag]
    ].

%% The first item is the information tuple; the file's head and item framing.
header() ->
    T = ets:new(h, [public, {keypos, 2}]),
    ets:insert(T, [{x, 1}, {y, 2}]),
    ok = ets:tab2file(T, "t.tab"),
    {ok, Bytes} = file:read_file("t.tab"),
    <<Head:8/binary, Size:32, Magic:4/binary, Rest/binary>> = Bytes,
    Info = tuple_to_list(binary_to_term(binary:part(Rest, 0, Size))),
    Kept = [Item || {Key, _} = Item <- Info, not lists:member(Key, [id, memory, owner])],
    ets:delete(T),
    [Head, Magic, [Key || {Key, _} <- Info], Kept, ets:tabfile_info("t.tab")].

extended() ->
    T = ets:new(e, [ordered_set]),
    ets:insert(T, objects()),
    R = [
        [
            begin
                ok = ets:tab2file(T, "x.tab", Options),
                {ok, Info} = ets:tabfile_info("x.tab"),
                {ok, Plain} = ets:file2tab("x.tab"),
                {ok, Verified} = ets:file2tab("x.tab", [{verify, true}]),
                Same =
                    ets:tab2list(Plain) =:= ets:tab2list(T) andalso
                        ets:tab2list(Verified) =:= ets:tab2list(T),
                ets:delete(Plain),
                ets:delete(Verified),
                {lists:keyfind(extended_info, 1, Info), Same}
            end
         || Options <- [
                [],
                [{extended_info, [object_count]}],
                [{extended_info, [md5sum]}],
                [{extended_info, [md5sum, object_count]}, {sync, true}],
                [{sync, false}]
            ]
        ]
    ],
    ets:delete(T),
    R.

named() ->
    T = ets:new(file_table, [named_table, set, private]),
    ets:insert(T, {k, v}),
    ok = ets:tab2file(file_table, "n.tab"),
    Exists = ets:file2tab("n.tab"),
    ets:delete(file_table),
    {ok, Loaded} = ets:file2tab("n.tab"),
    Into = ets:new(into, []),
    ets:insert(Into, {old, 1}),
    {ok, Into} = ets:file2tab("n.tab", [{table, Into}]),
    R = [
        Exists,
        Loaded,
        ets:tab2list(file_table),
        ets:info(file_table, protection),
        sorted(ets:tab2list(Into)),
        ets:tabfile_info(n_atom_is_no_file)
    ],
    ets:delete(file_table),
    ets:delete(Into),
    R.

%% Files cut short, never closed, foreign or without items.
damaged() ->
    T = ets:new(d, []),
    ets:insert(T, [{a, 1}, {b, 2}]),
    ok = ets:tab2file(T, "t.tab", [{extended_info, [object_count]}]),
    {ok, Bytes} = file:read_file("t.tab"),
    Size = byte_size(Bytes),
    ok = file:write_file("cut.tab", binary:part(Bytes, 0, Size - 3)),
    ok = file:write_file(
        "opened.tab", <<1, 2, 3, 4, 6, 7, 8, 9, (binary:part(Bytes, 8, Size - 8))/binary>>
    ),
    ok = file:write_file("text.tab", <<"not a log file at all">>),
    ok = file:write_file("short.tab", <<1, 2, 3>>),
    ok = file:write_file("empty.tab", <<>>),
    ok = file:write_file("head.tab", binary:part(Bytes, 0, 8)),
    ok = file:write_file("bad.tab", <<
        (binary:part(Bytes, 0, 20))/binary, 0, 0, (binary:part(Bytes, 22, Size - 22))/binary
    >>),
    R = [
        [
            result(attempt(fun() -> ets:file2tab(F) end)),
            result(attempt(fun() -> ets:file2tab(F, [{verify, true}]) end)),
            info_result(attempt(fun() -> ets:tabfile_info(F) end))
        ]
     || F <- [
            "cut.tab",
            "opened.tab",
            "text.tab",
            "short.tab",
            "empty.tab",
            "head.tab",
            "bad.tab",
            "missing.tab"
        ]
    ],
    ets:delete(T),
    R.

%% A loaded table as its sorted objects (it is deleted), an error as it is, a raise as {raised, Reason}.
result({ok, {ok, Table}}) -> loaded(Table);
result({ok, Other}) -> Other;
result({error, Reason}) -> {raised, Reason};
result(Other) -> Other.

loaded(Table) ->
    Objects = lists:sort(ets:tab2list(Table)),
    ets:delete(Table),
    {ok, Objects}.

info_result({ok, {ok, Info}}) -> {ok, [Item || {Key, _} = Item <- Info, Key =/= size]};
info_result({ok, Other}) -> Other;
info_result({error, Reason}) -> {raised, Reason}.

errors() ->
    T = ets:new(r, []),
    Gone = ets:new(gone, []),
    ets:delete(Gone),
    R = [
        ets:tab2file(Gone, "x.tab"),
        ets:tab2file(T, "no_such_directory/x.tab"),
        ets:tab2file(T, id(<<"x.tab">>)),
        ets:tab2file(T, 'a.tab'),
        ets:tabfile_info('a.tab') =/= {error, badfile},
        ets:file2tab(id(<<"x.tab">>)),
        attempt(fun() -> ets:tabfile_info(id(<<"x.tab">>)) end),
        attempt(fun() -> ets:tab2file(T, "x.tab", id(bogus)) end),
        ets:file2tab("missing.tab"),
        private_table()
    ],
    ets:delete(T),
    R.

%% A private table of another process cannot be written to a file.
private_table() ->
    Self = self(),
    Pid = spawn(fun() ->
        Self ! {table, ets:new(p, [private])},
        receive
            stop -> ok
        end
    end),
    receive
        {table, Table} -> ok
    end,
    Result = ets:tab2file(Table, "x.tab"),
    Pid ! stop,
    {Result, file:read_file_info("x.tab") =:= {error, enoent}}.

options() ->
    T = ets:new(o, []),
    ok = ets:tab2file(T, "x.tab"),
    R = [
        ets:tab2file(T, "x.tab", [bogus]),
        ets:tab2file(T, "x.tab", [{extended_info, [bogus]}]),
        ets:tab2file(T, "x.tab", [{extended_info, bogus}]),
        ets:tab2file(T, "x.tab", [{extended_info, []}, {extended_info, []}]),
        ets:tab2file(T, "x.tab", [{sync, 'maybe'}]),
        ets:tab2file(T, "x.tab", [ok | bad]),
        ets:file2tab("x.tab", [bogus]),
        ets:file2tab("x.tab", id(bogus)),
        ets:file2tab("x.tab", [{verify, 'maybe'}]),
        result(attempt(fun() -> ets:file2tab("x.tab", [{verify, false}, {verify, true}]) end))
    ],
    ets:delete(T),
    R.

%% A file OTP 29.1.1's tab2file/3 wrote, with object_count and md5sum.
otp_file() ->
    ok = file:write_file("otp.tab", otp_bytes()),
    {ok, Info} = ets:tabfile_info("otp.tab"),
    {ok, Plain} = ets:file2tab("otp.tab"),
    Objects = ets:tab2list(Plain),
    ets:delete(Plain),
    {ok, Verified} = ets:file2tab("otp.tab", [{verify, true}]),
    Same = ets:tab2list(Verified) =:= Objects,
    ets:delete(Verified),
    [Info, Objects, Same].

%% Objects of 65528 bytes or more are framed with an MD5 of their size.
large() ->
    T = ets:new(l, []),
    ets:insert(T, [{big, binary:copy(<<"y">>, 70000)}, {small, 1}]),
    ok = ets:tab2file(T, "large.tab", [{extended_info, [md5sum]}]),
    {ok, Loaded} = ets:file2tab("large.tab", [{verify, true}]),
    R = [lists:sort(ets:tab2list(Loaded)) =:= lists:sort(ets:tab2list(T)), ets:info(Loaded, size)],
    ets:delete(T),
    ets:delete(Loaded),
    R.

otp_bytes() ->
    <<1, 2, 3, 4, 99, 88, 77, 11, 0, 0, 1, 169, 98, 87, 76, 65, 131, 104, 18, 104, 2, 119, 2, 105,
        100, 90, 0, 3, 119, 13, 110, 111, 110, 111, 100, 101, 64, 110, 111, 104, 111, 115, 116, 0,
        0, 0, 0, 0, 3, 151, 221, 79, 250, 0, 4, 95, 12, 174, 39, 104, 2, 119, 22, 100, 101, 99, 101,
        110, 116, 114, 97, 108, 105, 122, 101, 100, 95, 99, 111, 117, 110, 116, 101, 114, 115, 119,
        5, 102, 97, 108, 115, 101, 104, 2, 119, 16, 114, 101, 97, 100, 95, 99, 111, 110, 99, 117,
        114, 114, 101, 110, 99, 121, 119, 5, 102, 97, 108, 115, 101, 104, 2, 119, 17, 119, 114, 105,
        116, 101, 95, 99, 111, 110, 99, 117, 114, 114, 101, 110, 99, 121, 119, 5, 102, 97, 108, 115,
        101, 104, 2, 119, 10, 99, 111, 109, 112, 114, 101, 115, 115, 101, 100, 119, 5, 102, 97, 108,
        115, 101, 104, 2, 119, 6, 109, 101, 109, 111, 114, 121, 97, 196, 104, 2, 119, 5, 111, 119,
        110, 101, 114, 88, 119, 13, 110, 111, 110, 111, 100, 101, 64, 110, 111, 104, 111, 115, 116,
        0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 104, 2, 119, 4, 104, 101, 105, 114, 119, 4, 110, 111,
        110, 101, 104, 2, 119, 4, 110, 97, 109, 101, 119, 8, 111, 116, 112, 95, 109, 97, 100, 101,
        104, 2, 119, 4, 115, 105, 122, 101, 97, 4, 104, 2, 119, 4, 110, 111, 100, 101, 119, 13, 110,
        111, 110, 111, 100, 101, 64, 110, 111, 104, 111, 115, 116, 104, 2, 119, 11, 110, 97, 109,
        101, 100, 95, 116, 97, 98, 108, 101, 119, 4, 116, 114, 117, 101, 104, 2, 119, 4, 116, 121,
        112, 101, 119, 11, 111, 114, 100, 101, 114, 101, 100, 95, 115, 101, 116, 104, 2, 119, 6,
        107, 101, 121, 112, 111, 115, 97, 1, 104, 2, 119, 10, 112, 114, 111, 116, 101, 99, 116, 105,
        111, 110, 119, 6, 112, 117, 98, 108, 105, 99, 104, 2, 119, 13, 109, 97, 106, 111, 114, 95,
        118, 101, 114, 115, 105, 111, 110, 97, 1, 104, 2, 119, 13, 109, 105, 110, 111, 114, 95, 118,
        101, 114, 115, 105, 111, 110, 97, 0, 104, 2, 119, 13, 101, 120, 116, 101, 110, 100, 101,
        100, 95, 105, 110, 102, 111, 108, 0, 0, 0, 2, 119, 12, 111, 98, 106, 101, 99, 116, 95, 99,
        111, 117, 110, 116, 119, 6, 109, 100, 53, 115, 117, 109, 106, 0, 0, 0, 10, 98, 87, 76, 65,
        131, 104, 2, 97, 1, 119, 3, 111, 110, 101, 0, 0, 0, 11, 98, 87, 76, 65, 131, 104, 2, 97, 2,
        107, 0, 3, 116, 119, 111, 0, 0, 0, 15, 98, 87, 76, 65, 131, 104, 2, 97, 3, 109, 0, 0, 0, 5,
        116, 104, 114, 101, 101, 0, 0, 0, 26, 98, 87, 76, 65, 131, 104, 2, 104, 1, 119, 4, 102, 111,
        117, 114, 108, 0, 0, 0, 1, 70, 64, 16, 0, 0, 0, 0, 0, 0, 106, 0, 0, 0, 67, 98, 87, 76, 65,
        131, 108, 0, 0, 0, 2, 119, 13, 36, 101, 110, 100, 95, 111, 102, 95, 116, 97, 98, 108, 101,
        108, 0, 0, 0, 2, 104, 2, 119, 5, 99, 111, 117, 110, 116, 97, 4, 104, 2, 119, 3, 109, 100,
        53, 109, 0, 0, 0, 16, 21, 111, 227, 103, 45, 9, 225, 180, 198, 88, 91, 150, 24, 154, 47, 57,
        106, 106>>.
