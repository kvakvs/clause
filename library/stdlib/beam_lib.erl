%% Clause's beam_lib (plan 21 step LIB54F, docs/libraries/library.md): reading, comparing, stripping and building BEAM files,
%% written from OTP's documentation and following OTP's chunk handling: the IFF chunk walk, symbol tables, attributes,
%% literals, debug information through its backend, MD5s over the significant chunks, gzip-compressed beams, and the
%% crypto key server for encrypted debug information (decryption needs crypto, which Clause programs lack).
-module(beam_lib).
-behaviour(gen_server).
-export([
    all_chunks/1,
    build_module/1,
    chunks/2,
    chunks/3,
    clear_crypto_key_fun/0,
    cmp/2,
    cmp_dirs/2,
    code_change/3,
    crypto_key_fun/1,
    diff_dirs/2,
    format_error/1,
    get_crypto_key/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    info/1,
    init/1,
    make_crypto_key/2,
    md5/1,
    significant_chunks/0,
    strip/1,
    strip/2,
    strip_files/1,
    strip_files/2,
    strip_release/1,
    strip_release/2,
    terminate/2,
    version/1
]).
-include("beam_lib.types.hrl").
-compile(nowarn_deprecated_catch).
-compile({no_auto_import, [error/1]}).
-compile({no_auto_import, [error/2]}).

-include_lib("kernel/include/eep48.hrl").

-import(lists, [append/1, delete/2, foreach/2, keysort/2, member/2, reverse/1, sort/1, splitwith/2]).

% --- The documented functions ---

info(File) -> read_info(beam_filename(File)).

chunks(File, Chunks) -> read_chunk_data(File, Chunks).

chunks(File, Chunks, Options) ->
    try
        read_chunk_data(File, Chunks, Options)
    catch
        Error -> Error
    end.

all_chunks(File) -> read_all_chunks(File).

cmp(File1, File2) ->
    try
        cmp_files(File1, File2)
    catch
        Error -> Error
    end.

cmp_dirs(Dir1, Dir2) -> catch compare_dirs(Dir1, Dir2).

diff_dirs(Dir1, Dir2) -> catch diff_directories(Dir1, Dir2).

strip(FileName) -> strip(FileName, []).

strip(FileName, AdditionalChunks) ->
    try
        strip_file(FileName, AdditionalChunks)
    catch
        Error -> Error
    end.

strip_files(Files) -> strip_files(Files, []).

strip_files(Files, AdditionalChunks) when is_list(Files) ->
    try
        strip_fils(Files, AdditionalChunks)
    catch
        Error -> Error
    end.

strip_release(Root) -> strip_release(Root, []).

strip_release(Root, AdditionalChunks) -> catch strip_rel(Root, AdditionalChunks).

version(File) ->
    case catch read_chunk_data(File, [attributes]) of
        {ok, {Module, [{attributes, Attrs}]}} ->
            {vsn, Version} = lists:keyfind(vsn, 1, Attrs),
            {ok, {Module, Version}};
        Error ->
            Error
    end.

% The MD5 of the chunks that make up a module's behaviour, the funs' old unique numbers zeroed.
md5(File) ->
    case catch read_significant_chunks(File, md5_chunks()) of
        {ok, {Module, Chunks0}} ->
            Chunks = filter_funtab(Chunks0),
            {ok, {Module, erlang:md5([C || {_Id, C} <- Chunks])}};
        Error ->
            Error
    end.

format_error({error, Error}) ->
    format_error(Error);
format_error({error, Module, Error}) ->
    Module:format_error(Error);
format_error({unknown_chunk, File, ChunkName}) ->
    io_lib:format("~tp: Cannot find chunk ~p~n", [File, ChunkName]);
format_error({invalid_chunk, File, ChunkId}) ->
    io_lib:format("~tp: Invalid contents of chunk ~p~n", [File, ChunkId]);
format_error({not_a_beam_file, File}) ->
    io_lib:format("~tp: Not a BEAM file~n", [File]);
format_error({file_error, File, Reason}) ->
    io_lib:format("~tp: ~tp~n", [File, file:format_error(Reason)]);
format_error({missing_chunk, File, ChunkId}) ->
    io_lib:format("~tp: Not a BEAM file: no IFF \"~s\" chunk~n", [File, ChunkId]);
format_error({invalid_beam_file, File, Pos}) ->
    io_lib:format("~tp: Invalid format of BEAM file near byte number ~p~n", [File, Pos]);
format_error({chunk_too_big, File, ChunkId, Size, Len}) ->
    io_lib:format("~tp: Size of chunk \"~s\" is ~p bytes, but only ~p bytes could be read~n", [File, ChunkId, Size, Len]);
format_error({chunks_different, Id}) ->
    io_lib:format("Chunk \"~s\" differs in the two files~n", [Id]);
format_error(different_chunks) ->
    "The two files have different chunks\n";
format_error({modules_different, Module1, Module2}) ->
    io_lib:format("Module names ~p and ~p differ in the two files~n", [Module1, Module2]);
format_error({not_a_directory, Name}) ->
    io_lib:format("~tp: Not a directory~n", [Name]);
format_error({key_missing_or_invalid, File, ChunkId}) ->
    io_lib:format("~tp: Cannot decrypt ~ts because key is missing or invalid", [File, ChunkId]);
