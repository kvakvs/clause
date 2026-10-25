-module(file_positioned).
-export([main/1]).

%% The rest of file (plan 21 step LIB50): positioned reads and writes, truncate,
%% advise, allocate, write_file_info and the change_* functions, sendfile over
%% loopback TCP and to a fun, and format_error/1. Host-dependent answers
%% (allocate, owners, permissions) are checked for shape only. Files are made in
%% the working directory and removed again.
main([]) ->
    Name = "file_positioned.txt",
    _ = file:delete(Name),
    ok = file:write_file(Name, <<"0123456789">>),
    positioned(Name),
    raw(Name),
    truncated(Name),
    advised(Name),
    information(Name),
    sent(Name),
    formatted(),
    ok = file:delete(Name),
    io:format("file_positioned: ok~n").

show(Label, Value) -> io:format("~s ~w~n", [Label, Value]).

%% pread/2,3 and pwrite/2,3 on a file server: every location form, end of file,
%% the position they leave, list forms and their errors.
positioned(Name) ->
    {ok, F} = file:open(Name, [read, write, binary]),
    show(pread, [
        file:pread(F, 0, 3),
        file:position(F, cur),
        file:pread(F, {bof, 4}, 2),
        file:pread(F, cur, 2),
        file:pread(F, {cur, 1}, 2),
        file:pread(F, {eof, -3}, 5),
        file:pread(F, eof, 1),
        file:pread(F, 20, 1),
        file:pread(F, 2, 0),
        file:pread(F, 30, 0),
        file:position(F, cur)
    ]),
    show(pread_list, [
        file:pread(F, [{0, 2}, {8, 5}, {20, 1}, {bof, 1}]),
        file:pread(F, []),
        file:pread(F, [{0, -1}]),
        file:pread(F, [bad]),
        file:pread(F, {-5, 1}),
        file:pread(F, -5, 1),
        file:pread(F, 0, -1),
        file:pread(F, 0, a),
        file:pread(F, bad, 1),
        file:pread(not_a_file, 0, 1)
    ]),
    show(pwrite, [
        file:pwrite(F, 2, <<"ab">>),
        file:position(F, cur),
        file:pwrite(F, {eof, 0}, ["c", <<"d">>]),
        file:pwrite(F, cur, "e"),
        file:pwrite(F, [{0, "x"}, {bof, <<"y">>}]),
        file:pwrite(F, [{1, "z"}, {-1, "q"}, {0, "w"}]),
        file:pwrite(F, [{0, bad}]),
        file:pwrite(F, 0, bad),
        file:pwrite(F, -1, "x"),
        file:pwrite(F, []),
        file:pwrite(F, [bad]),
        file:pwrite(F, bad),
        file:pwrite(not_a_file, 0, "x"),
        file:read_file(Name)
    ]),
    ok = file:close(F),
    {ok, L} = file:open(Name, [read]),
    show(pread_lists, [file:pread(L, 1, 3), file:pread(L, [{0, 2}, {20, 1}])]),
    ok = file:close(L),
    {ok, R} = file:open(Name, [read]),
    show(pwrite_read_only, [file:pwrite(R, 0, "x") =:= file:write(R, "x")]),
    ok = file:close(R),
    show(closed, [
        file:pread(F, 0, 1),
        file:pwrite(F, 0, "x"),
        file:pread(F, [{0, 1}]),
        file:pwrite(F, [{0, "x"}])
    ]).

%% The same functions on a raw file: integer locations only.
raw(Name) ->
    ok = file:write_file(Name, <<"0123456789">>),
    {ok, F} = file:open(Name, [read, write, raw, binary]),
    show(raw, [
        file:pread(F, 3, 2),
        file:pread(F, [{0, 1}, {9, 3}, {12, 1}]),
        file:pwrite(F, 1, <<"AB">>),
        file:pwrite(F, [{4, "C"}, {6, <<"D">>}]),
        file:pread(F, 0, 10),
        file:pread(F, 12, 1),
        file:pread(F, bof, 1),
        file:pwrite(F, eof, "x"),
        file:pwrite(F, -1, "x"),
        file:pread(F, -1, 1)
    ]),
    ok = file:close(F).

