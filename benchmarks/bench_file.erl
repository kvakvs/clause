%% File I/O benchmarks (plan step 69A); the driver runs them in an empty temporary working directory.
-module(bench_file).
-export([main/1]).

-define(CHUNK_SIZE, 65536).

main(_Args) ->
    Chunk = bench_util:bytes(?CHUNK_SIZE),
    Lines = [[integer_to_list(N), " line of text\n"] || N <- lists:seq(1, 20000)],
    ok = file:write_file("lines.txt", Lines),
    bench_util:run("file.small_files", 11, fun() -> small_files(500) end),
    bench_util:run("file.large_write", 250, fun() -> large_write("large.bin", Chunk, 256) end),
    bench_util:run("file.large_read", 370, fun() -> large_read("large.bin") end),
    bench_util:run("file.read_line", 52, fun() -> read_lines("lines.txt") end),
    bench_util:run("file.read_file", 330, fun() -> {ok, _} = file:read_file("large.bin") end),
    ok = file:delete("large.bin"),
    ok = file:delete("lines.txt"),
    bench_util:done("bench_file").

%% Write, read back and delete Count small files.
small_files(Count) ->
    Names = ["small_" ++ integer_to_list(N) ++ ".txt" || N <- lists:seq(1, Count)],
    [ok = file:write_file(Name, Name) || Name <- Names],
    [{ok, _} = file:read_file(Name) || Name <- Names],
    [ok = file:delete(Name) || Name <- Names],
    ok.

large_write(Name, Chunk, Count) ->
    {ok, File} = file:open(Name, [write, binary]),
    write_chunks(File, Chunk, Count),
    ok = file:close(File).

write_chunks(_File, _Chunk, 0) ->
    ok;
write_chunks(File, Chunk, N) ->
    ok = file:write(File, Chunk),
    write_chunks(File, Chunk, N - 1).

large_read(Name) ->
    {ok, File} = file:open(Name, [read, binary]),
    Size = read_chunks(File, 0),
    ok = file:close(File),
    Size.

read_chunks(File, Size) ->
    case file:read(File, ?CHUNK_SIZE) of
        {ok, Data} -> read_chunks(File, Size + byte_size(Data));
        eof -> Size
    end.

read_lines(Name) ->
    {ok, File} = file:open(Name, [read, binary]),
    Count = count_lines(File, 0),
    ok = file:close(File),
    Count.

count_lines(File, Count) ->
    case file:read_line(File) of
        {ok, _Line} -> count_lines(File, Count + 1);
        eof -> Count
    end.