format_error(badfun) ->
    "not a fun or the fun has the wrong arity";
format_error(exists) ->
    "a fun has already been installed";
format_error({missing_backend, File, Backend}) ->
    io_lib:format("~tp: Cannot retrieve abstract code because the backend ~p is missing", [File, Backend]);
format_error(E) ->
    io_lib:format("~tp~n", [E]).

crypto_key_fun(F) -> call_crypto_server({crypto_key_fun, F}).

clear_crypto_key_fun() -> call_crypto_server(clear_crypto_key_fun).

% The des3_cbc key, initialization vector and block size of a key string.
make_crypto_key(des3_cbc = Type, String) ->
    <<K1:8/binary, K2:8/binary>> = First = erlang:md5(String),
    <<K3:8/binary, IVec:8/binary>> = erlang:md5([First | reverse(String)]),
    {Type, [K1, K2, K3], IVec, 8}.

build_module(Chunks0) ->
    Chunks = list_to_binary(build_chunks(Chunks0)),
    Size = byte_size(Chunks),
    % The chunks are padded to whole words.
    0 = Size rem 4,
    {ok, <<"FOR1", (Size + 4):32, "BEAM", Chunks/binary>>}.

% --- Reading, comparing and stripping ---

read_info(File) ->
    try
        {ok, Module, Data} = scan_beam(File, info),
        [
            if
                is_binary(File) -> {binary, File};
                true -> {file, File}
            end,
            {module, Module},
            {chunks, Data}
        ]
    catch
        Error -> Error
    end.

diff_directories(Dir1, Dir2) ->
    {OnlyDir1, OnlyDir2, Diff} = compare_dirs(Dir1, Dir2),
    diff_only(Dir1, OnlyDir1),
    diff_only(Dir2, OnlyDir2),
    foreach(fun(D) -> io:format("** different: ~tp~n", [D]) end, Diff),
    ok.

diff_only(_Dir, []) -> ok;
diff_only(Dir, Only) -> io:format("Only in ~tp: ~tp~n", [Dir, Only]).

% The beams only in one directory or the other, and the pairs with the same name that differ.
compare_dirs(Dir1, Dir2) ->
    R1 = sofs:relation(beam_files(Dir1)),
    R2 = sofs:relation(beam_files(Dir2)),
    {O1, Both, O2} = sofs:symmetric_partition(sofs:domain(R1), sofs:domain(R2)),
    OnlyL1 = sofs:image(R1, O1),
    OnlyL2 = sofs:image(R2, O2),
    B1 = sofs:to_external(sofs:restriction(R1, Both)),
    B2 = sofs:to_external(sofs:restriction(R2, Both)),
    Diff = compare_files(B1, B2, []),
    {sofs:to_external(OnlyL1), sofs:to_external(OnlyL2), Diff}.

compare_files([], [], Acc) ->
    lists:reverse(Acc);
compare_files([{_, F1} | R1], [{_, F2} | R2], Acc) ->
    NAcc =
        case catch cmp_files(F1, F2) of
            {error, _Mod, _Reason} -> [{F1, F2} | Acc];
            ok -> Acc
        end,
    compare_files(R1, R2, NAcc).

beam_files(Dir) ->
    ok = assert_directory(Dir),
    [{filename:basename(Path), Path} || Path <- filelib:wildcard(filename:join(Dir, "*.beam"))].

cmp_files(File1, File2) ->
    {ok, {M1, L1}} = read_all_but_useless_chunks(File1),
    {ok, {M2, L2}} = read_all_but_useless_chunks(File2),
    if
        M1 =:= M2 -> cmp_lists(L1, L2);
        true -> error({modules_different, M1, M2})
    end.

cmp_lists([], []) ->
    ok;
cmp_lists([{Id, C1} | R1], [{Id, C2} | R2]) ->
    if
        C1 =:= C2 -> cmp_lists(R1, R2);
        true -> error({chunks_different, Id})
    end;
cmp_lists(_, _) ->
    error(different_chunks).

strip_rel(Root, AdditionalChunks) ->
    ok = assert_directory(Root),
    strip_fils(filelib:wildcard(filename:join(Root, "lib/*/ebin/*.beam")), AdditionalChunks).

strip_fils(Files, AdditionalChunks) ->
    {ok, [
        begin
            {ok, Reply} = strip_file(F, AdditionalChunks),
            Reply
        end
     || F <- Files
    ]}.

% A beam rebuilt from its significant chunks and compressed; a file is written back in place.
strip_file(File, AdditionalChunks) ->
    {ok, {Mod, Chunks}} = read_significant_chunks(File, AdditionalChunks ++ significant_chunks()),
    {ok, Stripped0} = build_module(Chunks),
    Stripped = compress(Stripped0),
    case File of
        _ when is_binary(File) -> {ok, {Mod, Stripped}};
        _ -> write_stripped(beam_filename(File), Mod, Stripped)
    end.

write_stripped(FileName, Mod, Stripped) ->
    case file:open(FileName, [raw, binary, write]) of
        {ok, Fd} ->
            case file:write(Fd, Stripped) of
                ok ->
                    ok = file:close(Fd),
                    {ok, {Mod, FileName}};
                Error ->
                    ok = file:close(Fd),
                    file_error(FileName, Error)
            end;
        Error ->
            file_error(FileName, Error)
    end.