%% truncate/1 at the current position, after writes and positioned writes.
truncated(Name) ->
    ok = file:write_file(Name, <<"0123456789">>),
    {ok, F} = file:open(Name, [read, write, binary]),
    show(truncate, [
        file:position(F, 6),
        file:truncate(F),
        file:read_file(Name),
        file:pwrite(F, 8, "xy"),
        file:position(F, 2),
        file:truncate(F),
        file:position(F, cur),
        file:read_file(Name),
        file:truncate(not_a_file)
    ]),
    ok = file:close(F),
    show(truncate_closed, [file:truncate(F)]),
    {ok, R} = file:open(Name, [read]),
    Reply = file:truncate(R),
    show(truncate_read_only, [element(1, Reply), is_process_alive(R)]),
    _ = file:close(R).

%% advise/4: every advice, bad arguments, and a server that stops after an error.
%% allocate/3 succeeds or is not supported, as the platform decides.
advised(Name) ->
    ok = file:write_file(Name, <<"0123456789">>),
    {ok, F} = file:open(Name, [read, write, binary]),
    Advice = [normal, sequential, random, no_reuse, will_need, dont_need],
    show(advise, [file:advise(F, 0, 0, A) || A <- Advice]),
    ok = file:close(F),
    Bad = [{-1, 0, normal}, {0, -1, normal}, {1.5, 0, normal}, {a, 0, normal}, {0, 0, bad}],
    show(advise_errors, [advise_error(Name, O, L, A) || {O, L, A} <- Bad]),
    show(advise_not_a_file, [file:advise(not_a_file, 0, 0, normal)]),
    {ok, G} = file:open(Name, [read, write, binary]),
    Allocated = file:allocate(G, 0, 64),
    show(allocate, [
        lists:member(Allocated, [ok, {error, enotsup}]),
        file:allocate(G, -1, 1),
        file:allocate(G, 1.5, 1),
        file:allocate(G, a, 1),
        file:allocate(G, 0, b),
        is_process_alive(G)
    ]),
    ok = file:close(G).

%% The reply of one bad advise/4 on a fresh file, and a read after it.
advise_error(Name, Offset, Length, Advice) ->
    {ok, F} = file:open(Name, [read, binary]),
    Reply = file:advise(F, Offset, Length, Advice),
    {Reply, file:read(F, 1)}.

