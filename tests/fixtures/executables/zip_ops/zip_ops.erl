-module(zip_ops).
-export([main/1]).

-include_lib("kernel/include/file.hrl").
-include_lib("stdlib/include/zip.hrl").

%% zip (plan 21 step LIB54): archives made in memory from binaries with fixed
%% file information (compared by digests of their bytes with the local DOS
%% times zeroed, so the time zone does not matter), stored and deflated files
%% past the 8 KiB block, comments, UTF-8 names, archives on disk made from
%% files and directories, extraction to memory and disk with filters, cwd,
%% keep_old_files and skip_directories, list_dir options, foldl, open archives
%% and errors. Times print as POSIX seconds. Files are made in a directory
%% under the working directory and removed again.
main([]) ->
    Dir = "zip_ops.dir",
    _ = file:del_dir_r(Dir),
    ok = file:make_dir(Dir),
    put(dir, Dir),
    % Illegal paths are reported through the logger.
    ok = logger:remove_handler(default),
    memory_archives(),
    names(),
    disk_archives(),
    extraction(),
    listing(),
    folding(),
    open_archives(),
    errors(),
    ok = file:del_dir_r(Dir),
    io:format("zip_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, clean(Value)]).

%% Local datetimes as POSIX seconds, pids by shape, caught errors without stacks.
clean(#file_info{mtime = M, atime = A, ctime = C} = FI) ->
    FI#file_info{mtime = posix(M), atime = posix(A), ctime = posix(C)};
clean(Pid) when is_pid(Pid) -> pid;
clean({'EXIT', {Reason, [_ | _]}}) ->
    {'EXIT', clean(Reason)};
clean(List) when is_list(List) -> [clean(E) || E <- List];
clean(Tuple) when is_tuple(Tuple) -> list_to_tuple(clean(tuple_to_list(Tuple)));
clean(Other) ->
    Other.

posix({{_, _, _}, {_, _, _}} = DateTime) ->
    erlang:universaltime_to_posixtime(erlang:localtime_to_universaltime(DateTime));
posix(Other) ->
    Other.

path(Name) -> filename:join(get(dir), Name).

-define(MTIME, 1700000000).

info(Size) ->
    #file_info{
        size = Size,
        type = regular,
        access = read_write,
        mtime = ?MTIME,
        atime = ?MTIME + 60,
        ctime = ?MTIME,
        mode = 8#100640,
        links = 1,
        uid = 1000,
        gid = 100
    }.

spec(Name, Bin) -> {Name, Bin, info(byte_size(Bin))}.

%% A text that deflates well, and one that does not.
text(N) -> iolist_to_binary([integer_to_list(I rem 97) || I <- lists:seq(1, N)]).

noise(N) -> noise(N, 12345, []).

noise(0, _, Acc) ->
    list_to_binary(Acc);
noise(N, Seed, Acc) ->
    Next = (Seed * 1103515245 + 12345) rem 2147483648,
    noise(N - 1, Next, [(Next bsr 16) band 255 | Acc]).

%% The archive's size and MD5 with each local and central DOS time zeroed.
digest(Archive) ->
    Masked = mask_central(mask_local(Archive), Archive),
    {byte_size(Archive), binary:encode_hex(erlang:md5(Masked))}.

mask_local(Archive) ->
    {ok, [_ | Files]} = zip:list_dir(Archive),
    lists:foldl(fun(#zip_file{offset = O}, B) -> zero(B, O + 10) end, Archive, Files).

mask_central(Masked, Archive) ->
    {EOCD, 4} = lists:last(binary:matches(Archive, <<"PK", 5, 6>>)),
    <<_:EOCD/binary, _:10/binary, N:16/little, _:32, Offset:32/little, _/binary>> = Archive,
    mask_entries(N, Offset, Masked).

mask_entries(0, _, B) ->
    B;
mask_entries(N, P, B) ->
    <<_:P/binary, "PK", 1, 2, _:24/binary, NL:16/little, EL:16/little, CL:16/little, _/binary>> = B,
    mask_entries(N - 1, P + 46 + NL + EL + CL, zero(B, P + 12)).

zero(B, P) ->
    <<Before:P/binary, _:32, After/binary>> = B,
    <<Before/binary, 0:32, After/binary>>.

entries(Archive) ->
    {ok, [Comment | Files]} = zip:list_dir(Archive),
    [
        Comment
        | [
            {N, S, C, O}
         || #zip_file{name = N, info = #file_info{size = S}, comp_size = C, offset = O} <- Files
        ]
    ].

memory_archives() ->
    Small = spec("small.txt", <<"tiny">>),
    Text = spec("text.txt", text(3000)),
    Noise = spec("noise.bin", noise(20000)),
    Empty = spec("empty.txt", <<>>),
    {ok, {"m.zip", A}} = zip:zip("m.zip", [Small, Text, Noise, Empty], [memory]),
    show(memory_zip, [digest(A), entries(A)]),
    show(memory_unzip, [
        [{N, byte_size(B), erlang:md5(B)} || {N, B} <- element(2, zip:unzip(A, [memory]))]
    ]),
    show(
        crc,
        element(2, zip:unzip(A, [memory])) =:= [{N, B} || {N, B, _} <- [Small, Text, Noise, Empty]]
    ),
    {ok, {"c.zip", C}} = zip:create("c.zip", [Text], [memory, {comment, "the comment"}]),
    show(comment, [digest(C), entries(C)]),
    {ok, {"s.zip", S}} = zip:zip("s.zip", [Text, Noise], [memory, {compress, []}]),
    show(compress_none, [digest(S), entries(S)]),
    {ok, {"u.zip", U}} = zip:zip("u.zip", [Text, spec("x.Z", text(100))], [
        memory, {uncompress, {add, [".txt"]}}
    ]),
    show(uncompress_add, entries(U)),
    {ok, {"e.zip", E}} = zip:zip("e.zip", [Text], [memory, {extra, []}]),
    show(no_extra, [digest(E), entries(E)]),
    {ok, {"g.zip", G}} = zip:zip("g.zip", [Text], [memory, {extra, [uid_gid]}]),
    show(uid_gid, [digest(G), zip:list_dir(G, [{extra, [uid_gid]}]), zip:list_dir(G, [{extra, []}])]),
    put(archive, A).

names() ->
    Accented = [$c, $a, $f, 16#e9, $., $t, $x, $t],
    {ok, {"n.zip", A}} = zip:zip(
        "n.zip", [spec(Accented, <<"latte">>), spec("dir/sub/f.txt", <<"x">>)], [memory]
    ),
    {ok, [_, #zip_file{name = Name1}, #zip_file{name = Name2}]} = zip:list_dir(A),
    show(names, [digest(A), Name1 =:= Accented, Name2]),
    show(name_unzip, [[{N =:= Accented, B} || {N, B} <- element(2, zip:unzip(A, [memory]))]]),
    show(get_crc32, begin
        {ok, H} = zip:zip_open(A, [memory]),
        R = zip:zip_get_crc32(Accented, H),
        ok = zip:zip_close(H),
        R
    end),
    % A missing file's CRC ends the archive's process and so its opener.
    {Pid, Ref} = spawn_monitor(fun() ->
        {ok, Handle} = zip:zip_open(A, [memory]),
        zip:zip_get_crc32("nope", Handle)
    end),
    receive
        {'DOWN', Ref, process, Pid, {Reason, _Stack}} -> show(get_crc32_missing, Reason)
    end,
    {ok, {"abs.zip", Abs}} = zip:zip("abs.zip", [spec("/abs/x.txt", <<"abs">>)], [memory]),
    show(absolute, [entries(Abs), zip:unzip(Abs, [memory])]),
    {ok, {"up.zip", Up}} = zip:zip("up.zip", [spec("a/../../up.txt", <<"up">>)], [memory]),
    show(climbing, [entries(Up), zip:unzip(Up, [memory])]).

write(Name, Bin) ->
    File = path(Name),
    ok = filelib:ensure_dir(File),
    ok = file:write_file(File, Bin),
    ok = file:write_file_info(File, #file_info{mtime = ?MTIME, atime = ?MTIME}, [{time, posix}]).

disk_archives() ->
    write("src/a.txt", text(500)),
    write("src/b.bin", noise(100)),
    write("src/d/c.txt", <<"in a directory">>),
    Zip = path("disk.zip"),
    show(zip_disk, zip:zip(Zip, ["a.txt", "b.bin", "d"], [{cwd, path("src")}])),
    show(disk_entries, [{N, S, C} || {N, S, C, _} <- tl(entries(Zip))]),
    show(verbose, zip:zip(path("v.zip"), ["a.txt"], [{cwd, path("src")}, verbose])),
    {ok, Bin} = file:read_file(Zip),
    show(same_files, element(2, zip:unzip(Bin, [memory])) =:= element(2, zip:unzip(Zip, [memory]))),
    show(mixed, [
        N
     || {N, _, _, _} <- tl(
            entries(element(2, element(2, zip:zip("x", [spec("m.txt", <<"m">>)], [memory]))))
        )
    ]),
    show(zip_to_memory, begin
        {ok, {"y.zip", Y}} = zip:zip("y.zip", ["a.txt"], [{cwd, path("src")}, memory]),
        [{N, S} || {N, S, _, _} <- tl(entries(Y))]
    end).

extraction() ->
    Zip = path("disk.zip"),
    Out = path("out"),
    show(unzip_disk, zip:unzip(Zip, [{cwd, Out}])),
    show(extracted, [
        file:read_file(filename:join(Out, "d/c.txt")),
        mtime(filename:join(Out, "a.txt")),
        mtime(filename:join(Out, "d/c.txt"))
    ]),
    ok = file:write_file(filename:join(Out, "a.txt"), <<"changed">>),
    show(keep_old_files, [
        zip:unzip(Zip, [{cwd, Out}, keep_old_files]), file:read_file(filename:join(Out, "a.txt"))
    ]),
    show(file_list, zip:extract(Zip, [{cwd, path("out2")}, {file_list, ["b.bin", "d/c.txt"]}])),
    Filter = fun(#zip_file{info = #file_info{size = Size}}) -> Size < 200 end,
    show(file_filter, zip:unzip(Zip, [memory, {file_filter, Filter}])),
    show(skip_directories, zip:unzip(Zip, [{cwd, path("out3")}, skip_directories])),
    show(verbose_unzip, zip:unzip(Zip, [memory, verbose, {file_list, ["a.txt"]}])),
    show(cooked, [N || {N, _} <- element(2, zip:unzip(Zip, [memory, cooked]))]).

mtime(File) ->
    {ok, #file_info{mtime = M}} = file:read_file_info(File, [{time, posix}]),
    M.

listing() ->
    A = get(archive),
    Zip = path("disk.zip"),
    {ok, [Comment, First | _]} = zip:list_dir(A),
    show(list_dir, [Comment, First]),
    show(names_only, [zip:list_dir(A, [names_only]), zip:table(Zip, [names_only, cooked])]),
    show(skip_dirs, zip:list_dir(Zip, [names_only, skip_directories])),
    show(t, zip:t(A)),
    show(t_disk, zip:t(Zip)).

folding() ->
    A = get(archive),
    Fun = fun(Name, GetInfo, GetBin, Acc) ->
        [{Name, (GetInfo())#file_info.size, byte_size(GetBin())} | Acc]
    end,
    show(foldl, zip:foldl(Fun, [], A)),
    show(foldl_named, zip:foldl(Fun, [], {"m.zip", A})),
    show(foldl_disk, zip:foldl(Fun, [], path("disk.zip"))),
    show(foldl_errors, [zip:foldl(Fun, [], <<"not a zip">>), zip:foldl(not_a_fun, [], A)]),
    show(
        foldl_throw, outcome(fun() -> zip:foldl(fun(N, _, _, _) -> throw({stop, N}) end, [], A) end)
    ).

open_archives() ->
    A = get(archive),
    {ok, H} = zip:zip_open(A, [memory]),
    show(zip_list_dir, [
        {N, S}
     || #zip_file{name = N, info = #file_info{size = S}} <- tl(element(2, zip:zip_list_dir(H)))
    ]),
    {ok, {"text.txt", Text}} = zip:zip_get("text.txt", H),
    show(zip_get, [byte_size(Text), zip:zip_get("missing", H), zip:zip_get_crc32("small.txt", H)]),
    show(zip_get_all, [{N, byte_size(B)} || {N, B} <- element(2, zip:zip_get(H))]),
    show(t_open, zip:t(H)),
    show(zip_close, [zip:zip_close(H), is_process_alive(H)]),
    {ok, D} = zip:zip_open(path("disk.zip"), [{cwd, path("open")}]),
    show(zip_get_disk, [zip:zip_get("a.txt", D), zip:zip_get(D), zip:zip_close(D)]),
    {ok, S} = zip:zip_open(path("disk.zip"), [memory, skip_directories]),
    show(zip_get_skip, [[N || {N, _} <- element(2, zip:zip_get(S))], zip:zip_close(S)]).

errors() ->
    show(missing, [
        zip:unzip(path("none.zip")),
        zip:list_dir(path("none.zip")),
        zip:t(path("none.zip")),
        zip:zip_open(path("none.zip"))
    ]),
    show(not_archive, [
        zip:unzip(<<"not a zip archive at all">>), zip:list_dir(<<>>), zip:zip_open(<<1, 2, 3>>)
    ]),
    show(bad_options, [
        zip:unzip(get(archive), [bogus]),
        zip:zip("x.zip", [], [memory, {compress, bad}]),
        zip:list_dir(get(archive), [{extra, [nope]}]),
        zip:zip_open(get(archive), [memory, nope]),
        zip:zip("x.zip", [spec("a", <<"a">>)], [memory, {extra, [bad]}])
    ]),
    show(einval, [zip:list_dir(an_atom), zip:unzip(an_atom), zip:zip("x.zip", [an_atom], [memory])]),
    show(missing_input, zip:zip(path("bad.zip"), [path("no_such_file")])),
    show(empty, zip:zip("empty.zip", [], [memory])),
    Truncated = binary:part(get(archive), 0, 100),
    show(truncated, zip:list_dir(Truncated)),
    % The first file's CRC in its local header.
    <<Head:14/binary, CRC:32/little, Rest/binary>> = get(archive),
    show(bad_crc, zip:unzip(<<Head/binary, (CRC bxor 1):32/little, Rest/binary>>, [memory])).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
