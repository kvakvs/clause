-module(file_sorter_ops).
-export([main/1]).

%% file_sorter (plan 21 step LIB54A): sort, keysort, merge, keymerge, check
%% and keycheck over binary_term, binary, term and fun formats, runs and
%% merges forced by small sizes and many files, unique and descending orders
%% and order funs (with terms that compare equal but differ, whose order and
%% survival depend on OTP's algorithm), headers, compression, temporary
%% directories, input and output funs, and errors. Outputs are shown as term
%% lists or digests of their exact bytes. Files are made in a directory under
%% the working directory and removed again.
main([]) ->
    Dir = "file_sorter_ops.dir",
    _ = file:del_dir_r(Dir),
    ok = file:make_dir(Dir),
    {ok, Cwd} = file:get_cwd(),
    put(cwd, Cwd),
    put(dir, Dir),
    sorted(),
    ties(),
    keyed(),
    merged(),
    checked(),
    formats(),
    funs(),
    big(),
    errors(),
    show(left_over, lists:sort(element(2, file:list_dir(Dir)))),
    ok = file:del_dir_r(Dir),
    io:format("file_sorter_ops: ok~n").

show(Label, Value) -> io:format("~s ~p~n", [Label, strip(Value)]).

%% Absolute file names in error terms shown relative to the working directory.
strip(Name) when is_list(Name) ->
    Cwd = get(cwd),
    case lists:prefix(Cwd, Name) of
        true -> "CWD" ++ lists:nthtail(length(Cwd), Name);
        false -> [strip(E) || E <- Name]
    end;
strip(Tuple) when is_tuple(Tuple) ->
    list_to_tuple(strip(tuple_to_list(Tuple)));
strip(Other) ->
    Other.

path(Name) -> filename:join(get(dir), Name).

%% A file of terms in OTP's binary_term format with a HdLen-byte size header.
write_terms(Name, Terms) -> write_terms(Name, Terms, 4).

write_terms(Name, Terms, HdLen) ->
    Bins = [term_to_binary(T) || T <- Terms],
    ok = file:write_file(path(Name), [[<<(byte_size(B)):HdLen/unit:8>>, B] || B <- Bins]).

read_terms(Name) -> read_terms(Name, 4).

read_terms(Name, HdLen) ->
    {ok, Bin} = file:read_file(path(Name)),
    [binary_to_term(B) || B <- split(Bin, HdLen)].

split(<<>>, _) ->
    [];
split(Bin, HdLen) ->
    <<Size:HdLen/unit:8, B:Size/binary, Rest/binary>> = Bin,
    [B | split(Rest, HdLen)].

%% The digest of a file's exact bytes.
digest(Name) ->
    {ok, Bin} = file:read_file(path(Name)),
    binary:encode_hex(erlang:md5(Bin)).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

sorted() ->
    Terms = [c, {b, 2}, 3, "str", a, 1, {b, 1}, <<"bin">>, 2.5, a, [x], 3],
    write_terms("in", Terms),
    Out = path("out"),
    show(sort, [file_sorter:sort([path("in")], Out), read_terms("out")]),
    show(unique, [file_sorter:sort([path("in")], Out, [{unique, true}]), read_terms("out")]),
    show(descending, [file_sorter:sort([path("in")], Out, {order, descending}), read_terms("out")]),
    Fun = fun(A, B) -> erlang:phash2(A) =< erlang:phash2(B) end,
    show(order_fun, [
        file_sorter:sort([path("in")], Out, [{order, Fun}]),
        read_terms("out") =:= lists:sort(Fun, Terms)
    ]),
    write_terms("in1", [5, 3, 9]),
    show(sort1, [file_sorter:sort(path("in1")), read_terms("in1")]),
    show(sort2, [file_sorter:sort([path("in"), path("in1")], Out), read_terms("out")]),
    show(empty, [file_sorter:sort([], Out), read_terms("out")]).

%% Terms that compare equal (1 and 1.0) and repeat, in one run and across runs and merge levels.
ties() ->
    Groups = [[1.0, 2, 1, 1.0, 3], [2.0, 1, 3.0, 2], [1, 1.0], [3, 2.0, 1.0], [2, 2, 2.0]],
    Files = [
        begin
            N = "t" ++ integer_to_list(I),
            write_terms(N, G),
            path(N)
        end
     || {I, G} <- enum(Groups)
    ],
    Out = path("out"),
    Runs = [{size, 1}, {no_files, 2}],
    Cases = [
        {one_run, []},
        {one_run_unique, [{unique, true}]},
        {runs, Runs},
        {runs_unique, [{unique, true} | Runs]},
        {runs_descending, [{order, descending} | Runs]},
        {runs_descending_unique, [{order, descending}, {unique, true} | Runs]},
        {runs_fun, [{order, fun(A, B) -> A =< B end} | Runs]},
        {runs_fun_unique, [{order, fun(A, B) -> A =< B end}, {unique, true} | Runs]}
    ],
    [
        show(Label, [file_sorter:sort(Files, Out, Options), read_terms("out")])
     || {Label, Options} <- Cases
    ].

enum(L) -> lists:zip(lists:seq(1, length(L)), L).

keyed() ->
    Tuples = [{b, 1, x}, {a, 2, y}, {b, 1.0, z}, {a, 1, w}, {c, 0, v}, {a, 2.0, u}, {b, 3, t}],
    write_terms("k1", lists:sublist(Tuples, 4)),
    write_terms("k2", lists:nthtail(4, Tuples)),
    Files = [path("k1"), path("k2")],
    Out = path("out"),
    Runs = [{size, 1}, {no_files, 2}],
    Cases = [
        {keysort, 1, []},
        {keysort_unique, 1, [{unique, true}]},
        {keysort_two, [2, 1], []},
        {keysort_runs, 1, Runs},
        {keysort_runs_unique, 1, [{unique, true} | Runs]},
        {keysort_runs_descending, 1, [{order, descending} | Runs]},
        {keysort_runs_descending_unique, 1, [{order, descending}, {unique, true} | Runs]},
        {keysort_runs_unique_two, [2, 1], [{unique, true} | Runs]},
        {keysort_list_one, [3], []}
    ],
    [show(L, [file_sorter:keysort(K, Files, Out, O), read_terms("out")]) || {L, K, O} <- Cases],
    show(keysort2, [file_sorter:keysort(2, path("k1")), read_terms("k1")]).

merged() ->
    write_terms("m1", [1, 3, 5, 5.0, 7]),
    write_terms("m2", [2, 3, 3.0, 6]),
    write_terms("m3", []),
    write_terms("m4", [0, 5, 8]),
    Files = [path(F) || F <- ["m1", "m2", "m3", "m4"]],
    Out = path("out"),
    show(merge, [file_sorter:merge(Files, Out), read_terms("out")]),
    show(merge_unique, [file_sorter:merge(Files, Out, [{unique, true}]), read_terms("out")]),
    show(merge_two, [
        file_sorter:merge(Files, Out, [{no_files, 2}, {unique, true}]), read_terms("out")
    ]),
    write_terms("d1", [9, 5, 5.0, 1]),
    write_terms("d2", [8, 5, 2]),
    show(merge_descending, [
        file_sorter:merge([path("d1"), path("d2")], Out, [{order, descending}, {unique, true}]),
        read_terms("out")
    ]),
    write_terms("km1", [{a, 1}, {b, 2}, {b, 3}]),
    write_terms("km2", [{a, 4}, {b, 5}, {c, 6}]),
    KFiles = [path("km1"), path("km2")],
    show(keymerge, [file_sorter:keymerge(1, KFiles, Out), read_terms("out")]),
    show(keymerge_unique, [
        file_sorter:keymerge(1, KFiles, Out, [{unique, true}]), read_terms("out")
    ]),
    show(merge_none, [file_sorter:merge([], Out), read_terms("out")]).

checked() ->
    write_terms("c1", [1, 2, 2, 3]),
    write_terms("c2", [1, 3, 2, 4]),
    write_terms("c3", [3, 2, 2, 1]),
    write_terms("c4", [{a, 2}, {b, 1}, {a, 3}]),
    C = fun(F) -> path(F) end,
    show(check, [
        file_sorter:check(C("c1")),
        file_sorter:check([C("c1"), C("c2"), C("c3")], []),
        file_sorter:check([C("c1")], [{unique, true}]),
        file_sorter:check([C("c3")], [{order, descending}]),
        file_sorter:check([C("c3")], [{order, descending}, {unique, true}]),
        file_sorter:check([C("c2")], [{order, fun(A, B) -> A >= B end}])
    ]),
    show(keycheck, [
        file_sorter:keycheck(1, C("c4")),
        file_sorter:keycheck(2, [C("c4")], []),
        file_sorter:keycheck([2, 1], [C("c4")], [{unique, true}])
    ]).

formats() ->
    Out = path("out"),
    ok = file:write_file(path("b"), [
        [<<(byte_size(B)):16>>, B]
     || B <- [<<"pear">>, <<"apple">>, <<"fig">>, <<>>]
    ]),
    show(binary, [
        file_sorter:sort([path("b")], Out, [{format, binary}, {header, 2}]), digest("out")
    ]),
    write_terms("h", [3, 1, 2], 2),
    show(header, [file_sorter:sort([path("h")], Out, [{header, 2}]), read_terms("out", 2)]),
    Decode = fun(B) -> -binary_to_term(B) end,
    write_terms("f", [3, 1, 2]),
    show(format_fun, [file_sorter:sort([path("f")], Out, [{format, Decode}]), read_terms("out")]),
    ok = file:write_file(path("terms"), "{b, 2}.\n{a, 1}.\n\"s\".\n{a, 1}.\n"),
    show(term, [
        file_sorter:sort([path("terms")], Out, [{format, term}, {unique, true}]), file:consult(Out)
    ]),
    show(term_keysort, [
        file_sorter:keysort(2, [path("terms")], Out, [{format, term}]), file:consult(Out)
    ]),
    show(term_check, file_sorter:check([path("terms")], [{format, term}])),
    show(term_merge, [file_sorter:merge([path("terms")], Out, [{format, term}]), file:consult(Out)]),
    show(
        term_header,
        outcome(fun() -> file_sorter:sort([path("terms")], Out, [{format, term}, {header, 2}]) end)
    ),
    write_terms("z", [3, 1, 2]),
    Z = path("z.gz"),
    show(compressed, [
        file_sorter:sort([path("z")], Z, [{compressed, true}]),
        begin
            {ok, B} = file:read_file(Z),
            [binary:part(B, 0, 2), [binary_to_term(X) || X <- split(zlib:gunzip(B), 4)]]
        end,
        file_sorter:sort([Z], Out),
        read_terms("out")
    ]),
    ok = file:make_dir(path("tmp")),
    show(tmpdir, [
        file_sorter:sort([path("t1"), path("t2")], Out, [{tmpdir, path("tmp")}, {size, 1}]),
        read_terms("out"),
        file:list_dir(path("tmp"))
    ]),
    ok = file:del_dir(path("tmp")).

funs() ->
    Input = fun
        Next([]) ->
            fun
                (close) -> closed;
                (read) -> {end_of_input, done}
            end;
        Next([Chunk | Chunks]) ->
            fun
                (close) -> closed;
                (read) -> {[term_to_binary(T) || T <- Chunk], Next(Chunks)}
            end
    end,
    Collect = fun Collect(Acc) ->
        fun
            (close) -> lists:reverse(Acc);
            (Objects) -> Collect([Objects | Acc])
        end
    end,
    Chunks = [[3, 1], [], [2, 5], [4]],
    show(fun_io, file_sorter:sort(Input(Chunks), Collect([]))),
    show(fun_io_runs, file_sorter:sort(Input(Chunks), Collect([]), [{size, 1}, {no_files, 2}])),
    show(fun_to_file, [file_sorter:sort(Input(Chunks), path("out")), read_terms("out")]),
    write_terms("fi", [b, a]),
    show(file_to_fun, file_sorter:sort([path("fi")], Collect([]))),
    TermInput = fun
        (read) ->
            {[{x, 2}, {y, 1}], fun
                (read) -> end_of_input;
                (close) -> ok
            end};
        (close) ->
            ok
    end,
    TermOutput = fun Out(Acc) ->
        fun
            (close) -> lists:append(lists:reverse(Acc));
            (L) -> Out([L | Acc])
        end
    end,
    show(term_funs, file_sorter:keysort(2, TermInput, TermOutput([]), [{format, term}])),
    show(merge_to_fun, file_sorter:merge([path("fi")], Collect([]))),
    Bad = fun
        (read) -> not_a_reply;
        (close) -> ok
    end,
    show(bad_input, file_sorter:sort(Bad, path("out"))),
    Refuse = fun
        (close) -> ok;
        (_) -> refused
    end,
    show(bad_output, file_sorter:sort(Input(Chunks), Refuse)),
    BadObjects = fun
        (read) -> {[<<"not a term">>], fun(read) -> end_of_input end};
        (close) -> ok
    end,
    show(bad_objects, file_sorter:sort(BadObjects, path("out"))).

%% Enough terms for several chunks, runs and merge levels, objects crossing chunk boundaries.
big() ->
    Terms = [{(I * 7919) rem 1009, lists:duplicate(I rem 23, $x), I} || I <- lists:seq(1, 3000)],
    write_terms("big", Terms),
    Out = path("out"),
    Opts = [{size, 4096}, {no_files, 3}],
    show(big_sort, [
        file_sorter:sort([path("big")], Out, Opts),
        read_terms("out") =:= lists:sort(Terms),
        digest("out")
    ]),
    show(big_keysort, [
        file_sorter:keysort(1, [path("big")], Out, Opts),
        read_terms("out") =:= lists:keysort(1, Terms),
        digest("out")
    ]),
    show(big_unique, [
        file_sorter:keysort(1, [path("big")], Out, [{unique, true} | Opts]),
        length(read_terms("out"))
    ]),
    show(big_check, file_sorter:check([path("big")])).

errors() ->
    Out = path("out"),
    write_terms("e", [1, 2]),
    show(missing, file_sorter:sort([path("missing")], Out)),
    show(missing_check, file_sorter:check([path("missing")], [])),
    ok = file:write_file(path("short"), <<0, 0, 0, 9, 131, 97>>),
    show(premature_eof, file_sorter:sort([path("short")], Out)),
    ok = file:write_file(path("junk"), <<0, 0, 0, 2, 1, 2>>),
    show(bad_object, file_sorter:sort([path("junk")], Out)),
    show(bad_keypos_object, file_sorter:keysort(3, [path("e")], Out)),
    ok = file:write_file(path("badterm"), "{a, 1}.\n{b,, 2}.\n"),
    show(bad_term, file_sorter:sort([path("badterm")], Out, [{format, term}])),
    Bad = fun(F) -> outcome(F) end,
    show(badargs, [
        Bad(fun() -> file_sorter:sort([path("e")], Out, [{size, -1}]) end),
        Bad(fun() -> file_sorter:sort([path("e")], Out, {no_files, 1}) end),
        Bad(fun() -> file_sorter:sort([path("e")], Out, [bogus]) end),
        Bad(fun() -> file_sorter:sort([path("e")], Out, [{header, 0}]) end),
        Bad(fun() -> file_sorter:sort(not_a_list, Out) end),
        Bad(fun() -> file_sorter:sort([{bad}], Out) end),
        Bad(fun() -> file_sorter:keysort(0, [path("e")], Out) end),
        Bad(fun() -> file_sorter:keysort([], [path("e")], Out) end),
        Bad(fun() -> file_sorter:keysort(1, [path("e")], Out, [{format, binary}]) end),
        Bad(fun() -> file_sorter:keysort(1, [path("e")], Out, [{order, fun(_, _) -> true end}]) end),
        Bad(fun() -> file_sorter:keycheck(1, [path("e")], [{format, binary}]) end),
        Bad(fun() -> file_sorter:merge(not_a_list, Out) end),
        Bad(fun() -> file_sorter:check(path("missing")) end)
    ]),
    show(output_dir_missing, file_sorter:sort([path("e")], path("nodir/out"))).