build_chunks([{Id, Data} | Chunks]) ->
    BId = list_to_binary(Id),
    Size = byte_size(Data),
    [[<<BId/binary, Size:32>>, Data | pad(Size)] | build_chunks(Chunks)];
build_chunks([]) ->
    [].

pad(Size) ->
    case Size rem 4 of
        0 -> [];
        Rem -> lists:duplicate(4 - Rem, 0)
    end.

read_all_but_useless_chunks(File0) when is_atom(File0); is_list(File0); is_binary(File0) ->
    File = beam_filename(File0),
    {ok, Module, ChunkIds0} = scan_beam(File, info),
    ChunkIds = [Name || {Name, _, _} <- ChunkIds0, not is_useless_chunk(Name)],
    {ok, Module, Chunks} = scan_beam(File, ChunkIds),
    {ok, {Module, lists:reverse(Chunks)}}.

is_useless_chunk("CInf") -> true;
is_useless_chunk(_) -> false.

read_significant_chunks(File, ChunkList) ->
    {ok, {Module, Chunks0}} = read_chunk_data(File, ChunkList, [allow_missing_chunks]),
    {ok, {Module, filter_significant_chunks(Chunks0, mandatory_chunks(), File, Module)}}.

filter_significant_chunks([{_, Data} = Pair | Cs], Mandatory, File, Mod) when is_binary(Data) ->
    [Pair | filter_significant_chunks(Cs, Mandatory, File, Mod)];
filter_significant_chunks([{Id, missing_chunk} | Cs], Mandatory, File, Mod) ->
    case member(Id, Mandatory) of
        false -> filter_significant_chunks(Cs, Mandatory, File, Mod);
        true -> error({missing_chunk, File, Id})
    end;
filter_significant_chunks([], _, _, _) ->
    [].

filter_funtab([{"FunT" = Tag, <<L:4/binary, Data0/binary>>} | Cs]) ->
    Data = filter_funtab_1(Data0, <<0:32>>),
    [{Tag, <<L/binary, (iolist_to_binary(Data))/binary>>} | filter_funtab(Cs)];
filter_funtab([H | T]) ->
    [H | filter_funtab(T)];
filter_funtab([]) ->
    [].

filter_funtab_1(<<Important:20/binary, _OldUniq:4/binary, T/binary>>, Zero) ->
    [Important, Zero | filter_funtab_1(T, Zero)];
filter_funtab_1(Tail, _) when is_binary(Tail) -> [Tail].

read_all_chunks(File0) when is_atom(File0); is_list(File0); is_binary(File0) ->
    try
        File = beam_filename(File0),
        {ok, Module, ChunkIds0} = scan_beam(File, info),
        ChunkIds = [Name || {Name, _, _} <- ChunkIds0],
        {ok, Module, Chunks} = scan_beam(File, ChunkIds),
        {ok, Module, lists:reverse(Chunks)}
    catch
        Error -> Error
    end.

read_chunk_data(File0, ChunkNames) ->
    try
        read_chunk_data(File0, ChunkNames, [])
    catch
        Error -> Error
    end.

read_chunk_data(File0, ChunkNames0, Options) when is_atom(File0); is_list(File0); is_binary(File0) ->
    File = beam_filename(File0),
    {ChunkIds, Names, Optional} = check_chunks(ChunkNames0, File, [], [], []),
    AllowMissingChunks = member(allow_missing_chunks, Options),
    {ok, Module, Chunks} = scan_beam(File, ChunkIds, AllowMissingChunks, Optional),
    AT = ets:new(beam_symbols, []),
    T = {empty, AT},
    try
        chunks_to_data(Names, Chunks, File, Chunks, Module, T, [])
    after
        ets:delete(AT)
    end.

% The chunk ids to read, the names to answer with, and the ids that may be missing (atoms and debug information
% have two ids each).
check_chunks([atoms | Ids], File, IL, L, O) ->
    check_chunks(Ids, File, ["Atom", "AtU8" | IL], [{atom_chunk, atoms} | L], ["Atom", "AtU8" | O]);
check_chunks([abstract_code | Ids], File, IL, L, O) ->
    check_chunks(Ids, File, ["Abst", "Dbgi" | IL], [{abst_chunk, abstract_code} | L], ["Abst", "Dbgi" | O]);
check_chunks([ChunkName | Ids], File, IL, L, O) when is_atom(ChunkName) ->
    ChunkId = chunk_name_to_id(ChunkName, File),
    check_chunks(Ids, File, [ChunkId | IL], [{ChunkId, ChunkName} | L], O);
check_chunks([ChunkId | Ids], File, IL, L, O) ->
    check_chunks(Ids, File, [ChunkId | IL], [{ChunkId, ChunkId} | L], O);
check_chunks([], _File, IL, L, O) ->
    {lists:usort(IL), reverse(L), O}.

scan_beam(File, What) -> scan_beam(File, What, false, []).

