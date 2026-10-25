-module(tar_ops).
-export([main/1]).

-include_lib("kernel/include/file.hrl").

%% erl_tar (plan 21 step LIB54D): archives written to files, compressed and
%% through init/3 funs over a ram file, with fixed times, modes and owners so
%% their bytes compare by digest; long names split into USTAR prefixes or
%% moved to pax headers; tables, t/1, extraction to memory and disk with
%% files, cwd, keep_old_files, max_size and verbose; hand-made v7, GNU (long
%% names, sparse files, base-256 numbers), STAR and pax archives; errors and
%% format_error/1. Files are made in a directory under the working directory
%% and removed again.
main([]) ->
    Dir = "tar_ops.dir",
    _ = file:del_dir_r(Dir),
    ok = file:make_dir(Dir),
    put(dir, Dir),
    writing(),
    names(),
    reading(),
    extraction(),
    user_io(),
    formats(),
    errors(),
    ok = file:del_dir_r(Dir),
    io:format("tar_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

path(Name) -> filename:join(get(dir), Name).

-define(TIME, 1700000000).
-define(FIXED, [
    {mtime, ?TIME}, {atime, ?TIME}, {ctime, ?TIME}, {mode, 8#100640}, {uid, 1000}, {gid, 100}
]).

digest(File) ->
    {ok, Bin} = file:read_file(File),
    bin_digest(Bin).

bin_digest(Bin) -> {byte_size(Bin), binary:encode_hex(erlang:md5(Bin))}.

text(N) -> iolist_to_binary([integer_to_list(I rem 97) || I <- lists:seq(1, N)]).

write(Name, Bin) ->
    File = path(Name),
    ok = filelib:ensure_dir(File),
    ok = file:write_file(File, Bin).

writing() ->
    Tar = path("bins.tar"),
    Files = [
        {"a.txt", <<"alpha">>}, {"dir/b.txt", text(700)}, {"empty", <<>>}, {"block", text(512)}
    ],
    show(create, erl_tar:create(Tar, Files, ?FIXED)),
    show(created, digest(Tar)),
    show(table, erl_tar:table(Tar)),
    show(table_verbose, erl_tar:table(Tar, [verbose])),
    show(t, erl_tar:t(Tar)),
    Gz = path("bins.tar.gz"),
    show(create_compressed, erl_tar:create(Gz, Files, [compressed | ?FIXED])),
    show(compressed_table, erl_tar:table(Gz, [compressed])),
    {ok, GzBin} = file:read_file(Gz),
    show(compressed_binary, erl_tar:table({binary, GzBin}, [compressed])),
    write("src/one.txt", <<"one">>),
    write("src/sub/two.txt", text(1000)),
    ok = file:make_dir(path("src/sub/empty")),
    Disk = path("disk.tar"),
    {ok, T} = erl_tar:open(Disk, [write]),
    show(add, [
        erl_tar:add(T, path("src/one.txt"), "one.txt", ?FIXED),
        erl_tar:add(T, {"renamed.txt", path("src/one.txt")}, ?FIXED),
        erl_tar:add(T, {"bin.txt", <<"in memory">>}, ?FIXED),
        erl_tar:add(T, path("src/sub/empty"), "empty_dir", ?FIXED),
        erl_tar:add(T, path("missing"), "missing", ?FIXED),
        erl_tar:close(T)
    ]),
    show(disk, [digest(Disk), erl_tar:table(Disk, [verbose])]),
    show(verbose_create, erl_tar:create(path("v.tar"), [{"v.txt", <<"v">>}], [verbose | ?FIXED])),
    show(nested, erl_tar:create(path("nested.tar"), [{"tree", path("src/sub")}], ?FIXED)),
    show(nested_table, lists:sort(element(2, erl_tar:table(path("nested.tar"))))).

names() ->
    Long = lists:flatten(lists:duplicate(12, "directory/")) ++ "file.txt",
    Unsplittable = lists:duplicate(120, $n),
    Accented = [$c, $a, $f, 16#e9],
    Tar = path("names.tar"),
    Files = [{Long, <<"long">>}, {Unsplittable, <<"pax">>}, {Accented, <<"utf8">>}],
    show(names_create, erl_tar:create(Tar, Files, ?FIXED)),
    {ok, Names} = erl_tar:table(Tar),
    show(names, [digest(Tar), Names =:= [Long, Unsplittable, Accented]]),
    show(
        big_owner,
        erl_tar:create(
            path("owner.tar"), [{"o", <<"o">>}], ?FIXED ++ [{uid, 8#10000000}, {gid, 1 bsl 40}]
        )
    ),
    show(big_owner_table, [digest(path("owner.tar")), erl_tar:table(path("owner.tar"), [verbose])]),
    show(
        long_name,
        outcome(fun() ->
            erl_tar:create(path("x.tar"), [{lists:duplicate(300, $a), <<>>}], ?FIXED)
        end)
    ).

reading() ->
    Tar = path("bins.tar"),
    {ok, Bin} = file:read_file(Tar),
    show(binary_table, erl_tar:table({binary, Bin})),
    {ok, Fd} = file:open(Tar, [read, raw, binary]),
    show(fd_table, erl_tar:table({file, Fd})),
    {ok, Reader} = erl_tar:open(Tar, [read]),
    show(reader, [erl_tar:extract(Reader, [memory, {files, ["a.txt"]}]), erl_tar:close(Reader)]),
    show(cooked, erl_tar:table(Tar, [cooked])).

extraction() ->
    Tar = path("bins.tar"),
    {ok, Memory} = erl_tar:extract(Tar, [memory]),
    show(memory, [{N, byte_size(B)} || {N, B} <- Memory]),
    show(memory_files, erl_tar:extract(Tar, [memory, {files, ["empty", "a.txt", "nope"]}])),
    Out = path("out"),
    show(extract, erl_tar:extract(Tar, [{cwd, Out}])),
    show(extracted, [
        file:read_file(filename:join(Out, "a.txt")),
        file:read_file(filename:join(Out, "empty")),
        mtime(filename:join(Out, "dir/b.txt"))
    ]),
    ok = file:write_file(filename:join(Out, "a.txt"), <<"changed">>),
    show(keep_old_files, [
        erl_tar:extract(Tar, [{cwd, Out}, keep_old_files, verbose]),
        file:read_file(filename:join(Out, "a.txt"))
    ]),
    show(verbose_extract, erl_tar:extract(Tar, [{cwd, Out}, verbose, {files, ["a.txt"]}])),
    show(max_size, [
        names(erl_tar:extract(Tar, [memory, {max_size, 3000}])),
        erl_tar:extract(Tar, [memory, {max_size, 100}])
    ]),
    {ok, GzBin} = file:read_file(path("bins.tar.gz")),
    show(compressed_max_size, [
        names(erl_tar:extract({binary, GzBin}, [memory, compressed, {max_size, 10000}])),
        erl_tar:extract({binary, GzBin}, [memory, compressed, {max_size, 1000}])
    ]),
    show(
        chunks, erl_tar:extract(Tar, [{cwd, path("out2")}, {chunks, 100}, {files, ["dir/b.txt"]}])
    ),
    show(chunked_copy, file:read_file(path("out2/dir/b.txt")) =:= {ok, text(700)}),
    show(disk_dir, erl_tar:extract(path("disk.tar"), [{cwd, path("out3")}])),
    show(dir_made, filelib:is_dir(path("out3/empty_dir"))).

mtime(File) ->
    {ok, #file_info{mtime = M}} = file:read_file_info(File, [{time, posix}]),
    M.

% erl_tar:init/3 over a ram file opened for reading and writing.
user_io() ->
    {ok, Fd} = file:open(<<>>, [ram, read, write, binary]),
    Fun = fun
        (write, {F, Data}) -> file:write(F, Data);
        (position, {F, Pos}) -> file:position(F, Pos);
        (read2, {F, Size}) -> file:read(F, Size);
        (close, _F) -> ok
    end,
    {ok, W} = erl_tar:init(Fd, write, Fun),
    ok = erl_tar:add(W, {"x.txt", <<"through funs">>}, ?FIXED),
    ok = erl_tar:close(W),
    {ok, Size} = file:position(Fd, eof),
    {ok, Archive} = file:pread(Fd, 0, Size),
    show(init_write, [bin_digest(Archive), erl_tar:extract({binary, Archive}, [memory])]),
    {ok, 0} = file:position(Fd, bof),
    {ok, R} = erl_tar:init(Fd, read, Fun),
    show(init_read, erl_tar:extract(R, [memory])),
    show(init_bad, erl_tar:init(Fd, read, not_a_fun)).

%% Hand-made headers: Fields at their offsets, then the checksum.
header(Fields) ->
    Block = lists:foldl(fun({At, Value}, B) -> put_field(B, At, Value) end, <<0:4096>>, Fields),
    Checksum = lists:sum(binary_to_list(put_field(Block, 148, <<"        ">>))),
    put_field(Block, 148, iolist_to_binary(io_lib:format("~6.8.0B", [Checksum]) ++ [0, $\s])).

put_field(Block, At, Value) ->
    Bin = iolist_to_binary(Value),
    <<Before:At/binary, _:(byte_size(Bin))/binary, After/binary>> = Block,
    <<Before/binary, Bin/binary, After/binary>>.

octal(N, Width) -> io_lib:format("~*.8.0B", [Width - 1, N]) ++ [0].

entry(Name, Type, Size, Extra) ->
    header([
        {0, Name},
        {100, octal(8#644, 8)},
        {108, octal(0, 8)},
        {116, octal(0, 8)},
        {124, octal(Size, 12)},
        {136, octal(?TIME, 12)},
        {156, [Type]}
        | Extra
    ]).

data(Bin) ->
    Pad = (512 - byte_size(Bin) rem 512) rem 512,
    <<Bin/binary, 0:(Pad * 8)>>.

archive(Members) -> iolist_to_binary([Members, <<0:8192>>]).

formats() ->
    V7 = archive([entry("v7.txt", $0, 3, []), data(<<"old">>)]),
    show(v7, erl_tar:extract({binary, V7}, [memory])),
    Gnu = {257, <<"ustar  ", 0>>},
    LongName = lists:duplicate(150, $L),
    GnuLong = archive([
        entry("././@LongLink", $L, 151, [Gnu]),
        data(list_to_binary(LongName ++ [0])),
        entry("short", $0, 2, [Gnu]),
        data(<<"gl">>)
    ]),
    show(gnu_longname, [
        N =:= LongName
     || {N, _} <- element(2, erl_tar:extract({binary, GnuLong}, [memory]))
    ]),
    % A sparse file of 20 bytes: 3 bytes at 5, 2 bytes at 15.
    Sparse = [
        {386, octal(5, 12)},
        {398, octal(3, 12)},
        {410, octal(15, 12)},
        {422, octal(2, 12)},
        {483, octal(20, 12)}
    ],
    GnuSparse = archive([entry("sparse", $S, 5, [Gnu | Sparse]), data(<<"abcde">>)]),
    show(gnu_sparse, erl_tar:extract({binary, GnuSparse}, [memory])),
    show(gnu_sparse_table, erl_tar:table({binary, GnuSparse}, [verbose])),
    Base256 = archive([
        header([{0, "b256"}, {100, octal(8#644, 8)}, {124, <<16#80, 0:80, 4>>}, {156, "0"}]),
        data(<<"base">>)
    ]),
    show(base256, erl_tar:extract({binary, Base256}, [memory])),
    % OTP reads base-256 fields inverted unless bit 6 is set: an 8-byte owner field gives a negative number.
    Uid256 = archive([
        header([
            {0, "u256"},
            {100, octal(8#644, 8)},
            {108, <<16#80, 0:48, 5>>},
            {124, octal(1, 12)},
            {156, "0"}
        ]),
        data(<<"u">>)
    ]),
    show(base256_owner, erl_tar:table({binary, Uid256}, [verbose])),
    Star = archive([
        entry("star.txt", $0, 1, [
            {257, <<"ustar", 0>>}, {263, "00"}, {345, "prefix"}, {508, <<"tar", 0>>}
        ]),
        data(<<"s">>)
    ]),
    show(star, erl_tar:table({binary, Star}, [verbose])),
    Ustar = [{257, <<"ustar", 0>>}, {263, "00"}],
    PaxData =
        <<"30 mtime=1600000000.123456789\n", "18 uid=4294967296\n", "21 path=pax/name.txt\n",
            "15 uname=user\n">>,
    Pax = archive([
        entry("PaxHeaders.0/x", $x, byte_size(PaxData), Ustar),
        data(PaxData),
        entry("x", $0, 1, Ustar),
        data(<<"p">>)
    ]),
    show(pax, erl_tar:table({binary, Pax}, [verbose])),
    Links = archive([
        entry("dir/", $5, 0, Ustar), entry("hard", $1, 0, [{157, "dir/target"} | Ustar])
    ]),
    show(links, [
        erl_tar:table({binary, Links}, [verbose]), erl_tar:extract({binary, Links}, [memory])
    ]).

errors() ->
    Tar = path("bins.tar"),
    {ok, Bin} = file:read_file(Tar),
    <<Head:100/binary, _:8, Rest/binary>> = Bin,
    show(bad_checksum, erl_tar:table({binary, <<Head/binary, $!, Rest/binary>>})),
    show(truncated, erl_tar:table({binary, binary:part(Bin, 0, 700)})),
    show(no_end, erl_tar:table({binary, binary:part(Bin, 0, 1024)})),
    show(half_end, erl_tar:table({binary, <<(entry("e", $0, 0, []))/binary, 0:4096>>})),
    show(bad_end, erl_tar:table({binary, <<(entry("e", $0, 0, []))/binary, 0:4096, 1, 0:4088>>})),
    show(empty, erl_tar:table({binary, <<>>})),
    show(
        unsafe,
        erl_tar:extract({binary, archive([entry("../up.txt", $0, 1, []), data(<<"u">>)])}, [
            {cwd, path("out4")}
        ])
    ),
    show(missing, [erl_tar:table(path("none.tar")), erl_tar:extract(path("none.tar"))]),
    show(open_errors, [
        erl_tar:open(path("x.tar"), [bogus]),
        erl_tar:open({file, device}, [read, compressed]),
        erl_tar:open({file, device}, [read, cooked])
    ]),
    {ok, R} = erl_tar:open(Tar, [read]),
    show(add_to_reader, erl_tar:add(R, {"x", <<>>}, [])),
    ok = erl_tar:close(R),
    show(close_bad, erl_tar:close(not_a_tar)),
    show(create_missing, erl_tar:create(path("c.tar"), [path("nope.txt")])),
    show(format_error, [
        erl_tar:format_error(Reason)
     || Reason <- [
            invalid_tar_checksum,
            bad_header,
            {bad_header, why},
            eof,
            too_big,
            unsafe_path,
            {misaligned_read, 7},
            {invalid_gnu_0_1_sparsemap, "0.1"},
            {"name.txt", enoent},
            enoent,
            {weird, term, here}
        ]
    ]).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

names({ok, Files}) -> [Name || {Name, _} <- Files];
names(Other) -> Other.
