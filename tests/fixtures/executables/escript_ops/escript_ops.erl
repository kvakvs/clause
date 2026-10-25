-module(escript_ops).
-export([main/1]).

-include_lib("kernel/include/file.hrl").

%% escript (plan 21 step LIB55): escripts made with create/2 in memory and
%% on disk (shebang, comment and emulator argument lines, source, beam and
%% archive bodies) and taken apart again with extract/2, OTP's header reading
%% quirks, and errors. Files are made in a directory under the working
%% directory and removed again.
main([]) ->
    Dir = "escript_ops.dir",
    _ = file:del_dir_r(Dir),
    ok = file:make_dir(Dir),
    put(dir, Dir),
    creating(),
    extracting(),
    errors(),
    show(script_name, is_list(escript:script_name())),
    ok = file:del_dir_r(Dir),
    io:format("escript_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, Value]).

path(Name) -> filename:join(get(dir), Name).

digest(Bin) -> {byte_size(Bin), binary:encode_hex(erlang:md5(Bin))}.

source() -> <<"main(_) -> io:format(\"hi~n\").\n">>.

info(Size) ->
    #file_info{
        size = Size,
        type = regular,
        access = read_write,
        mtime = 1700000000,
        atime = 1700000000,
        ctime = 1700000000,
        mode = 8#100644,
        links = 1,
        uid = 0,
        gid = 0
    }.

creating() ->
    show(
        source_default,
        escript:create(binary, [shebang, comment, {emu_args, "-smp enable"}, {source, source()}])
    ),
    show(
        custom_lines,
        escript:create(binary, [
            {shebang, "/opt/bin/escript"}, {comment, "Made by a test"}, {source, source()}
        ])
    ),
    show(
        undefined_lines,
        escript:create(binary, [
            {shebang, undefined}, {comment, undefined}, {emu_args, undefined}, {source, source()}
        ])
    ),
    show(beam_body, escript:create(binary, [shebang, {beam, <<"FOR1fake beam">>}])),
    Files = [
        {"main.beam", <<"beam bytes">>, info(10)}, {"priv/data.txt", <<"some data">>, info(9)}
    ],
    {ok, Archive} = escript:create(binary, [
        shebang, comment, {emu_args, "-escript main main"}, {archive, Files, []}
    ]),
    show(archive_body, [digest(Archive), binary:part(Archive, 0, 60)]),
    ok = file:write_file(path("body.src"), source()),
    show(
        body_from_file,
        escript:create(path("from_file.escript"), [shebang, {source, path("body.src")}])
    ),
    show(written, file:read_file(path("from_file.escript"))).

extracting() ->
    write("full.escript", [shebang, comment, {emu_args, "-smp enable"}, {source, source()}]),
    show(full, escript:extract(path("full.escript"), [])),
    write("custom.escript", [
        {shebang, "/opt/bin/escript -x"}, {comment, "%% doubled"}, {source, source()}
    ]),
    show(custom, escript:extract(path("custom.escript"), [])),
    write("emu_only.escript", [shebang, {emu_args, "-noshell"}, {source, source()}]),
    show(emu_only, escript:extract(path("emu_only.escript"), [])),
    write("bare.escript", [shebang, {source, source()}]),
    show(bare, escript:extract(path("bare.escript"), [])),
    Files = [{"main.beam", <<"beam bytes">>, info(10)}],
    write("archive.escript", [shebang, comment, {archive, Files, []}]),
    {ok, [_, _, _, {archive, Zip}]} = escript:extract(path("archive.escript"), []),
    show(archive, zip:unzip(Zip, [memory])),
    write("beam.escript", [{beam, <<"FOR1 beam body">>}]),
    show(beam, escript:extract(path("beam.escript"), [])),
    ok = file:write_file(path("no_shebang.escript"), <<"%% first\nmain(_) -> ok.\n">>),
    show(no_shebang, escript:extract(path("no_shebang.escript"), [])),
    ok = file:write_file(path("archive_first.escript"), <<"PK rest of a zip\nmore\n">>),
    show(archive_first, escript:extract(path("archive_first.escript"), [])).

write(Name, Options) -> ok = escript:create(path(Name), Options).

errors() ->
    show(create_errors, [
        escript:create(binary, [shebang]),
        escript:create(binary, [{text, <<"x">>}]),
        escript:create(binary, [{source, path("missing.erl")}]),
        escript:create(binary, [bogus, {source, <<"x">>}]),
        escript:create(binary, not_a_list_tail()),
        escript:create(binary, [{archive, [{"a", not_binary}], []}]),
        escript:create(path("no/such/dir/x.escript"), [{source, <<"x">>}])
    ]),
    ok = file:write_file(path("short.escript"), <<"#!/usr/bin/env escript\n">>),
    show(extract_errors, [
        escript:extract(path("nope.escript"), []),
        escript:extract(path("full.escript"), [bogus]),
        escript:extract(path("short.escript"), [])
    ]).

not_a_list_tail() -> [{source, <<"x">>} | improper].