scan_beam(File, What0, AllowMissingChunks, OptionalChunks) ->
    case scan_beam1(File, What0) of
        {missing, _FD, Mod, Data, What} when AllowMissingChunks ->
            {ok, Mod, [{Id, missing_chunk} || Id <- What] ++ Data};
        {missing, FD, Mod, Data, What} ->
            case What -- OptionalChunks of
                [] -> {ok, Mod, Data};
                [Missing | _] -> error({missing_chunk, filename(FD), Missing})
            end;
        R ->
            R
    end.

scan_beam1(File, What) ->
    FD = open_file(File),
    case catch scan_beam2(FD, What) of
        Error when error =:= element(1, Error) -> throw(Error);
        R -> R
    end.

scan_beam2(FD, What) ->
    case pread(FD, 0, 12) of
        {NFD, {ok, <<"FOR1", _Size:32, "BEAM">>}} -> scan_beam(NFD, 12, What, 17, []);
        _Error -> error({not_a_beam_file, filename(FD)})
    end.

% The walk over the chunks; the module (17 until the atom chunk is read) comes from the first atom.
scan_beam(_FD, _Pos, [], Mod, Data) when Mod =/= 17 ->
    {ok, Mod, Data};
scan_beam(FD, Pos, What, Mod, Data) ->
    case pread(FD, Pos, 8) of
        {_NFD, eof} when Mod =:= 17 ->
            error({missing_chunk, filename(FD), "Atom"});
        {_NFD, eof} when What =:= info ->
            {ok, Mod, reverse(Data)};
        {NFD, eof} ->
            {missing, NFD, Mod, Data, What};
        {NFD, {ok, <<IdL:4/binary, Sz:32>>}} ->
            Id = binary_to_list(IdL),
            Pos1 = Pos + 8,
            Pos2 = (4 * trunc((Sz + 3) / 4)) + Pos1,
            get_data(What, Id, NFD, Sz, Pos1, Pos2, Mod, Data);
        {_NFD, {ok, _ChunkHead}} ->
            error({invalid_beam_file, filename(FD), Pos})
    end.

get_atom_data(Cs, Id, FD, Size, Pos, Pos2, Data, Encoding) ->
    NewCs = del_chunk(Id, Cs),
    {NFD, Chunk} = get_chunk(Id, Pos, Size, FD),
    Module = extract_module(Chunk, Encoding),
    C =
        case Cs of
            info -> {Id, Pos, Size};
            _ -> {Id, Chunk}
        end,
    scan_beam(NFD, Pos2, NewCs, Module, [C | Data]).

get_data(Cs, "Atom" = Id, FD, Size, Pos, Pos2, _Mod, Data) ->
    get_atom_data(Cs, Id, FD, Size, Pos, Pos2, Data, latin1);
get_data(Cs, "AtU8" = Id, FD, Size, Pos, Pos2, _Mod, Data) ->
    get_atom_data(Cs, Id, FD, Size, Pos, Pos2, Data, utf8);
get_data(info, Id, FD, Size, Pos, Pos2, Mod, Data) ->
    scan_beam(FD, Pos2, info, Mod, [{Id, Pos, Size} | Data]);
get_data(Chunks, Id, FD, Size, Pos, Pos2, Mod, Data) ->
    {NFD, NewData} =
        case member(Id, Chunks) of
            true ->
                {FD1, Chunk} = get_chunk(Id, Pos, Size, FD),
                {FD1, [{Id, Chunk} | Data]};
            false ->
                {FD, Data}
        end,
    scan_beam(NFD, Pos2, del_chunk(Id, Chunks), Mod, NewData).

del_chunk(_Id, info) -> info;
del_chunk(Id, Chunks) -> delete(Id, Chunks).

get_chunk(Id, Pos, Size, FD) ->
    case pread(FD, Pos, Size) of
        {NFD, eof} when Size =:= 0 -> {NFD, <<>>};
        {_NFD, eof} when Size > 0 -> error({chunk_too_big, filename(FD), Id, Size, 0});
        {_NFD, {ok, Chunk}} when Size > byte_size(Chunk) ->
            error({chunk_too_big, filename(FD), Id, Size, byte_size(Chunk)});
        {NFD, {ok, Chunk}} ->
            {NFD, Chunk}
    end.

chunks_to_data([{atom_chunk, Name} | CNs], Chunks, File, Cs, Module, Atoms, L) ->
    {NewAtoms, Ret} = chunk_to_data(Name, <<"">>, File, Cs, Atoms, Module),
    chunks_to_data(CNs, Chunks, File, Cs, Module, NewAtoms, [Ret | L]);
chunks_to_data([{abst_chunk, Name} | CNs], Chunks, File, Cs, Module, Atoms, L) ->
    {NewAtoms, Ret} = abstract_code(Name, Chunks, File, Cs, Module, Atoms),
    chunks_to_data(CNs, Chunks, File, Cs, Module, NewAtoms, [Ret | L]);
chunks_to_data([{Id, Name} | CNs], Chunks, File, Cs, Module, Atoms, L) ->
    {_Id, Chunk} = lists:keyfind(Id, 1, Chunks),
    {NewAtoms, Ret} = chunk_to_data(Name, Chunk, File, Cs, Atoms, Module),
    chunks_to_data(CNs, Chunks, File, Cs, Module, NewAtoms, [Ret | L]);
