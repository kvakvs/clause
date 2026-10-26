%% Clause's file_sorter (plan 21 step LIB54A, docs/libraries/library.md): sorting, merging and checking terms on files, written
%% from OTP's documentation. The run and merge algorithm follows OTP's step for step (runs of `size` bytes sorted in
%% memory, temporary files merged `no_files` at a time, chunks of 16 KiB, merge streams ordered by their buffered
%% objects and file numbers), because the order of terms that compare equal and the terms `unique` keeps depend on
%% it. Objects are {Term, [SizeHeader | Binary]}: sorted on the term, ties broken by the bytes written, which are the
%% bytes read. Input and output may be funs; the `term` format reads with io:read/2 and writes with "~p.~n".
-module(file_sorter).
-export([
    check/1,
    check/2,
    keycheck/2,
    keycheck/3,
    keymerge/3,
    keymerge/4,
    keysort/2,
    keysort/3,
    keysort/4,
    merge/2,
    merge/3,
    sort/1,
    sort/2,
    sort/3
]).
-include("file_sorter.types.hrl").

% Bytes read from a file at a time, bytes sorted in memory per run by default, files merged at a time by default,
% bytes merged before they are written, and the largest header-announced size.
-define(CHUNKSIZE, 16384).
-define(RUNSIZE, 524288).
-define(NOMERGE, 16).
-define(MERGESIZE, ?CHUNKSIZE).
-define(MAXSIZE, (1 bsl 31)).

% The state of one sort, merge or check: keys, the runs written so far (lists of temporary files per merge level),
% the next temporary file number, the current input and output (files or funs), temporary files to remove, the
% options, the open output while merging, the reference thrown errors carry and an input fun's end value.
-record(w, {
    keypos,
    runs = [[]],
    seq = 1,
    in,
    out,
    fun_out,
    prefix,
    temp = [],
    format,
    runsize,
    no_files,
    order,
    chunksize,
    wfd,
    ref,
    z,
    unique,
    hdlen,
    inout_value
}).

% The options of a call.
-record(opts, {
    format = fun erlang:binary_to_term/1,
    size = ?RUNSIZE,
    no_files = ?NOMERGE,
    tmpdir = default,
    order = ascending,
    compressed = false,
    unique = false,
    header = 4
}).

% --- Exported functions ---

sort(FileName) -> sort([FileName], FileName).

sort(Input, Output) -> sort(Input, Output, []).

