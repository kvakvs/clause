-module(file_ram).
-export([main/1]).

%% file:open/2 with ram (plan 21 step LIB54C): files in memory read,
%% written, positioned, read and written at offsets, truncated and copied as
%% OTP's ram_file driver answers, with its modes, defaults and errors.
main([]) ->
    reading(),
    writing(),
    modes(),
    edges(),
    io:format("file_ram: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

reading() ->
    {ok, F} = file:open(<<"hello\nworld\n">>, [ram, binary, read]),
    show(read, [file:read(F, 2), file:position(F, cur), file:position(F, eof)]),
    show(at_eof, [file:read(F, 3), file:read(F, 0)]),
    show(pread, [file:pread(F, 1, 3), file:pread(F, 100, 3), file:pread(F, 12, 0)]),
    show(pread_list, [file:pread(F, [{0, 2}, {6, 5}, {50, 1}]), file:position(F, cur)]),
    show(positions, [
        file:position(F, {bof, -1}),
        file:position(F, foo),
        file:position(F, 100),
        file:read(F, 2),
        file:position(F, {cur, -95}),
        file:read(F, 3),
        file:position(F, bof),
        file:position(F, {eof, -1})
    ]),
    show(read_only, [file:write(F, <<"x">>), file:pwrite(F, 0, <<"x">>), file:truncate(F)]),
    show(syncs, [file:sync(F), file:datasync(F)]),
    show(close, [file:close(F), file:close(F)]).

writing() ->
    {ok, W} = file:open("abc", [ram, read, write]),
    show(list_read, [file:read(W, 10), file:position(W, cur)]),
    show(write, [file:write(W, "XY"), file:position(W, cur)]),
    show(pwrite, [
        file:pwrite(W, 5, <<"Z">>),
        file:pwrite(W, [{0, "q"}, {1, <<"r">>}]),
        file:pread(W, 0, 20),
        file:position(W, cur)
    ]),
    show(truncate, [file:position(W, 2), file:truncate(W), file:pread(W, 0, 20)]),
    show(extend, [file:position(W, 6), file:truncate(W), file:pread(W, 0, 20), file:pread(W, 6, 1)]),
    show(holes, [file:position(W, 9), file:write(W, "Q"), file:pread(W, 0, 20)]),
    show(bad_writes, [
        file:write(W, [foo]),
        file:write(W, foo),
        file:pwrite(W, 0, [foo]),
        file:pwrite(W, a, "x"),
        file:pwrite(W, -1, "x"),
        file:pwrite(W, [{0, "a"}, {-1, "b"}]),
        file:pread(W, 0, 1)
    ]),
    show(close, file:close(W)).

modes() ->
    show(bad_modes, [
        file:open(<<"abc">>, [ram, append]),
        file:open(<<"abc">>, [ram, write, exclusive]),
        file:open(<<"abc">>, [ram, read, {encoding, utf8}]),
        file:open(<<"abc">>, [ram, compressed, read]),
        file:open(foo, [ram])
    ]),
    {ok, Default} = file:open(<<"abc">>, [ram]),
    show(default_read, [file:read(Default, 2), file:write(Default, "z")]),
    {ok, WriteOnly} = file:open(<<"abcdef">>, [ram, write]),
    show(write_only, [
        file:read(WriteOnly, 1),
        file:write(WriteOnly, "Z"),
        file:pread(WriteOnly, 0, 5),
        file:position(WriteOnly, cur)
    ]),
    {ok, Deep} = file:open([<<"a">>, "b", $c], [ram, binary]),
    show(iodata, file:read(Deep, 5)),
    show(raw_and_ram, outcome(fun() -> file:open(<<"a">>, [ram, raw]) end)),
    {ok, Empty} = file:open(<<>>, [ram]),
    show(empty, [file:read(Empty, 0), file:read(Empty, 1), file:pread(Empty, 0, 0)]).

edges() ->
    {ok, F} = file:open(<<"hello">>, [ram, binary, read, write]),
    show(advise, [
        file:advise(F, 0, 10, normal), file:advise(F, 0, 10, bogus), file:advise(F, -1, 10, normal)
    ]),
    show(allocate, [file:allocate(F, 0, 100), file:pread(F, 0, 200)]),
    show(pread_keeps_position, [file:position(F, 2), file:pread(F, 0, 3), file:position(F, cur)]),
    Big = 1 bsl 31,
    show(limits, [
        file:read(F, Big),
        file:pread(F, Big, 1),
        file:position(F, Big),
        file:pwrite(F, Big, "a"),
        file:pread(F, 0, Big)
    ]),
    show(bad_preads, [
        file:pread(F, [{0, 1}, {a, 1}]), file:pread(F, [{-1, 1}]), file:pread(F, a, 1)
    ]),
    {ok, Source} = file:open(<<"source data">>, [ram, binary]),
    {ok, Destination} = file:open(<<>>, [ram, binary, write, read]),
    show(copy, [file:copy(Source, Destination, 6), file:pread(Destination, 0, 100)]),
    ok = file:close(F).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