chunks_to_data([], _Chunks, _File, _Cs, Module, _Atoms, L) ->
    {ok, {Module, reverse(L)}}.

% The abstract code from the debug information's backend, else from the old Abst chunk.
abstract_code(Name, Chunks, File, Cs, Module, Atoms) ->
    DbgiChunk = proplists:get_value("Dbgi", Chunks, <<"">>),
    case catch chunk_to_data(debug_info, DbgiChunk, File, Cs, Atoms, Module) of
        {DbgiAtoms, {debug_info, {debug_info_v1, Backend, Metadata}}} ->
            try backend_debug_info(Backend, Module, Metadata) of
                {ok, Code} -> {DbgiAtoms, {abstract_code, {raw_abstract_v1, Code}}};
                {error, _} -> {DbgiAtoms, {abstract_code, no_abstract_code}}
            catch
                error:undef -> error({missing_backend, File, Backend})
            end;
        {error, beam_lib, {key_missing_or_invalid, Path, debug_info}} ->
            error({key_missing_or_invalid, Path, abstract_code});
        _ ->
            AbstChunk = proplists:get_value("Abst", Chunks, <<"">>),
            chunk_to_data(Name, AbstChunk, File, Cs, Atoms, Module)
    end.

% The compiler's backend named literally, so programs that read abstract code link it.
backend_debug_info(erl_abstract_code, Module, Metadata) ->
    erl_abstract_code:debug_info(erlang_v1, Module, Metadata, []);
backend_debug_info(Backend, Module, Metadata) ->
    Backend:debug_info(erlang_v1, Module, Metadata, []).

chunk_to_data(Id, missing_chunk, _File, _Cs, AtomTable, _Mod) ->
    % Only with allow_missing_chunks.
    {AtomTable, {Id, missing_chunk}};
chunk_to_data(attributes = Id, Chunk, File, _Cs, AtomTable, _Mod) ->
    try
        {AtomTable, {Id, attributes(binary_to_term(Chunk))}}
    catch
        error:badarg -> error({invalid_chunk, File, chunk_name_to_id(Id, File)})
    end;
chunk_to_data(compile_info = Id, Chunk, File, _Cs, AtomTable, _Mod) ->
    try
        {AtomTable, {Id, binary_to_term(Chunk)}}
    catch
        error:badarg -> error({invalid_chunk, File, chunk_name_to_id(Id, File)})
    end;
chunk_to_data(debug_info = Id, Chunk, File, _Cs, AtomTable, Mod) ->
    {AtomTable, {Id, anno_from_term(debug_term(Id, Chunk, File, Mod))}};
chunk_to_data(documentation = Id, Chunk, File, _Cs, AtomTable, _Mod) ->
    try binary_to_term(Chunk) of
        #docs_v1{} = Term -> {AtomTable, {Id, Term}};
        _ -> error({invalid_chunk, File, chunk_name_to_id(Id, File)})
    catch
        error:badarg -> error({invalid_chunk, File, chunk_name_to_id(Id, File)})
    end;
chunk_to_data(abstract_code = Id, Chunk, File, _Cs, AtomTable, Mod) ->
    % Abst chunks are from before OTP 20.
    case debug_term(Id, Chunk, File, Mod) of
        no_debug_info -> {AtomTable, {Id, no_abstract_code}};
        Term -> {AtomTable, {Id, old_anno_or_invalid(Term, File, Id)}}
    end;
chunk_to_data(atoms = Id, _Chunk, _File, Cs, AtomTable0, _Mod) ->
    AtomTable = ensure_atoms(AtomTable0, Cs),
    {AtomTable, {Id, lists:sort(ets:tab2list(AtomTable))}};
chunk_to_data(literals = Id, Chunk, File, _Cs, AtomTable, _Mod) ->
    try extract_literals(Chunk) of
        Literals -> {AtomTable, {Id, Literals}}
    catch
        _:_ -> error({invalid_chunk, File, chunk_name_to_id(Id, File)})
    end;
chunk_to_data(ChunkName, Chunk, File, Cs, AtomTable, _Mod) when is_atom(ChunkName) ->
    case catch symbols(Chunk, AtomTable, Cs, ChunkName) of
        {ok, NewAtomTable, S} -> {NewAtomTable, {ChunkName, S}};
        {'EXIT', _} -> error({invalid_chunk, File, chunk_name_to_id(ChunkName, File)})
    end;
chunk_to_data(ChunkId, Chunk, _File, _Cs, AtomTable, _Module) when is_list(ChunkId) ->
    {AtomTable, {ChunkId, Chunk}}.

% The term of a Dbgi or Abst chunk: no_debug_info when empty, decrypted when encrypted.
debug_term(_Id, <<>>, _File, _Mod) ->
    no_debug_info;
debug_term(Id, <<0:8, N:8, Mode0:N/binary, Rest/binary>>, File, Mod) ->
    decrypt_chunk(binary_to_atom(Mode0, utf8), Mod, File, Id, Rest);
debug_term(Id, Chunk, File, _Mod) ->
    case catch binary_to_term(Chunk) of
        {'EXIT', _} -> error({invalid_chunk, File, chunk_name_to_id(Id, File)});
        Term -> Term
    end.

