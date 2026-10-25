-module(file_compressed).
-export([main/1]).

%% file:open/2 with compressed and compressed_one (plan 21 step LIB50C): gzip
%% written and read back, lines, positions, plain files read as they are,
%% concatenated members, io requests and the operations the layer refuses; on
%% plain files CRLF line endings and a read that stops the file server.
%% Compressed bytes are checked through zlib only (their exact bytes depend on
%% the platform's zlib). Files are made in the working directory and removed.
main([]) ->
    Name = "file_compressed.gz",
    Plain = "file_compressed.txt",
    written(Name),
    read_back(Name),
    positions(Name),
    lines(Name),
    plain(Plain),
    members(Name),
    io_requests(Name),
    refused(Name),
    plain_lines(Plain),
    [ok = file:delete(F) || F <- [Name, Plain]],
    io:format("file_compressed: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

text() ->
    <<"first line\nsecond line\r\nthird\n", (binary:copy(<<"0123456789">>, 2000))/binary,
        "\nlast">>.

written(Name) ->
    {ok, F} = file:open(Name, [write, compressed, binary]),
    Text = text(),
    <<A:20/binary, B/binary>> = Text,
    show(write, [file:write(F, A), file:position(F, cur), file:write(F, [B]), file:position(F, cur)]),
    show(write_position, [file:position(F, {cur, -1}), file:position(F, eof), file:position(F, bad)]),
    show(write_read, [file:read(F, 1), file:read_line(F)]),
    ok = file:close(F),
    {ok, Raw} = file:read_file(Name),
    show(gzip, [binary:part(Raw, 0, 3), zlib:gunzip(Raw) =:= Text, byte_size(Raw) < byte_size(Text)]).

read_back(Name) ->
    {ok, F} = file:open(Name, [read, compressed, binary]),
    show(read, [file:read(F, 5), file:position(F, cur), file:read(F, 0), file:read(F, 6)]),
    {ok, Rest} = file:read(F, 100000),
    show(read_rest, [byte_size(Rest), file:read(F, 1), file:position(F, cur)]),
    ok = file:close(F),
    {ok, L} = file:open(Name, [read, compressed]),
    show(list_mode, [file:read(L, 5), file:read_line(L)]),
    ok = file:close(L).

positions(Name) ->
    {ok, F} = file:open(Name, [read, compressed, binary]),
    show(positions, [
        file:position(F, 11),
        file:read(F, 6),
        file:position(F, {cur, -6}),
        file:read(F, 6),
        file:position(F, bof),
        file:read(F, 5),
        file:position(F, 15000),
        file:read(F, 3),
        file:position(F, 100000),
        file:read(F, 1),
        file:position(F, eof),
        file:position(F, {eof, 0}),
        file:position(F, -1),
        file:position(F, {bof, x}),
        file:position(F, {cur, 2})
    ]),
    ok = file:close(F).

lines(Name) ->
    {ok, F} = file:open(Name, [read, compressed, binary]),
    First = [file:read_line(F), file:read_line(F), file:position(F, cur), file:read_line(F)],
    {ok, Long} = file:read_line(F),
    show(
        lines,
        First ++ [byte_size(Long), file:read_line(F), file:position(F, cur), file:read_line(F)]
    ),
    ok = file:close(F).

plain(Plain) ->
    ok = file:write_file(Plain, <<"not gzip\nat all">>),
    {ok, F} = file:open(Plain, [read, compressed, binary]),
    show(plain, [file:read_line(F), file:pread(F, 0, 3), file:read(F, 100), file:position(F, 4)]),
    ok = file:close(F),
    ok = file:write_file(Plain, <<>>),
    {ok, E} = file:open(Plain, [read, compressed, binary]),
    show(empty, [file:read(E, 1), file:read_line(E)]),
    ok = file:close(E).

members(Name) ->
    ok = file:write_file(Name, [zlib:gzip(<<"one,">>), zlib:gzip(<<"two">>)]),
    {ok, F} = file:open(Name, [read, compressed, binary]),
    show(members, [file:read(F, 100)]),
    ok = file:close(F),
    {ok, G} = file:open(Name, [read, compressed_one, binary]),
    show(member_one, [file:read(G, 100), file:read(G, 100)]),
    ok = file:close(G).

io_requests(Name) ->
    {ok, W} = file:open(Name, [write, compressed]),
    ok = io:format(W, "~p.~n~s~n", [{term, 1}, "a line"]),
    ok = io:put_chars(W, "more\n"),
    ok = file:close(W),
    {ok, R} = file:open(Name, [read, compressed]),
    show(io, [
        io:read(R, ''),
        io:get_line(R, ''),
        io:get_line(R, ''),
        io:get_chars(R, '', 2),
        io:get_line(R, '')
    ]),
    show(io_end, [io:get_line(R, ''), io:get_chars(R, '', 1)]),
    ok = file:close(R).

refused(Name) ->
    show(open_errors, [
        file:open(Name, [read, write, compressed]),
        file:open(Name, [append, compressed]),
        file:open(Name, [write, append, compressed])
    ]),
    ok = file:write_file(Name, zlib:gzip(<<"abcdef">>)),
    {ok, F} = file:open(Name, [read, compressed, binary]),
    show(server, [
        file:pread(F, 2, 2),
        file:position(F, cur),
        file:pwrite(F, 0, "x"),
        file:read(F, 1)
    ]),
    {ok, G} = file:open(Name, [read, compressed, binary]),
    show(refusals, [file:allocate(G, 0, 1), file:read(G, 1), file:sync(G), file:read(G, 1)]),
    {ok, R} = file:open(Name, [read, raw, compressed, binary]),
    show(raw, [
        file:pread(R, 0, 1),
        file:pwrite(R, 0, "x"),
        file:truncate(R),
        file:write(R, "x"),
        file:read(R, 2),
        file:sync(R),
        file:read(R, 2)
    ]),
    ok = file:close(R),
    {ok, W} = file:open(Name, [write, compressed, binary]),
    show(written_refusals, [
        file:read(W, 1), file:read_line(W), file:truncate(W), file:write(W, "x")
    ]).

plain_lines(Plain) ->
    ok = file:write_file(Plain, <<
        "a\n"
        "b\n"
        "\n"
        "c"
    >>),
    {ok, F} = file:open(Plain, [read, binary]),
    show(crlf, [
        file:read_line(F),
        file:position(F, cur),
        file:read_line(F),
        io:get_line(F, ''),
        file:read_line(F)
    ]),
    ok = file:close(F),
    {ok, R} = file:open(Plain, [read, raw]),
    show(crlf_raw, [file:read_line(R), file:read_line(R)]),
    ok = file:close(R),
    {ok, W} = file:open(Plain, [write]),
    show(read_write_only, [file:read(W, 1), file:read(W, 1)]),
    {ok, L} = file:open(Plain, [write]),
    show(read_line_write_only, [file:read_line(L), file:write(L, "x")]).
