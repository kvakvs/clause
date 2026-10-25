-module(beam_ops).
-export([main/1]).

%% beam_lib (plan 21 step LIB54F): BEAM files built in the test from chunks
%% (both atom table encodings, attributes, compile information, debug
%% information, documentation, literals, funs), read back with info/1,
%% chunks/2,3 and all_chunks/1, compared, stripped (also gzip-compressed
%% beams), versions and MD5s, directory comparisons, the crypto key server
%% and errors. Files are made in a directory under the working directory and
%% removed again.
main([]) ->
    Dir = "beam_ops.dir",
    _ = file:del_dir_r(Dir),
    ok = file:make_dir(Dir),
    put(dir, Dir),
    reading(),
    files(),
    stripping(),
    directories(),
    crypto(),
    errors(),
    ok = file:del_dir_r(Dir),
    io:format("beam_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, clean(Value)]).

%% Beams (binaries over 64 bytes) by size.
clean(Bin) when is_binary(Bin), byte_size(Bin) > 64 -> {binary, byte_size(Bin)};
clean(List) when is_list(List) -> [clean(E) || E <- List];
clean(Tuple) when is_tuple(Tuple) -> list_to_tuple(clean(tuple_to_list(Tuple)));
clean(Other) -> Other.

path(Name) -> filename:join(get(dir), Name).

digest(Bin) -> {byte_size(Bin), binary:encode_hex(erlang:md5(Bin))}.

hex_md5({ok, {Module, MD5}}) -> {ok, {Module, binary:encode_hex(MD5)}};
hex_md5(Other) -> Other.

%% The atom table: a count then length-prefixed names, or (OTP 28 and later)
%% a negative count then names with compact-term lengths.
atoms_old(Atoms) -> [<<(length(Atoms)):32>> | [[byte_size(A), A] || A <- Atoms]].

atoms_new(Atoms) ->
    [<<(-length(Atoms)):32/signed>> | [[compact_length(byte_size(A)), A] || A <- Atoms]].

compact_length(N) when N < 16 -> <<N:4, 0:1, 0:3>>;
compact_length(N) -> <<(N bsr 8):3, 0:1, 1:1, 0:3, (N band 255)>>.

table(Entries) -> [<<(length(Entries)):32>> | [<<A:32, B:32, C:32>> || {A, B, C} <- Entries]].

literals(Terms) ->
    Bins = [term_to_binary(T) || T <- Terms],
    [<<0:32, (length(Bins)):32>> | [[<<(byte_size(B)):32>>, B] || B <- Bins]].

forms(Module) ->
    [
        {attribute, 1, module, Module},
        {attribute, 2, export, [{go, 0}]},
        {function, 3, go, 0, [{clause, 3, [], [], [{atom, 3, ok}]}]},
        {eof, 4}
    ].

docs() ->
    {docs_v1, 1, erlang, <<"application/erlang+html">>, #{<<"en">> => <<"A module.">>}, #{}, [
        {{function, go, 0}, 3, [<<"go()">>], #{<<"en">> => <<"Go.">>}, #{}}
    ]}.

%% A module's chunks: atoms (old or new encoding), code, strings, imports,
%% exports, locals, funs, literals, attributes, compile information, debug
%% information and documentation.
chunks(Module, Encoding) ->
    Atoms = [atom_to_binary(Module), <<"go">>, <<"erlang">>, <<"display">>, <<"helper">>],
    AtomChunk =
        case Encoding of
            old -> {"AtU8", iolist_to_binary(atoms_old(Atoms))};
            new -> {"AtU8", iolist_to_binary(atoms_new(Atoms))}
        end,
    [
        AtomChunk,
        {"Code", <<16:32, 0:32, 178:32, 5:32, 2:32, 1, 2, 3, 4, 5, 6, 7>>},
        {"StrT", <<"strings">>},
        {"ImpT", iolist_to_binary(table([{3, 4, 1}]))},
        {"ExpT", iolist_to_binary(table([{2, 0, 2}]))},
        {"LocT", iolist_to_binary(table([{5, 1, 4}]))},
        {"FunT", <<1:32, 5:32, 1:32, 4:32, 0:32, 0:32, 12345:32>>},
        {"LitT", iolist_to_binary(literals([{a, tuple}, "a string", 3.5]))},
        {"Attr",
            term_to_binary([
                {vsn, [1234]}, {author, [someone]}, {behaviour, [gen_server]}, {author, [other]}
            ])},
        {"CInf", term_to_binary([{version, "9.0"}, {options, [debug_info]}])},
        {"Dbgi", term_to_binary({debug_info_v1, erl_abstract_code, {forms(Module), [debug_info]}})},
        {"Docs", term_to_binary(docs())},
        {"Line", <<0:32, 1:32, 0:32, 1:32, 1:32, 1, 2, 3>>}
    ].

build(Module, Encoding) ->
    {ok, Beam} = beam_lib:build_module(chunks(Module, Encoding)),
    Beam.

reading() ->
    Old = build(old_mod, old),
    New = build(new_mod, new),
    show(built, [digest(Old), digest(New)]),
    show(info, [beam_lib:info(Old) -- [{binary, Old}], beam_lib:info(New) -- [{binary, New}]]),
    show(
        symbols,
        beam_lib:chunks(New, [
            atoms, exports, labeled_exports, imports, indexed_imports, locals, labeled_locals
        ])
    ),
    show(old_atoms, beam_lib:chunks(Old, [atoms])),
    show(attributes, beam_lib:chunks(Old, [attributes, compile_info])),
    show(literals, beam_lib:chunks(Old, [literals])),
    show(debug, beam_lib:chunks(New, [debug_info, abstract_code])),
    show(documentation, beam_lib:chunks(New, [documentation])),
    show(raw_ids, beam_lib:chunks(Old, ["StrT", "Code"])),
    show(all_chunks, [{Id, byte_size(B)} || {Id, B} <- element(3, beam_lib:all_chunks(Old))]),
    show(version, beam_lib:version(Old)),
    show(md5, [hex_md5(beam_lib:md5(Old)), hex_md5(beam_lib:md5(New))]),
    show(significant_chunks, beam_lib:significant_chunks()),
    NoDebug = build_without(new_mod, ["Dbgi", "Docs"]),
    show(missing_optional, beam_lib:chunks(NoDebug, [abstract_code])),
    show(missing_documentation, beam_lib:chunks(NoDebug, [documentation])),
    show(
        allow_missing,
        beam_lib:chunks(NoDebug, [documentation, "Docs", attributes], [allow_missing_chunks])
    ).

build_without(Module, Ids) ->
    {ok, Beam} = beam_lib:build_module([
        C
     || {Id, _} = C <- chunks(Module, new), not lists:member(Id, Ids)
    ]),
    Beam.

files() ->
    File = path("file_mod.beam"),
    ok = file:write_file(File, build(file_mod, new)),
    show(file_info, beam_lib:info(File)),
    show(file_chunks, beam_lib:chunks(path("file_mod"), [exports, attributes])),
    show(file_version, beam_lib:version(File)),
    Gz = path("gz_mod.beam"),
    ok = file:write_file(Gz, zlib:gzip(build(gz_mod, old))),
    show(gzipped, [beam_lib:chunks(Gz, [exports]), hex_md5(beam_lib:md5(Gz))]),
    show(cmp, [
        beam_lib:cmp(build(same, new), build(same, new)),
        beam_lib:cmp(build(one, new), build(two, new)),
        beam_lib:cmp(build(same, new), build_without(same, ["Docs"])),
        beam_lib:cmp(build(same, new), changed(build(same, new)))
    ]).

%% A beam whose string table differs.
changed(Beam) ->
    {ok, _, Chunks} = beam_lib:all_chunks(Beam),
    {ok, New} = beam_lib:build_module(lists:keyreplace("StrT", 1, Chunks, {"StrT", <<"STRINGS">>})),
    New.

stripping() ->
    Beam = build(strip_mod, new),
    {ok, {strip_mod, Stripped}} = beam_lib:strip(Beam),
    show(stripped, [
        digest(Stripped), [Id || {Id, _, _} <- proplists:get_value(chunks, beam_lib:info(Stripped))]
    ]),
    {ok, {strip_mod, Kept}} = beam_lib:strip(Beam, ["Attr", "Docs"]),
    show(stripped_keeping, [Id || {Id, _, _} <- proplists:get_value(chunks, beam_lib:info(Kept))]),
    File = path("strip_file.beam"),
    ok = file:write_file(File, build(strip_file, old)),
    show(strip_file, [
        beam_lib:strip(File), [Id || {Id, _, _} <- proplists:get_value(chunks, beam_lib:info(File))]
    ]),
    show(strip_files, beam_lib:strip_files([build(sf1, new), build(sf2, old)])),
    show(strip_missing_code, beam_lib:strip(build_without(nocode, ["Code"]))),
    Release = path("release"),
    ok = filelib:ensure_dir(filename:join([Release, "lib", "app-1", "ebin", "x"])),
    ok = file:write_file(
        filename:join([Release, "lib", "app-1", "ebin", "rel_mod.beam"]), build(rel_mod, new)
    ),
    show(strip_release, beam_lib:strip_release(Release)).

directories() ->
    A = path("a"),
    B = path("b"),
    ok = file:make_dir(A),
    ok = file:make_dir(B),
    write(A, "both", build(both, new)),
    write(B, "both", build(both, new)),
    write(A, "differ", build(differ, new)),
    write(B, "differ", changed(build(differ, new))),
    write(A, "only_a", build(only_a, new)),
    write(B, "only_b", build(only_b, new)),
    show(cmp_dirs, beam_lib:cmp_dirs(A, B)),
    show(diff_dirs, beam_lib:diff_dirs(A, B)),
    show(not_directory, beam_lib:cmp_dirs(path("nope"), B)).

write(Dir, Name, Beam) -> ok = file:write_file(filename:join(Dir, Name ++ ".beam"), Beam).

crypto() ->
    show(make_crypto_key, beam_lib:make_crypto_key(des3_cbc, "secret")),
    show(key_funs, [
        beam_lib:crypto_key_fun(not_a_fun),
        beam_lib:crypto_key_fun(fun(_) -> {error, nope} end),
        beam_lib:crypto_key_fun(fun
            (init) -> ok;
            (_) -> "key"
        end),
        beam_lib:crypto_key_fun(fun(init) -> ok end),
        beam_lib:clear_crypto_key_fun(),
        beam_lib:clear_crypto_key_fun()
    ]),
    Encrypted = encrypted(enc_mod),
    show(encrypted, [
        beam_lib:chunks(Encrypted, [debug_info]), beam_lib:chunks(Encrypted, [abstract_code])
    ]).

encrypted(Module) ->
    Dbgi = <<0, 8, "des3_cbc", "not really encrypted">>,
    {ok, Beam} = beam_lib:build_module(
        lists:keyreplace("Dbgi", 1, chunks(Module, new), {"Dbgi", Dbgi})
    ),
    Beam.

errors() ->
    Beam = build(err_mod, new),
    show(not_beam, [beam_lib:info(<<"not a beam">>), beam_lib:chunks(<<"FOR1">>, [atoms])]),
    show(file_errors, [beam_lib:info(path("none.beam")), beam_lib:chunks(path("none"), [atoms])]),
    show(unknown_chunk, beam_lib:chunks(Beam, [nonsense])),
    show(missing, [
        beam_lib:chunks(Beam, ["Zzzz"]),
        beam_lib:chunks(build_without(m, ["Attr"]), [attributes]),
        beam_lib:chunks(build_without(m, ["AtU8"]), [exports])
    ]),
    <<Head:20/binary, _/binary>> = Beam,
    show(truncated, [beam_lib:info(Head), beam_lib:chunks(binary:part(Beam, 0, 40), [exports])]),
    show(bad_head, beam_lib:info(<<"FOR1", 0:32, "BEAM", "AtU8">>)),
    {ok, Bad} = beam_lib:build_module([
        {"AtU8", iolist_to_binary(atoms_new([<<"bad">>]))}, {"Attr", <<"not a term">>}
    ]),
    show(invalid_chunk, [beam_lib:chunks(Bad, [attributes]), beam_lib:chunks(Bad, [exports])]),
    show(format_error, [
        lists:flatten(beam_lib:format_error(E))
     || E <- [
            {unknown_chunk, "f.beam", x},
            {invalid_chunk, "f.beam", "Attr"},
            {not_a_beam_file, "f.beam"},
            {file_error, "f.beam", enoent},
            {missing_chunk, "f.beam", "Atom"},
            {invalid_beam_file, "f.beam", 12},
            {chunk_too_big, "f.beam", "Code", 100, 10},
            {chunks_different, "Code"},
            different_chunks,
            {modules_different, a, b},
            {not_a_directory, "d"},
            {key_missing_or_invalid, "f.beam", debug_info},
            badfun,
            exists,
            {missing_backend, "f.beam", backend},
            {error, beam_lib, different_chunks},
            something_else
        ]
    ]).