%% write_file_info/2,3 and change_mode/2, change_owner/2,3, change_group/2,
%% change_time/2,3, read back with read_file_info/2.
information(Name) ->
    ok = file:write_file(Name, <<"abc">>),
    Info = file:read_file_info(Name, [{time, posix}]),
    {ok, {file_info, _, _, _, _, _, _, Mode, _, _, _, _, Uid, Gid}} = Info,
    show(change_mode, [
        file:change_mode(Name, 8#444),
        writable(Name),
        file:change_mode(Name, 8#644),
        writable(Name),
        file:change_mode("no_such_file.txt", 8#644),
        outcome(fun() -> file:change_mode(Name, bad) end)
    ]),
    show(change_owner, [
        file:change_owner(Name, Uid),
        file:change_owner(Name, Uid, Gid),
        file:change_group(Name, Gid),
        outcome(fun() -> file:change_owner(Name, bad) end),
        outcome(fun() -> file:change_group(Name, bad) end)
    ]),
    show(change_time, [
        file:change_time(Name, {{2020, 1, 2}, {3, 4, 5}}),
        mtime(Name, local),
        file:change_time(Name, {{2019, 5, 6}, {7, 8, 9}}, {{2021, 3, 4}, {5, 6, 7}}),
        atime(Name, local),
        mtime(Name, local),
        file:change_time("no_such_file.txt", {{2020, 1, 2}, {3, 4, 5}}),
        outcome(fun() -> file:change_time(Name, bad) end),
        outcome(fun() -> file:change_time(Name, {{2020, 1, 2}, {3, 4, x}}) end)
    ]),
    show(write_file_info, [
        file:write_file_info(Name, info(1000000000, 1100000000), [{time, posix}]),
        atime(Name, posix),
        mtime(Name, posix),
        file:write_file_info(Name, info({{2001, 2, 3}, {4, 5, 6}}, undefined), [{time, universal}]),
        mtime(Name, universal),
        file:write_file_info(Name, info(undefined, {{2002, 2, 3}, {4, 5, 6}})),
        mtime(Name, local),
        file:write_file_info(Name, info(1200000000, undefined), [raw, {time, posix}]),
        mtime(Name, posix),
        file:write_file_info(Name, setelement(8, info(undefined, undefined), Mode), [{time, posix}]),
        file:write_file_info("no_such_file.txt", info(0, 0), [{time, posix}]),
        file:write_file_info(Name, info(bad, 0), [{time, posix}]),
        file:write_file_info(Name, info(0, 0), [{time, bad}]),
        file:write_file_info({bad}, info(0, 0)),
        outcome(fun() -> file:write_file_info(Name, bad) end),
        outcome(fun() -> file:write_file_info(Name, info(0, 0), bad) end)
    ]).

%% A #file_info{} with only atime and mtime set (the record has 14 elements).
info(ATime, MTime) ->
    {file_info, undefined, undefined, undefined, ATime, MTime, undefined, undefined, undefined,
        undefined, undefined, undefined, undefined, undefined}.

writable(Name) ->
    {ok, Info} = file:read_file_info(Name),
    element(8, Info) band 8#200 =/= 0.

atime(Name, Time) -> element(5, element(2, file:read_file_info(Name, [{time, Time}]))).

mtime(Name, Time) -> element(6, element(2, file:read_file_info(Name, [{time, Time}]))).

%% sendfile/2,5 to a loopback TCP socket and to a fun, and its errors.
sent(Name) ->
    ok = file:write_file(Name, <<"0123456789">>),
    {ok, Listen} = gen_tcp:listen(0, [binary, {ip, {127, 0, 0, 1}}, {active, false}]),
    {ok, Port} = inet:port(Listen),
    Self = self(),
    Reader = spawn(fun() ->
        {ok, S} = gen_tcp:accept(Listen),
        Self ! {received, read_all(S, <<>>)}
    end),
    {ok, Client} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    {ok, Raw} = file:open(Name, [read, raw, binary]),
    {ok, Plain} = file:open(Name, [read, binary]),
    show(sendfile, [
        file:sendfile(Name, Client),
        file:sendfile(Raw, Client, 2, 3, []),
        file:position(Raw, cur),
        file:sendfile(Raw, Client, 8, 0, [{chunk_size, 1}]),
        file:sendfile(Raw, Client, 20, 0, []),
        file:sendfile(Plain, Client, 0, 0, []),
        file:sendfile("no_such_file.txt", Client),
        file:sendfile(Raw, Client, -1, 0, []),
        file:sendfile(Raw, Client, a, 0, [])
    ]),
    ok = gen_tcp:close(Client),
    receive
        {received, Data} -> show(received, [Data])
    after 5000 -> show(received, [timeout, Reader])
    end,
    Collect = fun(Chunk) ->
        Self ! {chunk, iolist_to_binary(Chunk)},
        ok
    end,
    Refuse = fun(_) -> {error, closed} end,
    show(sendfile_fun, [
        file:sendfile(Raw, Collect, 1, 7, [{chunk_size, 3}]),
        chunks(),
        file:sendfile(Raw, Refuse, 0, 0, []),
        file:sendfile(Name, Collect),
        chunks()
    ]),
    ok = file:close(Raw),
    ok = file:close(Plain),
    ok = gen_tcp:close(Listen).

read_all(Socket, Acc) ->
    case gen_tcp:recv(Socket, 0, 5000) of
        {ok, Data} -> read_all(Socket, <<Acc/binary, Data/binary>>);
        {error, closed} -> Acc
    end.

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

chunks() ->
    receive
        {chunk, Chunk} -> [Chunk | chunks()]
    after 0 -> []
    end.

%% format_error/1 of POSIX reasons, the module's own reasons and the
%% {Line, Module, Reason} errors of consult, eval and script.
formatted() ->
    Errors = [
        enoent,
        eacces,
        eexist,
        eisdir,
        enotdir,
        einval,
        ebadf,
        enospc,
        eperm,
        enotsup,
        econnrefused,
        exbadport,
        nxdomain,
        esuccess,
        unknown_reason,
        badarg,
        terminated,
        system_limit,
        {1, file, undefined_script},
        {2, file, {error, badarith, []}},
        {3, file, {badarith, [{m, f, 1, []}]}},
        {4, erl_parse, ["syntax error before: ", "'.'"]},
        {5, erl_scan, {string, $", "abc"}}
    ],
    [io:format("~ts~n", [file:format_error(E)]) || E <- Errors],
    show(format_error, [outcome(fun() -> file:format_error(12) end)]),
    ok = file:write_file("file_positioned.script", <<"X = 1.\nX + a.\n">>),
    {error, Error} = file:script("file_positioned.script"),
    io:format("~ts~n", [
        file:format_error(setelement(3, Error, setelement(3, element(3, Error), [])))
    ]),
    ok = file:delete("file_positioned.script").