% Sort the terms of the input files or fun to the output file or fun.
sort(Input0, Output0, Options) ->
    case {is_input(Input0), maybe_output(Output0), options(Options)} of
        {{true, Input}, {true, Output}, #opts{} = Opts} -> do_sort(0, Input, Output, Opts, sort);
        T -> badarg(culprit(tuple_to_list(T)), [Input0, Output0, Options])
    end.

keysort(KeyPos, FileName) -> keysort(KeyPos, [FileName], FileName).

keysort(KeyPos, Input, Output) -> keysort(KeyPos, Input, Output, []).

% Sort tuples on the elements at KeyPos (stable); keysort takes no order fun and no binary format.
keysort(KeyPos, Input0, Output0, Options) ->
    Checked = {is_keypos(KeyPos), is_input(Input0), maybe_output(Output0), options(Options)},
    case keyed(Checked) of
        {true, In, Out, Opts} -> do_sort(KeyPos, In, Out, Opts, sort);
        Bad -> badarg(culprit(Bad), [KeyPos, Input0, Output0, Options])
    end.

merge(Files, Output) -> merge(Files, Output, []).

% Merge sorted files.
merge(Files0, Output0, Options) ->
    case {is_files(Files0), maybe_output(Output0), options(Options)} of
        {{true, Files}, {true, Output}, #opts{} = Opts} -> do_sort(0, Files, Output, Opts, merge);
        T -> badarg(culprit(tuple_to_list(T)), [Files0, Output0, Options])
    end.

keymerge(KeyPos, Files, Output) -> keymerge(KeyPos, Files, Output, []).

% Merge files sorted on the elements at KeyPos.
keymerge(KeyPos, Files0, Output0, Options) ->
    Checked = {is_keypos(KeyPos), is_files(Files0), maybe_output(Output0), options(Options)},
    case keyed(Checked) of
        {true, Files, Out, Opts} -> do_sort(KeyPos, Files, Out, Opts, merge);
        Bad -> badarg(culprit(Bad), [KeyPos, Files0, Output0, Options])
    end.

check(FileName) -> check([FileName], []).

% {ok, [{FileName, Position, Term}]} of the first term out of order in each file that is not sorted.
check(Files0, Options) ->
    case {is_files(Files0), options(Options)} of
        {{true, Files}, #opts{} = Opts} -> do_sort(0, Files, undefined, Opts, check);
        T -> badarg(culprit(tuple_to_list(T)), [Files0, Options])
    end.

keycheck(KeyPos, FileName) -> keycheck(KeyPos, [FileName], []).

keycheck(KeyPos, Files0, Options) ->
    Checked = {is_keypos(KeyPos), is_files(Files0), options(Options)},
    case keyed(Checked) of
        {true, Files, Opts} -> do_sort(KeyPos, Files, undefined, Opts, check);
        Bad -> badarg(culprit(Bad), [KeyPos, Files0, Options])
    end.

% The checked arguments of a keyed call: the inputs and options when all are good, else the list culprit/1 picks
% from; the binary format and order funs are refused first.
keyed(Checked) ->
    case element(tuple_size(Checked), Checked) of
        #opts{format = binary} -> [{badarg, format}];
        #opts{order = Order} when is_function(Order) -> [{badarg, order}];
        #opts{} = Opts -> keyed_parts(tuple_to_list(Checked), Opts);
        _ -> tuple_to_list(Checked)
    end.

keyed_parts([true | Parts], Opts) ->
    case [Value || {true, Value} <- lists:droplast(Parts)] of
        Values when length(Values) =:= length(Parts) - 1 -> list_to_tuple([true | Values] ++ [Opts]);
        _ -> [true | Parts]
    end;
keyed_parts(Parts, _) ->
    Parts.

culprit([{error, _} = E | _]) -> E;
culprit([{badarg, _} = B | _]) -> B;
culprit([_ | B]) -> culprit(B).

badarg({error, _} = E, _Args) -> E;
badarg({badarg, _} = B, Args) -> erlang:error(B, Args).

options(Options) when is_list(Options) -> options(Options, #opts{});
options(Option) -> options([Option]).

options([{format, Format} | L], Opts) when Format =:= binary; Format =:= term; is_function(Format, 1) ->
    options(L, Opts#opts{format = Format});
options([{format, binary_term} | L], Opts) ->
    options(L, Opts#opts{format = fun erlang:binary_to_term/1});
options([{size, Size} | L], Opts) when is_integer(Size), Size >= 0 ->
    options(L, Opts#opts{size = max(Size, 1)});
options([{no_files, NoFiles} | L], Opts) when is_integer(NoFiles), NoFiles > 1 ->
    options(L, Opts#opts{no_files = NoFiles});
options([{tmpdir, ""} | L], Opts) ->
    options(L, Opts#opts{tmpdir = default});
options([{tmpdir, Dir} | L], Opts) ->
    try filename:absname(Dir) of
        FileName -> options(L, Opts#opts{tmpdir = {dir, FileName}})
    catch
        _:_ -> {badarg, Dir}
    end;
options([{order, Fun} | L], Opts) when is_function(Fun, 2) ->
    options(L, Opts#opts{order = Fun});
options([{order, Order} | L], Opts) when Order =:= ascending; Order =:= descending ->
    options(L, Opts#opts{order = Order});
options([{compressed, Bool} | L], Opts) when is_boolean(Bool) ->
    options(L, Opts#opts{compressed = Bool});
options([{unique, Bool} | L], Opts) when is_boolean(Bool) ->
    options(L, Opts#opts{unique = Bool});
options([{header, Len} | L], Opts) when is_integer(Len), Len > 0, Len < ?MAXSIZE ->
    options(L, Opts#opts{header = Len});
options([], #opts{format = term, header = Header}) when Header =/= 4 ->
    {badarg, header};
options([], Opts) ->
    Opts;
options([Bad | _], _Opts) ->
    {badarg, Bad};
options(Bad, _Opts) ->
    {badarg, Bad}.

is_keypos(Keypos) when is_integer(Keypos), Keypos > 0 -> true;
is_keypos([]) -> {badarg, []};
is_keypos(L) -> is_keyposs(L).

is_keyposs([Kp | Kps]) when is_integer(Kp), Kp > 0 -> is_keyposs(Kps);
is_keyposs([]) -> true;
is_keyposs([Bad | _]) -> {badarg, Bad};
is_keyposs(Bad) -> {badarg, Bad}.

is_input(Fun) when is_function(Fun, 1) -> {true, Fun};
is_input(Files) -> is_files(Files).

is_files(Fs) -> is_files(Fs, []).

is_files([F | Fs], L) ->
    case read_file_info(F) of
        {ok, File, _} -> is_files(Fs, [File | L]);
        Error -> Error
    end;
is_files([], L) ->
    {true, lists:reverse(L)};
is_files(Bad, _L) ->
    {badarg, Bad}.

maybe_output(Fun) when is_function(Fun, 1) ->
    {true, Fun};
maybe_output(File) ->
    case read_file_info(File) of
        {badarg, _} = Badarg -> Badarg;
        {ok, FileName, _} -> {true, FileName};
        {error, {file_error, FileName, _}} -> {true, FileName}
    end.

% Absolute names, so a later file:set_cwd/1 changes nothing.
read_file_info(File) ->
    try filename:absname(File) of
        FileName ->
            case file:read_file_info(FileName) of
                {ok, FileInfo} -> {ok, FileName, FileInfo};
                {error, einval} -> {badarg, File};
                {error, Reason} -> {error, {file_error, FileName, Reason}}
            end
    catch
        _:_ -> {badarg, File}
    end.

% --- Sorting, merging and checking ---

% A stable key holds the term's input number (I) after its key.
-define(OBJ(X, Y), {X, Y}).
-define(SK(T, I), [T | I]).

do_sort(KeyPos0, Input0, Output0, Opts, Do) ->
    #opts{
        format = Format0,
        size = Size,
        no_files = NoFiles,
        tmpdir = TmpDir,
        order = Order,
        compressed = Compressed,
        unique = Unique,
        header = HdLen
    } = Opts,
    Prefix = tmp_prefix(Output0, TmpDir),
    Ref = make_ref(),
    KeyPos =
        case KeyPos0 of
            [Kp] -> Kp;
            _ -> KeyPos0
        end,
    {Format, Input} = wrap_input(Format0, Do, Input0),
    Z =
        case Compressed of
            true -> [compressed];
            false -> []
        end,
    {Output, FunOut} = wrap_output_terms(Format0, Output0, Z),
    W = #w{
        keypos = KeyPos,
        out = Output,
        fun_out = FunOut,
        prefix = Prefix,
        format = Format,
        runsize = Size,
        no_files = NoFiles,
        order = Order,
        chunksize = ?CHUNKSIZE,
        ref = Ref,
        z = Z,
        unique = Unique,
        hdlen = HdLen,
        inout_value = no_value
    },
    try
        doit(Do, Input, W)
    catch
        throw:{Ref, Error} -> Error
    end.

doit(sort, Input, W) -> files(1, [], 0, W, Input);
doit(merge, Input, W) -> last_merge(Input, W);
doit(check, Input, W) -> check_files(Input, W, []).

% The term format reads and writes through funs over io; its objects are term_to_binary bytes.
wrap_input(term, check, Files) ->
    Fun = fun(File) -> {fn, merge_terms_fun(file_rterms(no_file, [File])), File} end,
    {fun erlang:binary_to_term/1, [Fun(F) || F <- Files]};
wrap_input(Format, check, Files) ->
    {Format, Files};
wrap_input(term, merge, Files) ->
    Input = lists:reverse([merge_terms_fun(file_rterms(no_file, [F])) || F <- Files]),
    {fun erlang:binary_to_term/1, Input};
wrap_input(Format, merge, Files) ->
    {Format, lists:reverse([merge_bins_fun(F) || F <- Files])};
wrap_input(term, sort, InFun) when is_function(InFun, 1) ->
    {fun erlang:binary_to_term/1, fun_rterms(InFun)};
wrap_input(term, sort, Files) ->
    {fun erlang:binary_to_term/1, file_rterms(no_file, Files)};
wrap_input(Format, sort, Input) ->
    {Format, Input}.

merge_terms_fun(RFun) ->
    fun
        (close) ->
            RFun(close);
        ({I, [], _LSz, W}) ->
            case RFun(read) of
                end_of_input ->
                    eof;
                {Objs, NRFun} when is_function(NRFun, 1) ->
                    {_, [], Ts, _} = fun_objs(Objs, [], 0, ?MAXSIZE, I, W),
                    {{I, Ts, ?CHUNKSIZE}, merge_terms_fun(NRFun)};
                Error ->
                    error(Error, W)
            end
    end.

merge_bins_fun(FileName) ->
    fun
        (close) -> ok;
        ({_I, _L, _LSz, W} = A) -> (read_fun(FileName, user, W))(A)
    end.

wrap_output_terms(term, OutFun, _Z) when is_function(OutFun, 1) -> {fun_wterms(OutFun), true};
wrap_output_terms(term, File, Z) when File =/= undefined -> {file_wterms(name, File, Z ++ [write]), false};
wrap_output_terms(_Format, Output, _Z) -> {Output, is_function(Output, 1)}.

% --- Checking ---

check_files([], _W, L) ->
    {ok, lists:reverse(L)};
check_files([FN | FNs], W, L) ->
    {IFun, FileName} =
        case FN of
            {fn, Fun, File} -> {Fun, File};
            File -> {read_fun(File, user, W), File}
        end,
    check_run(IFun, FileName, FNs, W#w{in = IFun}, L, 2, nolast).

check_run(IFun, F, FNs, W, L, I, Last) ->
    case IFun({{merge, I}, [], 0, W}) of
        {{_I, Objs, _LSz}, IFun1} -> check_objs0(IFun1, F, FNs, W#w{in = IFun1}, L, I, Last, lists:reverse(Objs));
        eof -> check_files(FNs, W#w{in = undefined}, L)
    end.

check_objs0(IFun, F, FNs, W, L, I, nolast, [?OBJ(T, _BT) | Os]) -> check_objs1(IFun, F, FNs, W, L, I, T, Os);
check_objs0(IFun, F, FNs, W, L, I, Last, []) -> check_run(IFun, F, FNs, W, L, I, Last);
check_objs0(IFun, F, FNs, W, L, I, {last, Last}, Os) -> check_objs1(IFun, F, FNs, W, L, I, Last, Os).

% Walk the objects while they keep the order: >=, > when unique, =< or < when descending, the fun otherwise.
check_objs1(IFun, F, FNs, #w{order = Order, unique = Unique} = W, L, I, LastT, Os) ->
    Kept = fun(Last, T) -> in_order(Order, Unique, Last, T) end,
    check_objs(IFun, F, FNs, W, L, I, LastT, Os, Kept).

in_order(ascending, false, Last, T) -> T >= Last;
in_order(ascending, true, Last, T) -> T > Last;
in_order(descending, false, Last, T) -> T =< Last;
in_order(descending, true, Last, T) -> T < Last;
in_order(CF, false, Last, T) -> CF(Last, T);
in_order(CF, true, Last, T) -> CF(Last, T) andalso not CF(T, Last).

check_objs(IFun, F, FNs, W, L, I, Last, [?OBJ(T, BT) | Os], Kept) ->
    case Kept(Last, T) of
        true -> check_objs(IFun, F, FNs, W, L, I + 1, T, Os, Kept);
        false -> culprit_found(IFun, F, FNs, W, L, I, BT)
    end;
check_objs(IFun, F, FNs, W, L, I, Last, [], _Kept) ->
    check_run(IFun, F, FNs, W, L, I, {last, Last}).

culprit_found(IFun, F, FNs, W, L, I, [_Size | BT]) ->
    IFun(close),
    check_files(FNs, W, [{F, I, binary_to_term(BT)} | L]).

% --- Runs ---

files(_I, L, _LSz, #w{seq = 1, out = Out} = W, []) ->
    % No temporary files were made: everything is in L.
    case Out of
        Fun when is_function(Fun) ->
            SL = internal_sort(L, W),
            W1 = outfun(binterm_objects(SL, []), W),
            outfun(close, close_input(W1));
        Out ->
            _ = write_run(L, W, Out),
            ok
    end;
files(_I, L, _LSz, W, []) ->
    W1 = write_run(L, W),
    last_merge(lists:append(W1#w.runs), W1);
files(I, L, LSz, W, Fun) when is_function(Fun) ->
    fun_run(I, L, LSz, W#w{in = Fun}, []);
files(I, L, LSz, W, [FileName | FileNames]) ->
    InFun = read_fun(FileName, user, W),
    file_run(InFun, FileNames, I, L, LSz, W#w{in = InFun}).

file_run(InFun, FileNames, I, L, LSz, W) when LSz < W#w.runsize ->
    case InFun({I, L, LSz, W}) of
        {{I1, L1, LSz1}, InFun1} -> file_run(InFun1, FileNames, I1, L1, LSz1, W#w{in = InFun1});
        eof -> files(I, L, LSz, W#w{in = undefined}, FileNames)
    end;
file_run(InFun, FileNames, I, L, _LSz, W) ->
    file_run(InFun, FileNames, I, [], 0, write_run(L, W)).

fun_run(I, L, LSz, W, []) ->
    case infun(W) of
        {end_of_input, NW} -> files(I, L, LSz, NW, []);
        {cont, NW, Objs} -> fun_run(I, L, LSz, NW, Objs)
    end;
fun_run(I, L, LSz, #w{runsize = Runsize} = W, Objs) when LSz < Runsize ->
    {NI, NObjs, NL, NLSz} = fun_objs(Objs, L, LSz, Runsize, I, W),
    fun_run(NI, NL, NLSz, W, NObjs);
fun_run(I, L, _LSz, W, Objs) ->
    fun_run(I, [], 0, write_run(L, W), Objs).

write_run([], W) ->
    W;
write_run(L, W) ->
    {W1, Temp} = next_temp(W),
    NW = write_run(L, W1, Temp),
    [R | Rs] = NW#w.runs,
    merge_runs([[Temp | R] | Rs], [], NW).

write_run(L, W, FileName) ->
    BTs = binterms(internal_sort(L, W), []),
    {Fd, W1} = open_file(FileName, W),
    write(Fd, FileName, BTs, W1),
    close_file(Fd, W1).

% The objects sorted, in reversed order.
internal_sort([] = L, _W) ->
    L;
internal_sort(L, #w{order = CFun, unique = Unique}) when is_function(CFun) ->
    Fun = fun(?OBJ(T1, _), ?OBJ(T2, _)) -> CFun(T1, T2) end,
    RL = lists:reverse(L),
    lists:reverse(
        case Unique of
            true -> lists:usort(Fun, RL);
            false -> lists:sort(Fun, RL)
        end
    );
internal_sort(L, #w{unique = true, keypos = 0} = W) ->
    rev(lists:usort(L), W);
internal_sort(L, #w{unique = false, keypos = 0} = W) ->
    rev(lists:sort(L), W);
internal_sort(L, #w{unique = true} = W) ->
    rev(lists:ukeysort(1, lists:reverse(L)), W);
internal_sort(L, #w{unique = false} = W) ->
    rev(lists:keysort(1, lists:reverse(L)), W).

rev(L, #w{order = ascending}) -> lists:reverse(L);
rev(L, #w{order = descending}) -> L.

% --- Merging ---

last_merge(R, W) when length(R) =< W#w.no_files ->
    case W#w.out of
        Fun when is_function(Fun) ->
            {Fs, W1} = init_merge(lists:reverse(R), 1, [], W),
            W2 = merge_files(Fs, [], 0, nolast, W1),
            outfun(close, close_input(W2));
        Out ->
            _ = merge_files(R, W, Out),
            ok
    end;
last_merge(R, W) ->
    {M, NW} = merge_files(lists:sublist(R, W#w.no_files), W),
    last_merge([M | lists:nthtail(W#w.no_files, R)], NW).

merge_runs([R | Rs], NRs0, W) when length(R) < W#w.no_files ->
    W#w{runs = lists:reverse(NRs0) ++ [R | Rs]};
merge_runs([R], NRs0, W) ->
    {M, NW} = merge_files(R, W),
    NW#w{runs = [[] | lists:reverse([[M] | NRs0])]};
merge_runs([R, R1 | Rs], NRs0, W) ->
    {M, NW} = merge_files(R, W),
    merge_runs([[M | R1] | Rs], [[] | NRs0], NW).

merge_files(R, W) ->
    {W1, Temp} = next_temp(W),
    {Temp, merge_files(R, W1, Temp)}.

merge_files(R, W, FileName) ->
    {Fs, W1} = init_merge(lists:reverse(R), 1, [], W),
    {Fd, W2} = open_file(FileName, W1),
    W4 = merge_files(Fs, [], 0, nolast, W2#w{wfd = {Fd, FileName}}),
    close_file(Fd, W4#w{wfd = undefined}).

% Merge streams [Objects | {FileNumber, InFun}]; the file number keeps the merge stable.
init_merge([FN | FNs], I, Fs, W) ->
    IFun =
        case FN of
            % Only merge/2,3 and keymerge/3,4 pass funs.
            _ when is_function(FN) -> FN;
            _ -> read_fun(FN, fsort, W)
        end,
    W1 = W#w{temp = [IFun | lists:delete(FN, W#w.temp)]},
    case read_more(IFun, I, 0, W1) of
        {Ts, _LSz, NIFun, NW} -> init_merge(FNs, I + 1, [[Ts | {I, NIFun}] | Fs], NW);
        % Only when merging files.
        {eof, NW} -> init_merge(FNs, I + 1, Fs, NW)
    end;
init_merge([], _I, Fs0, #w{order = ascending} = W) ->
    {lists:sort(Fs0), W};
init_merge([], _I, Fs0, #w{order = descending} = W) ->
    {lists:reverse(lists:sort(Fs0)), W};
init_merge([], _I, Fs0, #w{order = Order} = W) when is_function(Order) ->
    {lists:sort(cfun_files(W#w.order), lists:reverse(Fs0)), W}.

cfun_files(CFun) ->
    fun([[?OBJ(T1, _) | _] | _], [[?OBJ(T2, _) | _] | _]) -> CFun(T1, T2) end.

% Last is the last object kept when unique. LSz counts bytes read, not the objects in L; it starts at 0 after
% init_merge, so the first chunk written may be large.
merge_files([F1, F2 | Fs], L0, LSz, Last0, W) when LSz < ?MERGESIZE ->
    [Ts0 | InEtc] = F1,
    Kind = merge_kind(W),
    {Last, L, Ts} = first_kept(Last0, Kind, L0, Ts0),
    [[?OBJ(T2, BT2) | Ts2T] = Ts2 | InEtc2] = F2,
    {NInEtc, NFs, NL, NLast} =
        case Kind of
            umerge ->
                umerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Last);
            {ukmerge, Kp} ->
                ukmerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Kp, Last);
            merge ->
                merge_files(L, F2, Fs, InEtc2, BT2, Ts2T, Ts, InEtc, T2);
            rumerge ->
                rumerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Last);
            {rukmerge, Kp} ->
                {Lt, LtBT} = Last,
                rukmerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Kp, Lt, LtBT);
            rmerge ->
                rmerge_files(L, F2, Fs, InEtc2, BT2, Ts2T, Ts, InEtc, T2);
            {ucmerge, CF} ->
                {I2, _} = InEtc2,
                {I, _} = InEtc,
                ucmerge_files(L, F2, Fs, InEtc2, Ts2, I2, Ts, I, InEtc, T2, CF, Last);
            {cmerge, CF} ->
                {I2, _} = InEtc2,
                {I, _} = InEtc,
                cmerge_files(L, F2, Fs, InEtc2, BT2, Ts2T, I2, Ts, I, InEtc, T2, CF)
        end,
    read_chunk(NInEtc, NFs, NL, LSz, NLast, W);
merge_files([F1], L, LSz, Last, W) when LSz < ?MERGESIZE ->
    [Ts | InEtc] = F1,
    NL = last_file(Ts, L, Last, merge_kind(W), W),
    read_chunk(InEtc, [], NL, LSz, nolast, W);
merge_files([], [], 0, nolast, W) ->
    % When merging files, the output fun is called at least once before closing.
    merge_write(W, []);
merge_files([], L, _LSz, nolast, W) ->
    merge_write(W, L);
merge_files(Fs, L, _LSz, Last, W) ->
    merge_files(Fs, [], 0, Last, merge_write(W, L)).

% The last kept object and the stream's other objects when the merge starts over: the first object is kept.
first_kept({last, Last}, _Kind, L0, Ts0) -> {Last, L0, Ts0};
first_kept(nolast, {ukmerge, _}, _L0, [?OBJ(?SK(T, _I), BT) | Ts1]) -> {T, [BT], Ts1};
first_kept(nolast, {rukmerge, _}, _L0, [?OBJ(?SK(T, _I), BT) | Ts1]) -> {{T, BT}, [], Ts1};
first_kept(nolast, _Kind, _L0, [?OBJ(T, BT) | Ts1]) -> {T, [BT], Ts1}.

merge_kind(#w{order = ascending, unique = true, keypos = 0}) -> umerge;
merge_kind(#w{order = ascending, unique = true, keypos = Kp}) -> {ukmerge, Kp};
merge_kind(#w{order = ascending, unique = false}) -> merge;
merge_kind(#w{order = descending, unique = true, keypos = 0}) -> rumerge;
merge_kind(#w{order = descending, unique = true, keypos = Kp}) -> {rukmerge, Kp};
merge_kind(#w{order = descending, unique = false}) -> rmerge;
merge_kind(#w{order = CF, unique = true}) -> {ucmerge, CF};
merge_kind(#w{order = CF, unique = false}) -> {cmerge, CF}.

merge_write(W, L) ->
    case {W#w.wfd, W#w.out} of
        {undefined, Fun} when is_function(Fun) ->
            outfun(objects(L, []), W);
        {{Fd, FileName}, _} ->
            write(Fd, FileName, lists:reverse(L), W),
            W
    end.

% Each merge takes objects from the current stream (the second argument's head after Ts) while they come before the
% head of the next stream, then switches to the stream that comes first. Unique merges skip objects equal to the
% last kept one.
umerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(T, _BT) | Ts], InEtc, T2, Last) when T == Last ->
    umerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Last);
umerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(T, BT) | Ts], InEtc, T2, _Last) when T =< T2 ->
    umerge_files([BT | L], F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, T);
umerge_files(L, F2, Fs, _InEtc2, _Ts2, [], InEtc, _T2, Last) ->
    {InEtc, [F2 | Fs], L, {last, Last}};
umerge_files(L, _F2, Fs, InEtc2, Ts2, Ts, InEtc, _T2, Last) ->
    [F3 | NFs] = insert([Ts | InEtc], Fs),
    [[?OBJ(T3, _BT3) | _] = Ts3 | InEtc3] = F3,
    umerge_files(L, F3, NFs, InEtc3, Ts3, Ts2, InEtc2, T3, Last).

rumerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(T, _BT) | Ts], InEtc, T2, Last) when T == Last ->
    rumerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Last);
rumerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(T, BT) | Ts], InEtc, T2, _Last) when T >= T2 ->
    rumerge_files([BT | L], F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, T);
rumerge_files(L, F2, Fs, _InEtc2, _Ts2, [], InEtc, _T2, Last) ->
    {InEtc, [F2 | Fs], L, {last, Last}};
rumerge_files(L, _F2, Fs, InEtc2, Ts2, Ts, InEtc, _T2, Last) ->
    [F3 | NFs] = rinsert([Ts | InEtc], Fs),
    [[?OBJ(T3, _BT3) | _] = Ts3 | InEtc3] = F3,
    rumerge_files(L, F3, NFs, InEtc3, Ts3, Ts2, InEtc2, T3, Last).

merge_files(L, F2, Fs, InEtc2, BT2, Ts2, [?OBJ(T, BT) | Ts], InEtc, T2) when T =< T2 ->
    merge_files([BT | L], F2, Fs, InEtc2, BT2, Ts2, Ts, InEtc, T2);
merge_files(L, F2, Fs, _InEtc2, _BT2, _Ts2, [], InEtc, _T2) ->
    {InEtc, [F2 | Fs], L, {last, foo}};
merge_files(L, _F2, Fs, InEtc2, BT2, Ts2, Ts, InEtc, _T2) ->
    [F3 | NFs] = insert([Ts | InEtc], Fs),
    [[?OBJ(T3, BT3) | Ts3] | InEtc3] = F3,
    merge_files([BT2 | L], F3, NFs, InEtc3, BT3, Ts3, Ts2, InEtc2, T3).

rmerge_files(L, F2, Fs, InEtc2, BT2, Ts2, [?OBJ(T, BT) | Ts], InEtc, T2) when T >= T2 ->
    rmerge_files([BT | L], F2, Fs, InEtc2, BT2, Ts2, Ts, InEtc, T2);
rmerge_files(L, F2, Fs, _InEtc2, _BT2, _Ts2, [], InEtc, _T2) ->
    {InEtc, [F2 | Fs], L, {last, foo}};
rmerge_files(L, _F2, Fs, InEtc2, BT2, Ts2, Ts, InEtc, _T2) ->
    [F3 | NFs] = rinsert([Ts | InEtc], Fs),
    [[?OBJ(T3, BT3) | Ts3] | InEtc3] = F3,
    rmerge_files([BT2 | L], F3, NFs, InEtc3, BT3, Ts3, Ts2, InEtc2, T3).

ukmerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(?SK(T, _I), _BT) | Ts], InEtc, T2, Kp, Last) when T == Last ->
    ukmerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Kp, Last);
ukmerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(?SK(T0, _I) = T, BT) | Ts], InEtc, T2, Kp, _Last) when T =< T2 ->
    ukmerge_files([BT | L], F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Kp, T0);
ukmerge_files(L, F2, Fs, _InEtc2, _Ts2, [], InEtc, _T2, _Kp, Last) ->
    {InEtc, [F2 | Fs], L, {last, Last}};
ukmerge_files(L, _F2, Fs, InEtc2, Ts2, Ts, InEtc, _T2, Kp, Last) ->
    [F3 | NFs] = insert([Ts | InEtc], Fs),
    [[?OBJ(T3, _BT3) | _] = Ts3 | InEtc3] = F3,
    ukmerge_files(L, F3, NFs, InEtc3, Ts3, Ts2, InEtc2, T3, Kp, Last).

% Descending unique keyed merges keep the last of equal keys, which stays pending in LastBT until a new key comes.
rukmerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(?SK(T, _I), BT) | Ts], InEtc, T2, Kp, Last, _LastBT) when T == Last ->
    rukmerge_files(L, F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Kp, T, BT);
rukmerge_files(L, F2, Fs, InEtc2, Ts2, [?OBJ(?SK(T0, _I) = T, BT) | Ts], InEtc, T2, Kp, _Last, LastBT) when
    T >= T2
->
    rukmerge_files([LastBT | L], F2, Fs, InEtc2, Ts2, Ts, InEtc, T2, Kp, T0, BT);
rukmerge_files(L, F2, Fs, _InEtc2, _Ts2, [], InEtc, _T2, _Kp, Last, LastBT) ->
    {InEtc, [F2 | Fs], L, {last, {Last, LastBT}}};
rukmerge_files(L, _F2, Fs, InEtc2, Ts2, Ts, InEtc, _T2, Kp, Last, LastBT) ->
    [F3 | NFs] = rinsert([Ts | InEtc], Fs),
    [[?OBJ(T3, _BT3) | _] = Ts3 | InEtc3] = F3,
    rukmerge_files(L, F3, NFs, InEtc3, Ts3, Ts2, InEtc2, T3, Kp, Last, LastBT).

% Merges with an order fun break ties by file number, so they compare in both directions.
ucmerge_files(L, F2, Fs, InEtc2, Ts2, I2, [?OBJ(T, BT) | Ts] = Ts0, I, InEtc, T2, CF, Last) when I < I2 ->
    case CF(T, T2) of
        true ->
            case CF(T, Last) of
                true -> ucmerge_files(L, F2, Fs, InEtc2, Ts2, I2, Ts, I, InEtc, T2, CF, Last);
                false -> ucmerge_files([BT | L], F2, Fs, InEtc2, Ts2, I2, Ts, I, InEtc, T2, CF, T)
            end;
        false ->
            ucmerge_switch(L, Fs, InEtc2, Ts2, I2, Ts0, InEtc, CF, Last)
    end;
ucmerge_files(L, F2, Fs, InEtc2, Ts2, I2, [?OBJ(T, BT) | Ts] = Ts0, I, InEtc, T2, CF, Last) ->
    case CF(T2, T) of
        true ->
            ucmerge_switch(L, Fs, InEtc2, Ts2, I2, Ts0, InEtc, CF, Last);
        false ->
            case CF(T, Last) of
                true -> ucmerge_files(L, F2, Fs, InEtc2, Ts2, I2, Ts, I, InEtc, T2, CF, Last);
                false -> ucmerge_files([BT | L], F2, Fs, InEtc2, Ts2, I2, Ts, I, InEtc, T2, CF, T)
            end
    end;
ucmerge_files(L, F2, Fs, _InEtc2, _Ts2, _I2, [], _I, InEtc, _T2, _CF, Last) ->
    {InEtc, [F2 | Fs], L, {last, Last}}.

ucmerge_switch(L, Fs, InEtc2, Ts2, I2, Ts0, InEtc, CF, Last) ->
    [F3 | NFs] = cinsert([Ts0 | InEtc], Fs, CF),
    [[?OBJ(T3, _BT3) | _] = Ts3 | {I3, _} = InEtc3] = F3,
    ucmerge_files(L, F3, NFs, InEtc3, Ts3, I3, Ts2, I2, InEtc2, T3, CF, Last).

cmerge_files(L, F2, Fs, InEtc2, BT2, Ts2, I2, [?OBJ(T, BT) | Ts] = Ts0, I, InEtc, T2, CF) when I < I2 ->
    case CF(T, T2) of
        true -> cmerge_files([BT | L], F2, Fs, InEtc2, BT2, Ts2, I2, Ts, I, InEtc, T2, CF);
        false -> cmerge_switch([BT2 | L], Fs, InEtc2, Ts2, I2, Ts0, InEtc, CF)
    end;
cmerge_files(L, F2, Fs, InEtc2, BT2, Ts2, I2, [?OBJ(T, BT) | Ts] = Ts0, I, InEtc, T2, CF) ->
    case CF(T2, T) of
        true -> cmerge_switch([BT2 | L], Fs, InEtc2, Ts2, I2, Ts0, InEtc, CF);
        false -> cmerge_files([BT | L], F2, Fs, InEtc2, BT2, Ts2, I2, Ts, I, InEtc, T2, CF)
    end;
cmerge_files(L, F2, Fs, _InEtc2, _BT2, _Ts2, _I2, [], _I, InEtc, _T2, _CF) ->
    {InEtc, [F2 | Fs], L, {last, foo}}.

cmerge_switch(L1, Fs, InEtc2, Ts2, I2, Ts0, InEtc, CF) ->
    [F3 | NFs] = cinsert([Ts0 | InEtc], Fs, CF),
    [[?OBJ(T3, BT3) | Ts3] | {I3, _} = InEtc3] = F3,
    cmerge_files(L1, F3, NFs, InEtc3, BT3, Ts3, I3, Ts2, I2, InEtc2, T3, CF).

% The objects of the last stream left, after the last kept object of a unique merge.
last_file(Ts, L, {last, T}, {ukmerge, _}, _W) -> kulast_file(Ts, T, L);
last_file(Ts, L, {last, {T, BT}}, {rukmerge, _}, _W) -> ruklast_file(Ts, T, BT, L);
last_file(Ts, L, {last, T}, {ucmerge, CF}, _W) -> uclast_file(Ts, T, CF, L);
last_file(Ts, L, {last, T}, _Kind, #w{unique = true}) -> ulast_file(Ts, T, L);
last_file(Ts, L, _Last, _Kind, _W) -> last_file(Ts, L).

ulast_file([?OBJ(T, _BT) | Ts], Last, L) when Last == T -> last_file(Ts, L);
ulast_file(Ts, _Last, L) -> last_file(Ts, L).

kulast_file([?OBJ(?SK(T, _I), _BT) | Ts], Last, L) when Last == T -> last_file(Ts, L);
kulast_file(Ts, _Last, L) -> last_file(Ts, L).

ruklast_file([?OBJ(?SK(T, _I), BT) | Ts], Last, _LastBT, L) when Last == T -> last_file(Ts, [BT | L]);
ruklast_file(Ts, _Last, LastBT, L) -> last_file(Ts, [LastBT | L]).

uclast_file([?OBJ(T, BT) | Ts], Last, CF, L) ->
    case CF(T, Last) of
        true -> last_file(Ts, L);
        false -> last_file(Ts, [BT | L])
    end.

last_file([?OBJ(_Ta, BTa), ?OBJ(_Tb, BTb) | Ts], L) -> last_file(Ts, [BTb, BTa | L]);
last_file([?OBJ(_T, BT) | Ts], L) -> last_file(Ts, [BT | L]);
last_file([], L) -> L.

% Insert a stream among the others by its objects and file number (ascending, descending, or by the order fun).
insert(A, [X1, X2, X3, X4 | Xs]) when A > X4 -> [X1, X2, X3, X4 | insert(A, Xs)];
insert(A, [X1, X2, X3 | T]) when A > X3 -> [X1, X2, X3, A | T];
insert(A, [X1, X2 | Xs]) when A > X2 -> [X1, X2, A | Xs];
insert(A, [X1 | T]) when A > X1 -> [X1, A | T];
insert(A, Xs) -> [A | Xs].

rinsert(A, [X1, X2, X3, X4 | Xs]) when A < X4 -> [X1, X2, X3, X4 | rinsert(A, Xs)];
rinsert(A, [X1, X2, X3 | T]) when A < X3 -> [X1, X2, X3, A | T];
rinsert(A, [X1, X2 | Xs]) when A < X2 -> [X1, X2, A | Xs];
rinsert(A, [X1 | T]) when A < X1 -> [X1, A | T];
rinsert(A, Xs) -> [A | Xs].

cinsert(A, [F1 | [F2 | [F3 | [F4 | Fs] = T4] = T3] = T2] = T1, CF) ->
    case cfun(CF, F4, A) of
        true ->
            [F1, F2, F3, F4 | cinsert(A, Fs, CF)];
        false ->
            case cfun(CF, F2, A) of
                true -> [F1, F2 | cinsert_one(F3, A, T3, T4, CF)];
                false -> cinsert_one(F1, A, T1, T2, CF)
            end
    end;
cinsert(A, [F1 | [F2 | Fs] = T2] = T1, CF) ->
    case cfun(CF, F2, A) of
        true -> [F1, F2 | cinsert(A, Fs, CF)];
        false -> cinsert_one(F1, A, T1, T2, CF)
    end;
cinsert(A, [F | Fs] = T, CF) ->
    cinsert_one(F, A, T, Fs, CF);
cinsert(A, _, _CF) ->
    [A].

cinsert_one(F, A, T1, T2, CF) ->
    case cfun(CF, F, A) of
        true -> [F, A | T2];
        false -> [A | T1]
    end.

cfun(CF, [[?OBJ(T1, _) | _] | {I1, _}], [[?OBJ(T2, _) | _] | {I2, _}]) when I1 < I2 -> CF(T1, T2);
cfun(CF, [[?OBJ(T1, _) | _] | _], [[?OBJ(T2, _) | _] | _]) -> not CF(T2, T1).

binterm_objects([?OBJ(_T, [_Sz | BT]) | Ts], L) -> binterm_objects(Ts, [BT | L]);
binterm_objects([], L) -> L.

objects([[_Sz | BT] | Ts], L) -> objects(Ts, [BT | L]);
objects([], L) -> L.

binterms([?OBJ(_T1, BT1), ?OBJ(_T2, BT2) | Ts], L) -> binterms(Ts, [BT2, BT1 | L]);
binterms([?OBJ(_T, BT) | Ts], L) -> binterms(Ts, [BT | L]);
binterms([], L) -> L.

read_chunk({I, IFun}, Fs, L, LSz, Last, W) ->
    case read_more(IFun, I, LSz, W) of
        {Ts, NLSz, NIFun, #w{order = ascending} = NW} ->
            merge_files(insert([Ts | {I, NIFun}], Fs), L, NLSz, Last, NW);
        {Ts, NLSz, NIFun, #w{order = descending} = NW} ->
            merge_files(rinsert([Ts | {I, NIFun}], Fs), L, NLSz, Last, NW);
        {Ts, NLSz, NIFun, NW} ->
            merge_files(cinsert([Ts | {I, NIFun}], Fs, NW#w.order), L, NLSz, Last, NW);
        {eof, NW} ->
            merge_files(Fs, L, LSz, Last, NW)
    end.

read_more(IFun, I, LSz, W) ->
    case IFun({{merge, I}, [], LSz, W}) of
        {{_, [], NLSz}, NIFun} ->
            read_more(NIFun, I, NLSz, W);
        {{_, L, NLSz}, NInFun} ->
            NW =
                case lists:member(IFun, W#w.temp) of
                    % A temporary file.
                    true -> W#w{temp = [NInFun | lists:delete(IFun, W#w.temp)]};
                    % An input file.
                    false -> W
                end,
            {lists:reverse(L), NLSz, NInFun, NW};
        eof ->
            % Already closed.
            {eof, W#w{temp = lists:delete(IFun, W#w.temp)}}
    end.

% --- Reading files ---

read_fun(FileName, Owner, W) ->
    case file:open(FileName, [raw, binary, read, compressed]) of
        {ok, Fd} -> read_fun2(Fd, <<>>, 0, FileName, Owner);
        Error -> file_error(FileName, Error, W)
    end.

read_fun2(Fd, Bin, Size, FileName, Owner) ->
    fun
        (close) ->
            close_read_fun(Fd, FileName, Owner);
        ({I, L, LSz, W}) ->
            case read_objs(Fd, FileName, I, L, Bin, Size, LSz, W) of
                {{I1, L1, Bin1, Size1}, LSz1} ->
                    {{I1, L1, LSz1}, read_fun2(Fd, Bin1, Size1, FileName, Owner)};
                eof ->
                    close_read_fun(Fd, FileName, Owner),
                    eof
            end
    end.

% A temporary file (fsort) is removed once read.
close_read_fun(Fd, _FileName, user) ->
    _ = file:close(Fd),
    ok;
close_read_fun(Fd, FileName, fsort) ->
    _ = file:close(Fd),
    _ = file:delete(FileName),
    ok.

% Read at least what the pending object needs and at least a chunk; split the bytes into objects.
read_objs(Fd, FileName, I, L, Bin0, Size0, LSz, W) ->
    Max = max(Size0, ?CHUNKSIZE),
    BSz0 = byte_size(Bin0),
    Min = Size0 - BSz0 + W#w.hdlen,
    case read(Fd, FileName, max(Min, Max), W) of
        {ok, Bin} ->
            BSz = byte_size(Bin),
            try file_loop(L, I, Bin0, Bin, Size0, BSz0, BSz, Min, W) of
                Reply -> {Reply, LSz + BSz}
            catch
                _:_ -> error({error, {bad_object, FileName}}, W)
            end;
        eof when byte_size(Bin0) =:= 0 ->
            eof;
        eof ->
            error({error, {premature_eof, FileName}}, W)
    end.

file_loop(L, I, _B1, B2, Sz, 0, _B2Sz, _Min, W) ->
    file_loop(L, I, B2, Sz, W);
file_loop(L, I, B1, B2, Sz, _B1Sz, B2Sz, Min, W) when B2Sz > Min ->
    {B3, B4} = split_binary(B2, Min),
    {I1, L1, <<>>, Sz1} = file_loop(L, I, list_to_binary([B1, B3]), Sz, W),
    file_loop(L1, I1, B4, Sz1, W);
file_loop(L, I, B1, B2, Sz, _B1Sz, _B2Sz, _Min, W) ->
    file_loop(L, I, list_to_binary([B1, B2]), Sz, W).

file_loop(L, I, B, Sz, #w{keypos = Kp, format = Format, hdlen = HdLen}) ->
    file_loop1(L, I, B, Sz, Kp, Format, HdLen).

file_loop1(L, I, HB, 0, Kp, F, HdLen) ->
    <<Size:HdLen/unit:8, B/binary>> = HB,
    file_loop2(L, I, B, Size, <<Size:HdLen/unit:8>>, Kp, F, HdLen);
file_loop1(L, I, B, Sz, Kp, F, HdLen) ->
    file_loop2(L, I, B, Sz, <<Sz:HdLen/unit:8>>, Kp, F, HdLen).

file_loop2(L, _I, B, Sz, SzB, 0, binary, HdLen) ->
    {NL, NB, NSz, NSzB} = file_binloop(L, Sz, SzB, B, HdLen),
    case byte_size(NB) =:= NSz of
        true ->
            <<Bin:NSz/binary>> = NB,
            {0, [?OBJ(Bin, [NSzB | Bin]) | NL], <<>>, 0};
        false ->
            {0, NL, NB, NSz}
    end;
file_loop2(L, _I, B, Sz, SzB, 0, Fun, HdLen) ->
    file_binterm_loop(L, Sz, SzB, B, Fun, HdLen);
file_loop2(L, {merge, I}, B, Sz, SzB, Kp, Fun, HdLen) ->
    merge_loop(Kp, I, L, Sz, SzB, B, Fun, HdLen);
file_loop2(L, I, B, Sz, SzB, Kp, Fun, HdLen) when is_integer(I) ->
    key_loop(Kp, I, L, Sz, SzB, B, Fun, HdLen).

file_binloop(L, Size, SizeB, B, HL) ->
    case B of
        <<Bin:Size/binary, NSizeB:HL/binary, R/binary>> ->
            <<NSize:HL/unit:8>> = NSizeB,
            file_binloop([?OBJ(Bin, [SizeB | Bin]) | L], NSize, NSizeB, R, HL);
        _ ->
            {L, B, Size, SizeB}
    end.

file_binterm_loop(L, Size, SizeB, B, Fun, HL) ->
    case B of
        <<BinTerm:Size/binary, NSizeB:HL/binary, R/binary>> ->
            <<NSize:HL/unit:8>> = NSizeB,
            file_binterm_loop([?OBJ(Fun(BinTerm), [SizeB | BinTerm]) | L], NSize, NSizeB, R, Fun, HL);
        <<BinTerm:Size/binary>> ->
            {0, [?OBJ(Fun(BinTerm), [SizeB | BinTerm]) | L], <<>>, 0};
        _ ->
            {0, L, B, Size}
    end.

% Sorting reads keys (the input number I keeps it stable), merging stable keys of the file number.
key_loop(KeyPos, I, L, Size, SizeB, B, Fun, HL) ->
    case B of
        <<BinTerm:Size/binary, NSizeB:HL/binary, R/binary>> ->
            <<NSize:HL/unit:8>> = NSizeB,
            E = ?OBJ(make_key(KeyPos, Fun(BinTerm)), [SizeB | BinTerm]),
            key_loop(KeyPos, I + 1, [E | L], NSize, NSizeB, R, Fun, HL);
        <<BinTerm:Size/binary>> ->
            {I + 1, [?OBJ(make_key(KeyPos, Fun(BinTerm)), [SizeB | BinTerm]) | L], <<>>, 0};
        _ ->
            {I, L, B, Size}
    end.

merge_loop(KeyPos, I, L, Size, SizeB, B, Fun, HL) ->
    case B of
        <<BinTerm:Size/binary, NSizeB:HL/binary, R/binary>> ->
            <<NSize:HL/unit:8>> = NSizeB,
            E = ?OBJ(make_stable_key(KeyPos, I, Fun(BinTerm)), [SizeB | BinTerm]),
            merge_loop(KeyPos, I, [E | L], NSize, NSizeB, R, Fun, HL);
        <<BinTerm:Size/binary>> ->
            {{merge, I}, [?OBJ(make_stable_key(KeyPos, I, Fun(BinTerm)), [SizeB | BinTerm]) | L], <<>>, 0};
        _ ->
            {{merge, I}, L, B, Size}
    end.

% Objects of an input fun's list, up to the run size.
fun_objs(Objs, L, LSz, NoBytes, I, #w{keypos = Keypos, format = Format, hdlen = HL} = W) ->
    try
        fun_loop(Objs, L, LSz, NoBytes, I, Keypos, Format, HL)
    catch
        _:_ -> error({error, bad_object}, W)
    end.

fun_loop(Objs, L, LSz, RunSize, _I, 0, binary, HdLen) ->
    fun_binloop(Objs, L, LSz, RunSize, HdLen);
fun_loop(Objs, L, LSz, RunSize, _I, 0, Fun, HdLen) ->
    fun_loop(Objs, L, LSz, RunSize, Fun, HdLen);
fun_loop(Objs, L, LSz, RunSize, {merge, I}, Keypos, Fun, HdLen) ->
    fun_mergeloop(Objs, L, LSz, RunSize, I, Keypos, Fun, HdLen);
fun_loop(Objs, L, LSz, RunSize, I, Keypos, Fun, HdLen) when is_integer(I) ->
    fun_keyloop(Objs, L, LSz, RunSize, I, Keypos, Fun, HdLen).

fun_binloop([B | Bs], L, LSz, RunSize, HL) when LSz < RunSize ->
    Size = byte_size(B),
    fun_binloop(Bs, [?OBJ(B, [<<Size:HL/unit:8>> | B]) | L], LSz + Size, RunSize, HL);
fun_binloop(Bs, L, LSz, _RunSize, _HL) ->
    {0, Bs, L, LSz}.

fun_loop([B | Bs], L, LSz, RunSize, Fun, HL) when LSz < RunSize ->
    Size = byte_size(B),
    fun_loop(Bs, [?OBJ(Fun(B), [<<Size:HL/unit:8>> | B]) | L], LSz + Size, RunSize, Fun, HL);
fun_loop(Bs, L, LSz, _RunSize, _Fun, _HL) ->
    {0, Bs, L, LSz}.

fun_keyloop([B | Bs], L, LSz, RunSize, I, Kp, Fun, HL) when LSz < RunSize ->
    Size = byte_size(B),
    E = ?OBJ(make_key(Kp, Fun(B)), [<<Size:HL/unit:8>> | B]),
    fun_keyloop(Bs, [E | L], LSz + Size, RunSize, I + 1, Kp, Fun, HL);
fun_keyloop(Bs, L, LSz, _RunSize, I, _Kp, _Fun, _HL) ->
    {I, Bs, L, LSz}.

fun_mergeloop([B | Bs], L, LSz, RunSize, I, Kp, Fun, HL) when LSz < RunSize ->
    Size = byte_size(B),
    E = ?OBJ(make_stable_key(Kp, I, Fun(B)), [<<Size:HL/unit:8>> | B]),
    fun_mergeloop(Bs, [E | L], LSz + Size, RunSize, I, Kp, Fun, HL);
fun_mergeloop(Bs, L, LSz, _RunSize, I, _Kp, _Fun, _HL) ->
    {{merge, I}, Bs, L, LSz}.

make_key(Kp, T) when is_integer(Kp) -> element(Kp, T);
make_key([Kp1, Kp2], T) -> [element(Kp1, T), element(Kp2, T)];
make_key([Kp1, Kp2 | Kps], T) -> [element(Kp1, T), element(Kp2, T) | make_key2(Kps, T)].

make_stable_key(Kp, I, T) when is_integer(Kp) -> ?SK(element(Kp, T), I);
make_stable_key([Kp1, Kp2], I, T) -> ?SK([element(Kp1, T) | element(Kp2, T)], I);
make_stable_key([Kp1, Kp2 | Kps], I, T) -> ?SK([element(Kp1, T), element(Kp2, T) | make_key2(Kps, T)], I).

make_key2([Kp], T) -> [element(Kp, T)];
make_key2([Kp | Kps], T) -> [element(Kp, T) | make_key2(Kps, T)].

% --- Input and output funs ---

infun(W) ->
    W1 = W#w{in = undefined},
    try (W#w.in)(read) of
        end_of_input -> {end_of_input, W1};
        {end_of_input, Value} -> {end_of_input, W1#w{inout_value = {value, Value}}};
        {Objs, NFun} when is_function(NFun, 1), is_list(Objs) -> {cont, W#w{in = NFun}, Objs};
        Error -> error(Error, W1)
    catch
        Class:Reason:Stacktrace ->
            cleanup(W1),
            erlang:raise(Class, Reason, Stacktrace)
    end.

outfun(A, #w{inout_value = Val} = W) when Val =/= no_value ->
    W1 = W#w{inout_value = no_value},
    W2 =
        case W1#w.fun_out of
            true -> outfun(Val, W1);
            false -> W1
        end,
    outfun(A, W2);
outfun(A, W) ->
    W1 = W#w{out = undefined},
    try (W#w.out)(A) of
        Reply when A =:= close -> Reply;
        NF when is_function(NF, 1) -> W#w{out = NF};
        Error -> error(Error, W1)
    catch
        Class:Reason:Stacktrace ->
            cleanup(W1),
            erlang:raise(Class, Reason, Stacktrace)
    end.

% --- Temporary files and errors ---

% Existing files are overwritten.
next_temp(#w{seq = Seq, prefix = Prefix} = W) ->
    {W#w{seq = Seq + 1}, lists:concat([Prefix, Seq])}.

% Temporary names fs_Node_OsPid_Unique.Seq in the output's directory (the working directory for an output fun) or
% the tmpdir option's.
tmp_prefix(F, TmpDirOpt) when is_function(F); F =:= undefined ->
    {ok, CurDir} = file:get_cwd(),
    tmp_prefix1(CurDir, TmpDirOpt);
tmp_prefix(OutFile, TmpDirOpt) ->
    tmp_prefix1(filename:dirname(OutFile), TmpDirOpt).

tmp_prefix1(Dir, TmpDirOpt) ->
    Name = lists:concat(["fs_", node(), "_", os:getpid(), "_", erlang:unique_integer([positive]), "."]),
    TmpDir =
        case TmpDirOpt of
            default -> Dir;
            {dir, TDir} -> TDir
        end,
    filename:join(filename:absname(TmpDir), Name).

open_file(FileName, W) ->
    case file:open(FileName, W#w.z ++ [raw, binary, write]) of
        {ok, Fd} -> {Fd, W#w{temp = [{Fd, FileName} | W#w.temp]}};
        Error -> file_error(FileName, Error, W)
    end.

read(Fd, FileName, N, W) ->
    case file:read(Fd, N) of
        {ok, Bin} -> {ok, Bin};
        eof -> eof;
        % A bad size.
        {error, Reason} when Reason =:= enomem; Reason =:= einval -> error({error, {bad_object, FileName}}, W);
        Error -> file_error(FileName, Error, W)
    end.

write(Fd, FileName, B, W) ->
    case file:write(Fd, B) of
        ok -> ok;
        Error -> file_error(FileName, Error, W)
    end.

file_error(File, {error, Reason}, W) -> error({error, {file_error, File, Reason}}, W).

% End the call with Error, the temporary files removed and the funs closed.
error(Error, W) ->
    cleanup(W),
    throw({W#w.ref, Error}).

cleanup(W) ->
    close_out(W),
    W1 = close_input(W),
    lists:foreach(fun cleanup_temp/1, W1#w.temp).

cleanup_temp(IFun) when is_function(IFun) ->
    IFun(close);
cleanup_temp({Fd, FileName}) ->
    _ = file:close(Fd),
    _ = file:delete(FileName);
cleanup_temp(FileName) ->
    _ = file:delete(FileName).

close_input(#w{in = In} = W) when is_function(In) ->
    _ = (catch In(close)),
    W#w{in = undefined};
close_input(#w{in = undefined} = W) ->
    W.

close_out(#w{out = Out}) when is_function(Out) ->
    _ = (catch Out(close)),
    ok;
close_out(_) ->
    ok.

close_file(Fd, W) ->
    {Fd, FileName} = lists:keyfind(Fd, 1, W#w.temp),
    case file:close(Fd) of
        ok -> W#w{temp = [FileName | lists:keydelete(Fd, 1, W#w.temp)]};
        Error -> file_error(FileName, Error, W)
    end.

% --- The term format ---

file_rterms(no_file, Files) ->
    fun
        (close) ->
            ok;
        (read) when Files =:= [] ->
            end_of_input;
        (read) ->
            [F | Fs] = Files,
            case file:open(F, [read, compressed]) of
                {ok, Fd} -> file_rterms2(Fd, [], 0, F, Fs);
                {error, Reason} -> {error, {file_error, F, Reason}}
            end
    end;
file_rterms({Fd, FileName}, Files) ->
    fun
        (close) -> file:close(Fd);
        (read) -> file_rterms2(Fd, [], 0, FileName, Files)
    end.

file_rterms2(Fd, L, LSz, FileName, Files) when LSz < ?CHUNKSIZE ->
    case io:read(Fd, '') of
        {ok, Term} ->
            B = term_to_binary(Term),
            file_rterms2(Fd, [B | L], LSz + byte_size(B), FileName, Files);
        eof ->
            _ = file:close(Fd),
            {lists:reverse(L), file_rterms(no_file, Files)};
        _Error ->
            _ = file:close(Fd),
            {error, {bad_term, FileName}}
    end;
file_rterms2(Fd, L, _LSz, FileName, Files) ->
    {lists:reverse(L), file_rterms({Fd, FileName}, Files)}.

file_wterms(W, F, Args) ->
    fun
        (close) when W =:= name ->
            ok;
        (close) ->
            {fd, Fd} = W,
            file:close(Fd);
        (L) when W =:= name ->
            case file:open(F, Args) of
                {ok, Fd} -> write_terms(Fd, F, L, Args);
                {error, Reason} -> {error, {file_error, F, Reason}}
            end;
        (L) ->
            {fd, Fd} = W,
            write_terms(Fd, F, L, Args)
    end.

write_terms(Fd, F, [B | Bs], Args) ->
    case io:request(Fd, {format, "~p.~n", [binary_to_term(B)]}) of
        ok ->
            write_terms(Fd, F, Bs, Args);
        {error, Reason} ->
            _ = file:close(Fd),
            {error, {file_error, F, Reason}}
    end;
write_terms(Fd, F, [], Args) ->
    file_wterms({fd, Fd}, F, Args).

fun_rterms(InFun) ->
    fun
        (close) ->
            InFun(close);
        (read) ->
            case InFun(read) of
                {Ts, NInFun} when is_list(Ts), is_function(NInFun, 1) ->
                    {[term_to_binary(T) || T <- Ts], fun_rterms(NInFun)};
                Else ->
                    Else
            end
    end.

fun_wterms(OutFun) ->
    fun
        (close) ->
            OutFun(close);
        (L) ->
            case OutFun(wterms_arg(L)) of
                NOutFun when is_function(NOutFun, 1) -> fun_wterms(NOutFun);
                Else -> Else
            end
    end.

wterms_arg(L) when is_list(L) -> [binary_to_term(B) || B <- L];
wterms_arg(Value) -> Value.