old_anno_or_invalid(Term, File, Id) ->
    try
        old_anno_from_term(Term)
    catch
        _:_ -> error({invalid_chunk, File, chunk_name_to_id(Id, File)})
    end.

chunk_name_to_id(indexed_imports, _) -> "ImpT";
chunk_name_to_id(imports, _) -> "ImpT";
chunk_name_to_id(exports, _) -> "ExpT";
chunk_name_to_id(labeled_exports, _) -> "ExpT";
chunk_name_to_id(locals, _) -> "LocT";
chunk_name_to_id(labeled_locals, _) -> "LocT";
chunk_name_to_id(attributes, _) -> "Attr";
chunk_name_to_id(abstract_code, _) -> "Abst";
chunk_name_to_id(debug_info, _) -> "Dbgi";
chunk_name_to_id(compile_info, _) -> "CInf";
chunk_name_to_id(documentation, _) -> "Docs";
chunk_name_to_id(literals, _) -> "LitT";
chunk_name_to_id(Other, File) -> error({unknown_chunk, File, Other}).

% Attributes of the same name joined, in name order.
attributes(Attrs) -> attributes(keysort(1, Attrs), []).

attributes([], R) ->
    reverse(R);
attributes(L, R) ->
    K = element(1, hd(L)),
    {L1, L2} = splitwith(fun(T) -> element(1, T) =:= K end, L),
    attributes(L2, [{K, append([A || {_, A} <- L1])} | R]).

symbols(<<_Num:32, B/binary>>, AT0, Cs, Name) ->
    AT = ensure_atoms(AT0, Cs),
    symbols1(B, AT, Name, [], 1).

symbols1(<<I1:32, I2:32, I3:32, B/binary>>, AT, Name, S, Cnt) ->
    symbols1(B, AT, Name, [symbol(Name, AT, I1, I2, I3, Cnt) | S], Cnt + 1);
symbols1(<<>>, AT, _Name, S, _Cnt) ->
    {ok, AT, sort(S)}.

symbol(indexed_imports, AT, I1, I2, I3, Cnt) -> {Cnt, atm(AT, I1), atm(AT, I2), I3};
symbol(imports, AT, I1, I2, I3, _Cnt) -> {atm(AT, I1), atm(AT, I2), I3};
symbol(labeled_exports, AT, I1, I2, I3, _Cnt) -> {atm(AT, I1), I2, I3};
symbol(labeled_locals, AT, I1, I2, I3, _Cnt) -> {atm(AT, I1), I2, I3};
symbol(_, AT, I1, I2, _I3, _Cnt) -> {atm(AT, I1), I2}.

atm(AT, N) -> ets:lookup_element(AT, N, 2).

% The atom table, filled from AtU8 (or Atom) the first time a chunk needs it.
ensure_atoms({empty, AT}, Cs) ->
    case lists:keyfind("AtU8", 1, Cs) of
        {_Id, AtomChunk} when is_binary(AtomChunk) ->
            extract_atoms(AtomChunk, AT, utf8);
        _ ->
            {_Id, AtomChunk} = lists:keyfind("Atom", 1, Cs),
            extract_atoms(AtomChunk, AT, latin1)
    end,
    AT;
ensure_atoms(AT, _Cs) ->
    AT.

% A negative count says the atoms have compact-term lengths (OTP 28 and later).
extract_module(<<Num:32/signed-integer, B/binary>>, utf8) when Num < 0 ->
    {Module, _} = extract_long_atom(B),
    Module;
extract_module(<<_Num:32/signed-integer, B/binary>>, Encoding) ->
    {Module, _} = extract_atom(B, Encoding),
    Module.

extract_atoms(<<Num:32/signed-integer, B/binary>>, AT, utf8) when Num < 0 -> extract_long_atoms(B, 1, AT);
extract_atoms(<<_Num:32, B/binary>>, AT, Encoding) -> extract_atoms(B, 1, AT, Encoding).

extract_atoms(<<>>, _I, _AT, _Encoding) ->
    true;
extract_atoms(B, I, AT, Encoding) ->
    {Atom, B1} = extract_atom(B, Encoding),
    true = ets:insert(AT, {I, Atom}),
    extract_atoms(B1, I + 1, AT, Encoding).

extract_atom(<<Len, B/binary>>, Encoding) ->
    <<SB:Len/binary, Tail/binary>> = B,
    {binary_to_atom(SB, Encoding), Tail}.

extract_long_atoms(<<>>, _I, _AT) ->
    true;
extract_long_atoms(B, I, AT) ->
    {Atom, B1} = extract_long_atom(B),
    true = ets:insert(AT, {I, Atom}),
    extract_long_atoms(B1, I + 1, AT).

extract_long_atom(B0) ->
    {Len, B} = decode_arg_val(B0),
    <<SB:Len/binary, Tail/binary>> = B,
    {binary_to_atom(SB, utf8), Tail}.

decode_arg_val(<<N:4, 0:1, _Tag:3, Code/binary>>) -> {N, Code};
decode_arg_val(<<High:3, 0:1, 1:1, _Tag:3, Low, Code0/binary>>) -> {(High bsl 8) bor Low, Code0}.

% The literal table, compressed with zlib before OTP 28, as {Index, Term} pairs.
extract_literals(Chunk0) ->
    Literals0 =
        case Chunk0 of
            <<0:32, Chunk/binary>> ->
                Chunk;
            <<OriginalSize:32, Chunk1/binary>> ->
                Chunk = zlib:uncompress(Chunk1),
                OriginalSize = byte_size(Chunk),
                Chunk
        end,
    <<NumLiterals:32, Literals1/binary>> = Literals0,
    Literals = literal_terms(Literals1),
    NumLiterals = length(Literals),
    lists:enumerate(0, Literals).

literal_terms(<<N:32, Term:N/binary, Rest/binary>>) -> [binary_to_term(Term) | literal_terms(Rest)];
literal_terms(<<>>) -> [].

% A beam read whole into memory: the position, the bytes not yet read, and where it came from.
-record(bb, {pos = 0, bin, source}).

open_file(Binary0) when is_binary(Binary0) ->
    Binary = maybe_uncompress(Binary0),
    #bb{bin = Binary, source = Binary};
open_file(FileName) ->
    case file:open(FileName, [read, raw, binary]) of
        {ok, Fd} -> read_all(Fd, FileName, []);
        Error -> file_error(FileName, Error)
    end.

read_all(Fd, FileName, Bins) ->
    case file:read(Fd, 1 bsl 18) of
        {ok, Bin} ->
            read_all(Fd, FileName, [Bin | Bins]);
        eof ->
            ok = file:close(Fd),
            #bb{bin = maybe_uncompress(reverse(Bins)), source = FileName};
        Error ->
            ok = file:close(Fd),
            file_error(FileName, Error)
    end.

% Reads only move forward: the bytes before AtPos are dropped.
pread(FD, AtPos, Size) ->
    #bb{pos = Pos, bin = Binary} = FD,
    Skip = AtPos - Pos,
    case Binary of
        <<_:Skip/binary, B:Size/binary, Bin/binary>> ->
            {FD#bb{pos = AtPos + Size, bin = Bin}, {ok, B}};
        <<_:Skip/binary, Bin/binary>> when byte_size(Bin) > 0 ->
            {FD#bb{pos = AtPos + byte_size(Bin), bin = <<>>}, {ok, Bin}};
        _ ->
            {FD, eof}
    end.

filename(#bb{source = Source}) -> beam_filename(Source).

beam_filename(Bin) when is_binary(Bin) -> Bin;
beam_filename(File) -> filename:rootname(File, ".beam") ++ ".beam".

maybe_uncompress(<<"FOR1", _/binary>> = Binary) ->
    Binary;
maybe_uncompress([<<"FOR1", _/binary>> | _] = IOData) ->
    iolist_to_binary(IOData);
maybe_uncompress(IOData) ->
    try
        zlib:gunzip(IOData)
    catch
        _:_ -> iolist_to_binary(IOData)
    end.

compress(IOData) -> zlib:gzip(IOData).

assert_directory(FileName) ->
    case filelib:is_dir(FileName) of
        true -> ok;
        false -> error({not_a_directory, FileName})
    end.

file_error(FileName, {error, Reason}) -> error({file_error, FileName, Reason}).

error(Reason) -> throw({error, ?MODULE, Reason}).

significant_chunks() -> ["Line", "Type" | md5_chunks()].

md5_chunks() -> ["Atom", "AtU8", "Code", "StrT", "ImpT", "ExpT", "FunT", "LitT", "Meta", "Recs", "DbgB"].

mandatory_chunks() -> ["Code", "ExpT", "ImpT", "StrT"].

% --- Encrypted debug information ---

% The crypto key fun, or none.
-record(state, {crypto_key_f}).

-define(CRYPTO_KEY_SERVER, beam_lib__crypto_key_server).

% Decryption needs crypto, called dynamically: without it every key is missing or invalid.
decrypt_chunk(Type, Module, File, Id, Bin) ->
    try
        KeyString = get_crypto_key({debug_info, Type, Module, File}),
        {Type, Key, IVec, _BlockSize} = make_crypto_key(Type, KeyString),
        ok = start_crypto(),
        binary_to_term(erlang:apply(crypto, crypto_one_time, [des_ede3_cbc, Key, IVec, Bin, false]))
    catch
        _:_ -> error({key_missing_or_invalid, File, Id})
    end.

old_anno_from_term({raw_abstract_v1, Forms}) -> {raw_abstract_v1, anno_from_forms(Forms)};
old_anno_from_term(T) -> T.

anno_from_term({debug_info_v1 = Tag1, erl_abstract_code = Tag2, {Forms, Opts}}) ->
    try
        {Tag1, Tag2, {anno_from_forms(Forms), Opts}}
    catch
        _:_ -> {Tag1, Tag2, {Forms, Opts}}
    end;
anno_from_term(T) ->
    T.

% Record field types from before OTP 19 become well-formed record forms.
anno_from_forms(Forms0) ->
    Forms = epp:restore_typed_record_fields(Forms0),
    [erl_parse:anno_from_term(Form) || Form <- Forms].

start_crypto() ->
    case application:start(crypto) of
        {error, {already_started, _}} -> ok;
        ok -> ok
    end.

get_crypto_key(What) -> call_crypto_server({get_crypto_key, What}).

call_crypto_server(Req) ->
    try
        gen_server:call(?CRYPTO_KEY_SERVER, Req, infinity)
    catch
        exit:{noproc, _} -> call_crypto_server_1(Req);
        % The server ended just as it was called.
        exit:{normal, _} -> call_crypto_server_1(Req)
    end.

call_crypto_server_1(Req) ->
    case gen_server:start({local, ?CRYPTO_KEY_SERVER}, ?MODULE, [], []) of
        {ok, _} -> ok;
        {error, {already_started, _}} -> ok
    end,
    erlang:yield(),
    call_crypto_server(Req).

init([]) -> {ok, #state{}}.

handle_call({get_crypto_key, _} = R, From, #state{crypto_key_f = undefined} = S) ->
    case crypto_key_fun_from_file() of
        error -> {reply, error, S};
        % Its init has been called already.
        F when is_function(F) -> handle_call(R, From, S#state{crypto_key_f = F})
    end;
handle_call({get_crypto_key, What}, From, #state{crypto_key_f = F} = S) ->
    try
        Result = F(What),
        % The key is not left lying around: reply, collect, then go on.
        gen_server:reply(From, Result),
        erlang:garbage_collect(),
        {noreply, S}
    catch
        _:_ -> {reply, error, S}
    end;
handle_call({crypto_key_fun, F}, {_, _} = From, S) ->
    case S#state.crypto_key_f of
        undefined when is_function(F, 1) ->
            {Result, Fun, Reply} = install_crypto_key_fun(F),
            gen_server:reply(From, Reply),
            erlang:garbage_collect(),
            case Result of
                true -> {noreply, S#state{crypto_key_f = Fun}};
                false -> {noreply, S}
            end;
        undefined ->
            {reply, {error, badfun}, S};
        OtherF when is_function(OtherF) ->
            {reply, {error, exists}, S}
    end;
handle_call(clear_crypto_key_fun, _From, S) ->
    case S#state.crypto_key_f of
        undefined -> {stop, normal, undefined, S};
        F -> {stop, normal, {ok, catch F(clear)}, S}
    end.

% The fun's init: ok keeps it, {ok, F1} installs F1, anything else is the error.
install_crypto_key_fun(F) ->
    case catch F(init) of
        ok -> {true, F, ok};
        {ok, F1} when is_function(F1, 1) -> {true, F1, ok};
        {ok, F1} when is_function(F1) -> {false, undefined, {error, badfun}};
        {error, Reason} -> {false, undefined, {error, Reason}};
        {'EXIT', Reason} -> {false, undefined, {error, Reason}}
    end.

handle_cast(_, State) -> {noreply, State}.

handle_info(_, State) -> {noreply, State}.

code_change(_OldVsn, State, _Extra) -> {ok, State}.

terminate(_Reason, _State) -> ok.

% Keys from .erlang.crypt in the current directory, the home directory or the user configuration directory.
crypto_key_fun_from_file() ->
    UserConfig = filename:basedir(user_config, "erlang"),
    case init:get_argument(home) of
        {ok, [[Home]]} -> crypto_key_fun_from_file_1([".", Home, UserConfig]);
        _ -> crypto_key_fun_from_file_1([".", UserConfig])
    end.

crypto_key_fun_from_file_1(Path) ->
    case f_p_s(Path, ".erlang.crypt") of
        {ok, KeyInfo, _} -> try_load_crypto_fun(KeyInfo);
        _ -> error
    end.

f_p_s(P, F) ->
    case file:path_script(P, F) of
        {error, enoent} ->
            {error, enoent};
        {error, {Line, _Mod, _Term} = E} ->
            error("file:path_script(~tp,~tp): error on line ~p: ~ts~n", [P, F, Line, file:format_error(E)]),
            ok;
        {error, E} when is_atom(E) ->
            error("file:path_script(~tp,~tp): ~ts~n", [P, F, file:format_error(E)]),
            ok;
        Other ->
            Other
    end.

try_load_crypto_fun(KeyInfo) when is_list(KeyInfo) ->
    T = ets:new(keys, [private, set]),
    foreach(
        fun
            ({debug_info, Mode, M, Key}) when is_atom(M) -> ets:insert(T, {{debug_info, Mode, M, []}, Key});
            ({debug_info, Mode, [], Key}) -> ets:insert(T, {{debug_info, Mode, [], []}, Key});
            (Other) -> error("unknown key: ~p~n", [Other])
        end,
        KeyInfo
    ),
    fun
        ({debug_info, Mode, M, F}) ->
            alt_lookup_key([{debug_info, Mode, M, F}, {debug_info, Mode, M, []}, {debug_info, Mode, [], []}], T);
        (clear) ->
            ets:delete(T);
        (_) ->
            error
    end;
try_load_crypto_fun(KeyInfo) ->
    error("unrecognized crypto key info: ~p\n", [KeyInfo]).

alt_lookup_key([H | T], Tab) ->
    case ets:lookup(Tab, H) of
        [] -> alt_lookup_key(T, Tab);
        [{_, Val}] -> Val
    end;
alt_lookup_key([], _) ->
    error.

error(Fmt, Args) ->
    error_logger:error_msg(Fmt, Args),
    error.
