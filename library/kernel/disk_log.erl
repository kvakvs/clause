%% Clause's disk_log (plan 21 step LIB52, docs/libraries/library.md): halt, wrap and rotate logs of terms or bytes, written
%% from OTP's documentation with OTP's file formats (clause_disk_log), so logs are exchangeable with OTP. Each open
%% log has a process that owns its files, with owners (linked, notified on request) and anonymous users; a registry
%% server named disk_log_server (clause_disk_log_server) keeps the named tables disk_log_names and disk_log_pids and
%% starts log processes.
%% Requests while a log is blocked are queued, or refused when it does not queue; log requests that arrive together
%% are written at once.
-module(disk_log).
-export([
    all/0,
    alog/2,
    alog_terms/2,
    balog/2,
    balog_terms/2,
    bchunk/2,
    bchunk/3,
    block/1,
    block/2,
    blog/2,
    blog_terms/2,
    breopen/3,
    btruncate/2,
    change_header/2,
    change_notify/3,
    change_size/2,
    chunk/2,
    chunk/3,
    chunk_info/1,
    chunk_step/3,
    close/1,
    format_error/1,
    inc_wrap_file/1,
    info/1,
    log/2,
    log_terms/2,
    next_file/1,
    open/1,
    pid2name/1,
    reopen/2,
    reopen/3,
    sync/1,
    truncate/1,
    truncate/2,
    unblock/1
]).
%% OTP's internal exports: wrap_log_reader turns chunks into terms with ichunk_end/2; the rest serve OTP's processes.
-export([
    do_info/2,
    do_log/2,
    do_sync/1,
    ichunk_end/2,
    init/2,
    internal_open/2,
    istart_link/1,
    ll_close/1,
    ll_open/1,
    start/0,
    system_code_change/4,
    system_continue/3,
    system_terminate/4
]).
-include("disk_log.types.hrl").
%% The log process's own atoms (requests, reasons) never name a called module.
-compile({clause_liveness, strict}).

-define(CONTINUATION_RECORD, true).
-include("clause_disk_log.hrl").

% A log process's state: requests queued while blocked, requests to handle before new ones, its parent (the registry
% server), the number of items on the log, the open/1 options, the last error status and a cache write error after a
% timer.
-record(state, {queue = [], messages = [], parent, server, cnt = 0, args, error_status = ok, cache_error = ok}).

-define(failure(Error, Function, Arg), {{failed, Error}, [{?MODULE, Function, Arg}]}).

% --- API ---

open(A) -> server_open(check_arg(A, #arg{options = A})).

log(Log, Term) -> req(Log, {log, internal, [term_to_binary(Term)]}).

blog(Log, Bytes) -> req(Log, {log, external, [ensure_binary(Bytes)]}).

log_terms(Log, Terms) -> req(Log, {log, internal, [term_to_binary(T) || T <- Terms]}).

blog_terms(Log, Bytess) -> req(Log, {log, external, ensure_binary_list(Bytess)}).

alog(Log, Term) -> notify(Log, {alog, internal, [term_to_binary(Term)]}).

alog_terms(Log, Terms) -> notify(Log, {alog, internal, [term_to_binary(T) || T <- Terms]}).

balog(Log, Bytes) -> notify(Log, {alog, external, [ensure_binary(Bytes)]}).

balog_terms(Log, Bytess) -> notify(Log, {alog, external, ensure_binary_list(Bytess)}).

close(Log) -> req(Log, close).

truncate(Log) -> req(Log, {truncate, none, truncate, 1}).

truncate(Log, Head) -> req(Log, {truncate, {ok, term_to_binary(Head)}, truncate, 2}).

btruncate(Log, Head) -> req(Log, {truncate, {ok, ensure_binary(Head)}, btruncate, 2}).

reopen(Log, NewFile) -> req(Log, {reopen, NewFile, none, reopen, 2}).

reopen(Log, NewFile, NewHead) -> req(Log, {reopen, NewFile, {ok, term_to_binary(NewHead)}, reopen, 3}).

breopen(Log, NewFile, NewHead) -> req(Log, {reopen, NewFile, {ok, ensure_binary(NewHead)}, breopen, 3}).

next_file(Log) -> req(Log, next_file).

inc_wrap_file(Log) -> req(Log, inc_wrap_file).

change_size(Log, NewSize) -> req(Log, {change_size, NewSize}).

change_notify(Log, Pid, NewNotify) -> req(Log, {change_notify, Pid, NewNotify}).

change_header(Log, NewHead) -> req(Log, {change_header, NewHead}).

sync(Log) -> req(Log, sync).

block(Log) -> block(Log, true).

block(Log, QueueLogRecords) -> req(Log, {block, QueueLogRecords}).

unblock(Log) -> req(Log, unblock).

format_error(Error) -> do_format_error(Error).

info(Log) -> req(Log, info).

pid2name(Pid) ->
    start(),
    case ets:lookup(?DISK_LOG_PID_TABLE, Pid) of
        [] -> undefined;
        [{_Pid, Log}] -> {ok, Log}
    end.

% There cannot be more than ?MAX_CHUNK_SIZE terms in a chunk.
chunk(Log, Cont) -> chunk(Log, Cont, infinity).

chunk(Log, Cont, infinity) -> ichunk(Log, Cont, ?MAX_CHUNK_SIZE);
chunk(Log, Cont, N) when is_integer(N), N > 0 -> ichunk(Log, Cont, N).

ichunk(Log, start, N) ->
    ichunk_end(req(Log, {chunk, 0, [], N}), Log);
ichunk(Log, #continuation{pid = Pid, pos = Pos, b = B}, N) ->
    ichunk_end(monitor_request(Pid, {chunk, Pos, B, N}), Log);
ichunk(_Log, _, _) ->
    {error, {badarg, continuation}}.

% The terms of a chunk's binaries; a binary that is no term makes a written log's chunk corrupt, and is skipped and
% counted as bad bytes of a read-only one.
ichunk_end({#continuation{} = C, R}, Log) -> ichunk_end(R, read_write, Log, C, 0);
ichunk_end({#continuation{} = C, R, Bad}, Log) -> ichunk_end(R, read_only, Log, C, Bad);
ichunk_end(R, _Log) -> R.

ichunk_end(R, Mode, Log, C, Bad) ->
    try bins2terms(R, []) of
        Ts when Bad > 0 -> {C, Ts, Bad};
        Ts -> {C, Ts}
    catch
        _:_ -> ichunk_bad_end(lists:reverse(R), Mode, Log, C, Bad, [])
    end.

bins2terms([], L) -> L;
bins2terms([B | Bs], L) -> bins2terms(Bs, [binary_to_term(B) | L]).

ichunk_bad_end([B | Bs], Mode, Log, C, Bad, A) ->
    try binary_to_term(B) of
        T -> ichunk_bad_end(Bs, Mode, Log, C, Bad, [T | A])
    catch
        _:_ when Mode =:= read_write ->
            {value, {file, FileName}} = lists:keysearch(file, 1, info(Log)),
            File =
                case C#continuation.pos of
                    Pos when is_integer(Pos) -> FileName;
                    {FileNo, _} -> add_ext(FileName, FileNo)
                end,
            {error, {corrupt_log_file, File}};
        _:_ ->
            Reread = lists:foldl(fun(Bin, Sz) -> Sz + byte_size(Bin) + ?HEADERSZ end, 0, Bs),
            NewPos =
                case C#continuation.pos of
                    Pos when is_integer(Pos) -> Pos - Reread;
                    {FileNo, Pos} -> {FileNo, Pos - Reread}
                end,
            {C#continuation{pos = NewPos, b = []}, lists:reverse(A), Bad + byte_size(B)}
    end.

bchunk(Log, Cont) -> bchunk(Log, Cont, infinity).

bchunk(Log, Cont, infinity) -> bichunk(Log, Cont, ?MAX_CHUNK_SIZE);
bchunk(Log, Cont, N) when is_integer(N), N > 0 -> bichunk(Log, Cont, N).

bichunk(Log, start, N) -> bichunk_end(req(Log, {chunk, 0, [], N}));
bichunk(_Log, #continuation{pid = Pid, pos = Pos, b = B}, N) -> bichunk_end(monitor_request(Pid, {chunk, Pos, B, N}));
bichunk(_Log, _, _) -> {error, {badarg, continuation}}.

bichunk_end({#continuation{} = C, R}) -> {C, lists:reverse(R)};
bichunk_end({#continuation{} = C, R, Bad}) -> {C, lists:reverse(R), Bad};
bichunk_end(R) -> R.

chunk_step(Log, Cont, N) when is_integer(N) -> ichunk_step(Log, Cont, N).

ichunk_step(Log, start, N) -> req(Log, {chunk_step, 0, N});
ichunk_step(_Log, #continuation{pid = Pid, pos = Pos}, N) -> monitor_request(Pid, {chunk_step, Pos, N});
ichunk_step(_Log, _, _) -> {error, {badarg, continuation}}.

chunk_info(#continuation{pid = Pid}) -> [{node, node(Pid)}];
chunk_info(BadCont) -> {error, {no_continuation, BadCont}}.

all() ->
    start(),
    lists:sort([Name || [Name] <- ets:match(?DISK_LOG_NAME_TABLE, {'$1', '_'})]).

% --- OTP's internal entry points ---

istart_link(Server) -> {ok, proc_lib:spawn_link(?MODULE, init, [self(), Server])}.

start() -> clause_disk_log_server:start().

internal_open(Pid, A) -> monitor_request(Pid, {internal_open, A}).

ll_open(A) ->
    case check_arg(A, #arg{options = A}) of
        {ok, L} -> do_open(L);
        Error -> Error
    end.

ll_close(Log) -> close_disk_log2(Log).

% --- Checking open/1 options ---

check_arg([], Res) ->
    Ret =
        case Res#arg.head of
            none ->
                {ok, Res};
            _ ->
                case check_head(Res#arg.head, Res#arg.format) of
                    {ok, Head} -> {ok, Res#arg{head = Head}};
                    Error -> Error
                end
        end,
    checked_arg(Ret, Res);
check_arg([{file, F} | Tail], Res) when is_list(F); is_atom(F) ->
    check_arg(Tail, Res#arg{file = F});
check_arg([{linkto, Pid} | Tail], Res) when is_pid(Pid); Pid =:= none ->
    check_arg(Tail, Res#arg{linkto = Pid});
check_arg([{name, Name} | Tail], Res) ->
    check_arg(Tail, Res#arg{name = Name});
check_arg([{repair, Repair} | Tail], Res) when Repair =:= true; Repair =:= false; Repair =:= truncate ->
    check_arg(Tail, Res#arg{repair = Repair});
check_arg([{size, Int} | Tail], Res) when is_integer(Int), Int > 0 ->
    check_arg(Tail, Res#arg{size = Int});
check_arg([{size, infinity} | Tail], Res) ->
    check_arg(Tail, Res#arg{size = infinity});
check_arg([{size, {MaxB, MaxF}} | Tail], Res) when
    is_integer(MaxB), is_integer(MaxF), MaxB > 0, MaxB =< ?MAX_BYTES, MaxF > 0, MaxF < ?MAX_FILES
->
    check_arg(Tail, Res#arg{size = {MaxB, MaxF}});
check_arg([{type, Type} | Tail], Res) when Type =:= rotate; Type =:= wrap; Type =:= halt ->
    check_arg(Tail, Res#arg{type = Type});
check_arg([{format, Format} | Tail], Res) when Format =:= internal; Format =:= external ->
    check_arg(Tail, Res#arg{format = Format});
check_arg([{notify, Notify} | Tail], Res) when is_boolean(Notify) ->
    check_arg(Tail, Res#arg{notify = Notify});
check_arg([{head_func, HeadFunc} | Tail], Res) ->
    check_arg(Tail, Res#arg{head = {head_func, HeadFunc}});
check_arg([{head, Term} | Tail], Res) ->
    check_arg(Tail, Res#arg{head = {head, Term}});
check_arg([{mode, Mode} | Tail], Res) when Mode =:= read_only; Mode =:= read_write ->
    check_arg(Tail, Res#arg{mode = Mode});
check_arg([{quiet, Boolean} | Tail], Res) when is_boolean(Boolean) ->
    check_arg(Tail, Res#arg{quiet = Boolean});
check_arg(Arg, _) ->
    {error, {badarg, Arg}}.

% The whole options checked; a missing file name is the log name with ".LOG".
checked_arg(_Ret, #arg{name = 0}) ->
    {error, {badarg, name}};
checked_arg(_Ret, #arg{file = none} = Res) ->
    try lists:concat([Res#arg.name, ".LOG"]) of
        FName -> check_arg([], Res#arg{file = FName})
    catch
        _:_ -> {error, {badarg, file}}
    end;
checked_arg(_Ret, #arg{repair = truncate, mode = read_only}) ->
    {error, {badarg, repair_read_only}};
checked_arg(_Ret, #arg{type = halt, size = Size}) when is_tuple(Size) ->
    {error, {badarg, size}};
checked_arg(Ret, #arg{type = wrap, file = File}) ->
    {OldSize, Version} = clause_disk_log:read_size_file_version(File),
    check_wrap_arg(Ret, OldSize, Version);
checked_arg({ok, Patched}, #arg{type = rotate}) ->
    {ok, Patched#arg{format = external}};
checked_arg(Ret, _Res) ->
    Ret.

check_wrap_arg({ok, #arg{size = infinity}}, {0, 0}, _Version) ->
    {error, {badarg, size}};
check_wrap_arg({ok, #arg{size = infinity} = Res}, OldSize, Version) ->
    {ok, Res#arg{version = Version, old_size = OldSize, size = OldSize}};
check_wrap_arg({ok, Res}, {0, 0}, Version) ->
    {ok, Res#arg{version = Version, old_size = Res#arg.size}};
check_wrap_arg({ok, #arg{size = OldSize} = Res}, OldSize, Version) ->
    {ok, Res#arg{version = Version, old_size = OldSize}};
check_wrap_arg({ok, #arg{repair = truncate, size = Size} = Res}, OldSize, Version) when is_tuple(Size) ->
    {ok, Res#arg{version = Version, old_size = OldSize}};
check_wrap_arg({ok, #arg{mode = read_only} = Res}, OldSize, _Version) ->
    {error, {size_mismatch, OldSize, Res#arg.size}};
check_wrap_arg({ok, #arg{size = Size} = Res}, OldSize, Version) when is_tuple(Size) ->
    {ok, Res#arg{version = Version, old_size = OldSize}};
check_wrap_arg({ok, _Res}, _OldSize, _Version) ->
    {error, {badarg, size}};
check_wrap_arg(Ret, _OldSize, _Version) ->
    Ret.

% --- The log process ---

init(Parent, Server) ->
    process_flag(trap_exit, true),
    loop(#state{parent = Parent, server = Server}).

loop(#state{messages = []} = State) ->
    receive
        Message -> handle(Message, State)
    end;
loop(#state{messages = [M | Ms]} = State) ->
    handle(M, State#state{messages = Ms}).

handle({From, write_cache}, S) when From =:= self() ->
    case catch do_write_cache(get(log)) of
        ok -> loop(S);
        Error -> loop(S#state{cache_error = Error})
    end;
handle({From, {log, Format, B}} = Message, S) ->
    case get(log) of
        #log{mode = read_only} = L -> reply(From, {error, {read_only_mode, L#log.name}}, S);
        #log{status = ok, format = external} = L when Format =:= internal -> reply(From, format_external(L), S);
        #log{status = ok, format = LogFormat} -> log_loop(S, From, [B], [], iolist_size(B), LogFormat);
        L -> blocked(From, Message, L, S)
    end;
handle({alog, Format, B} = Message, S) ->
    case get(log) of
        #log{mode = read_only} ->
            notify_owners({read_only, B}),
            loop(S);
        #log{status = ok, format = external} when Format =:= internal ->
            notify_owners({format_external, B}),
            loop(S);
        #log{status = ok, format = LogFormat} ->
            log_loop(S, [], [B], [], iolist_size(B), LogFormat);
        #log{status = {blocked, false}} ->
            notify_owners({blocked_log, B}),
            loop(S);
        _ ->
            enqueue(Message, S)
    end;
handle({From, {block, QueueLogRecs}} = Message, S) ->
    case get(log) of
        #log{status = ok} = L ->
            do_block(From, QueueLogRecs, L),
            reply(From, ok, S);
        L ->
            blocked(From, Message, L, S)
    end;
handle({From, unblock}, S) ->
    case get(log) of
        #log{status = ok} = L -> reply(From, {error, {not_blocked, L#log.name}}, S);
        #log{blocked_by = From} = L -> reply(From, ok, do_unblock(L, S));
        L -> reply(From, {error, {not_blocked_by_pid, L#log.name}}, S)
    end;
handle({From, sync} = Message, S) ->
    case get(log) of
        #log{mode = read_only} = L -> reply(From, {error, {read_only_mode, L#log.name}}, S);
        #log{status = ok, format = LogFormat} -> log_loop(S, [], [], [From], 0, LogFormat);
        L -> blocked(From, Message, L, S)
    end;
handle({From, {truncate, Head, F, A}} = Message, S) ->
    case get(log) of
        #log{mode = read_only} = L -> reply(From, {error, {read_only_mode, L#log.name}}, S);
        #log{status = ok} when S#state.cache_error =/= ok -> loop(cache_error(S, [From]));
        #log{status = ok} = L -> truncated(From, Head, {F, A}, L, S);
        L -> blocked(From, Message, L, S)
    end;
handle({From, {chunk, Pos, B, N}} = Message, S) ->
    case get(log) of
        #log{status = ok} when S#state.cache_error =/= ok -> loop(cache_error(S, [From]));
        #log{status = ok} = L -> reply(From, do_chunk(L, Pos, B, N), S);
        #log{blocked_by = From} = L -> reply(From, do_chunk(L, Pos, B, N), S);
        L -> blocked(From, Message, L, S)
    end;
handle({From, {chunk_step, Pos, N}} = Message, S) ->
    case get(log) of
        #log{status = ok} when S#state.cache_error =/= ok -> loop(cache_error(S, [From]));
        #log{status = ok} = L -> reply(From, do_chunk_step(L, Pos, N), S);
        #log{blocked_by = From} = L -> reply(From, do_chunk_step(L, Pos, N), S);
        L -> blocked(From, Message, L, S)
    end;
handle({From, {change_notify, Pid, NewNotify}} = Message, S) ->
    case get(log) of
        #log{status = ok} = L ->
            case do_change_notify(L, Pid, NewNotify) of
                {ok, L1} ->
                    put(log, L1),
                    reply(From, ok, S);
                Error ->
                    reply(From, Error, S)
            end;
        L ->
            blocked(From, Message, L, S)
    end;
handle({From, {change_header, NewHead}} = Message, S) ->
    case get(log) of
        #log{mode = read_only} = L ->
            reply(From, {error, {read_only_mode, L#log.name}}, S);
        #log{status = ok, format = Format} = L ->
            case check_head(NewHead, Format) of
                {ok, Head} ->
                    put(log, L#log{head = mk_head(Head, Format)}),
                    reply(From, ok, S);
                Error ->
                    reply(From, Error, S)
            end;
        L ->
            blocked(From, Message, L, S)
    end;
handle({From, {change_size, NewSize}} = Message, S) ->
    case get(log) of
        #log{mode = read_only} = L -> reply(From, {error, {read_only_mode, L#log.name}}, S);
        #log{status = ok} = L -> size_changed(From, NewSize, L, S);
        L -> blocked(From, Message, L, S)
    end;
handle({From, Next} = Message, S) when Next =:= inc_wrap_file; Next =:= next_file ->
    case get(log) of
        #log{mode = read_only} = L -> reply(From, {error, {read_only_mode, L#log.name}}, S);
        #log{type = halt} = L -> reply(From, {error, {halt_log, L#log.name}}, S);
        #log{type = rotate} = L when Next =:= inc_wrap_file -> reply(From, {error, {rotate_log, L#log.name}}, S);
        #log{status = ok} when S#state.cache_error =/= ok -> loop(cache_error(S, [From]));
        #log{status = ok, type = wrap} = L -> wrapped(From, L, S);
        #log{status = ok, type = rotate} = L -> rotated(From, L, S);
        L -> blocked(From, Message, L, S)
    end;
handle({From, {reopen, NewFile, Head, F, A}}, S) ->
    case get(log) of
        #log{mode = read_only} = L -> reply(From, {error, {read_only_mode, L#log.name}}, S);
        #log{status = ok} when S#state.cache_error =/= ok -> loop(cache_error(S, [From]));
        #log{status = ok, filename = NewFile} = L -> reply(From, {error, {same_file_name, L#log.name}}, S);
        #log{status = ok} = L -> reopened(From, {NewFile, Head, F, A}, L, S);
        L -> reply(From, {error, {blocked_log, L#log.name}}, S)
    end;
handle({Server, {internal_open, A}}, S) ->
    case get(log) of
        undefined -> first_open(Server, A, S);
        L -> another_open(Server, A, L, S)
    end;
handle({From, close}, S) ->
    case do_close(From, S) of
        {stop, S1} -> do_exit(S1, From, ok, normal);
        {continue, S1} -> reply(From, ok, S1)
    end;
handle({From, info}, S) ->
    reply(From, do_info(get(log), S#state.cnt), S);
handle({'EXIT', From, Reason}, #state{parent = From} = S) ->
    % The parent orders a shutdown.
    _ = do_stop(S),
    exit(Reason);
handle({'EXIT', From, Reason}, #state{server = From} = S) ->
    _ = do_stop(S),
    exit(Reason);
handle({'EXIT', From, _Reason}, S) ->
    owner_exit(From, S);
handle({system, From, Req}, S) ->
    sys:handle_system_msg(Req, From, S#state.parent, ?MODULE, [], S);
handle(_, S) ->
    loop(S).

% A request on a log that is not open for it: refused when the log does not queue or the caller blocked it,
% queued otherwise.
blocked(From, _Message, #log{status = {blocked, false}} = L, S) -> reply(From, {error, {blocked_log, L#log.name}}, S);
blocked(From, _Message, #log{blocked_by = From} = L, S) -> reply(From, {error, {blocked_log, L#log.name}}, S);
blocked(_From, Message, _L, S) -> enqueue(Message, S).

format_external(L) -> {error, {format_external, L#log.name}}.

truncated(From, Head, {F, A}, L, S) ->
    H = merge_head(Head, L#log.head),
    case catch do_trunc(L, H) of
        ok ->
            erase(is_full),
            notify_owners({truncated, S#state.cnt}),
            N =
                case H of
                    none -> 0;
                    _ -> 1
                end,
            reply(From, ok, (state_ok(S))#state{cnt = N});
        Error ->
            do_exit(S, From, Error, ?failure(Error, F, A))
    end.

size_changed(From, NewSize, L, S) ->
    case check_size(L#log.type, NewSize) of
        ok ->
            % do_change_size/2 puts the log.
            case catch do_change_size(L, NewSize) of
                ok -> reply(From, ok, S);
                {big, CurSize} -> reply(From, {error, {new_size_too_small, L#log.name, CurSize}}, S);
                Else -> reply(From, Else, state_err(S, Else))
            end;
        not_ok ->
            reply(From, {error, {badarg, size}}, S)
    end.

wrapped(From, L, S) ->
    case catch do_inc_wrap_file(L) of
        {ok, L2, Lost} ->
            put(log, L2),
            notify_owners({L#log.type, Lost}),
            reply(From, ok, S#state{cnt = S#state.cnt - Lost});
        {error, Error, L2} ->
            put(log, L2),
            reply(From, Error, state_err(S, Error))
    end.

rotated(From, #log{extra = Handle, head = Head} = L, S) ->
    put(log, L#log{extra = clause_disk_log:do_rotate(Handle, Head)}),
    reply(From, ok, S).

% Close the files, rename them to the new name, and open a new log under the old name.
reopened(From, {NewFile, Head, F, A}, L, S) ->
    case catch close_disk_log2(L) of
        closed ->
            File = L#log.filename,
            case catch rename_file(File, NewFile, L) of
                ok -> reopen_renamed(From, {Head, F, A}, L, S);
                Error -> do_exit(S, From, Error, ?failure(Error, reopen, 2))
            end;
        Error ->
            do_exit(S, From, Error, ?failure(Error, F, A))
    end.

reopen_renamed(From, {Head, F, A}, L, S) ->
    H = merge_head(Head, L#log.head),
    Args = (S#state.args)#arg{name = L#log.name, repair = truncate, head = H, file = L#log.filename},
    case do_open(Args) of
        {ok, Res, L2, Cnt} ->
            put(log, L2#log{owners = L#log.owners, head = L#log.head, users = L#log.users}),
            notify_owners({truncated, S#state.cnt}),
            erase(is_full),
            case Res of
                {error, _} -> do_exit(S, From, Res, ?failure(Res, F, A));
                _ -> reply(From, ok, S#state{cnt = Cnt})
            end;
        Res ->
            do_exit(S, From, Res, ?failure(Res, F, A))
    end.

first_open(Server, A, S) ->
    % do_open/1 puts the log.
    case do_open(A) of
        {ok, Res, L, Cnt} ->
            put(log, opening_pid(A#arg.linkto, A#arg.notify, L)),
            #arg{size = Size, old_size = OldSize} = A,
            S1 = S#state{args = A, cnt = Cnt},
            case Size =:= OldSize of
                true ->
                    reply(Server, Res, S1);
                false ->
                    case catch do_change_size(get(log), Size) of
                        ok -> reply(Server, Res, S1);
                        % Halt logs are opened even when the given size is too small.
                        {big, _CurSize} -> reply(Server, Res, S1);
                        Else -> reply(Server, Else, S1)
                    end
            end;
        Res ->
            do_fast_exit(S, Server, Res)
    end.

another_open(Server, A, L, S) ->
    TestH = mk_head(A#arg.head, A#arg.format),
    case compare_arg(A#arg.options, S#state.args, TestH, L#log.head) of
        ok ->
            case add_pid(A#arg.linkto, A#arg.notify, L) of
                {ok, L1} ->
                    put(log, L1),
                    reply(Server, {ok, L#log.name}, S);
                Error ->
                    reply(Server, Error, S)
            end;
        Error ->
            reply(Server, Error, S)
    end.

owner_exit(From, S) ->
    L = get(log),
    case is_owner(From, L) of
        {true, _Notify} ->
            case close_owner(From, L, S) of
                {stop, S1} ->
                    _ = do_stop(S1),
                    exit(normal);
                {continue, S1} ->
                    loop(S1)
            end;
        false ->
            % users is not decremented.
            loop(do_unblock(From, get(log), S))
    end.

enqueue(Message, #state{queue = Queue} = S) -> loop(S#state{queue = [Message | Queue]}).

% Log requests that arrive together, up to 64 KiB, are written at once.
-define(MAX_LOOK_AHEAD, 64 * 1024).

log_loop(#state{cache_error = CE} = S, Pids, _Bins, _Sync, _Sz, _F) when CE =/= ok ->
    loop(cache_error(S, Pids));
log_loop(#state{} = S, Pids, Bins, Sync, Sz, _F) when Sz > ?MAX_LOOK_AHEAD ->
    loop(log_end(S, Pids, Bins, Sync, Sz));
log_loop(#state{messages = []} = S, Pids, Bins, Sync, Sz, F) ->
    receive
        Message -> log_loop(Message, Pids, Bins, Sync, Sz, F, S)
    after 0 -> loop(log_end(S, Pids, Bins, Sync, Sz))
    end;
log_loop(#state{messages = [M | Ms]} = S, Pids, Bins, Sync, Sz, F) ->
    log_loop(M, Pids, Bins, Sync, Sz, F, S#state{messages = Ms}).

% Terms are logged together on internal logs only; OTP's binary format tag never comes from the API.
log_loop({alog, internal, B}, Pids, Bins, Sync, Sz, internal = F, S) ->
    log_loop(S, Pids, [B | Bins], Sync, Sz + iolist_size(B), F);
log_loop({alog, binary, B}, Pids, Bins, Sync, Sz, F, S) ->
    log_loop(S, Pids, [B | Bins], Sync, Sz + iolist_size(B), F);
log_loop({From, {log, internal, B}}, Pids, Bins, Sync, Sz, internal = F, S) ->
    log_loop(S, [From | Pids], [B | Bins], Sync, Sz + iolist_size(B), F);
log_loop({From, {log, binary, B}}, Pids, Bins, Sync, Sz, F, S) ->
    log_loop(S, [From | Pids], [B | Bins], Sync, Sz + iolist_size(B), F);
log_loop({From, sync}, Pids, Bins, Sync, Sz, F, S) ->
    log_loop(S, Pids, Bins, [From | Sync], Sz, F);
log_loop(Message, Pids, Bins, Sync, Sz, _F, S) ->
    handle(Message, log_end(S, Pids, Bins, Sync, Sz)).

log_end(S, [], [], Sync, _Sz) ->
    log_end_sync(S, Sync);
log_end(#state{cnt = Cnt} = S, Pids, Bins, Sync, Sz) ->
    case do_log(get(log), rflat(Bins), Sz) of
        N when is_integer(N) ->
            ok = replies(Pids, ok),
            log_end_sync((state_ok(S))#state{cnt = Cnt + N}, Sync);
        {error, {error, {full, _Name}}, N} when Pids =:= [] ->
            log_end_sync(state_ok(S#state{cnt = Cnt + N}), Sync);
        {error, Error, N} ->
            ok = replies(Pids, Error),
            state_err(S#state{cnt = Cnt + N}, Error)
    end.

log_end_sync(S, []) ->
    S;
log_end_sync(S, Sync) ->
    Res = do_sync(get(log)),
    ok = replies(Sync, Res),
    state_err(S, Res).

rflat([B]) -> B;
rflat(B) -> rflat(B, []).

rflat([B | Bs], L) -> rflat(Bs, B ++ L);
rflat([], L) -> L.

do_change_notify(L, Pid, Notify) ->
    case is_owner(Pid, L) of
        {true, Notify} -> {ok, L};
        {true, _OldNotify} when Notify =/= true, Notify =/= false -> {error, {badarg, notify}};
        {true, _OldNotify} -> {ok, L#log{owners = [{Pid, Notify} | lists:keydelete(Pid, 1, L#log.owners)]}};
        false -> {error, {not_owner, Pid}}
    end.

do_close(Pid, S) ->
    L = get(log),
    case is_owner(Pid, L) of
        {true, _Notify} -> close_owner(Pid, L, S);
        false -> close_user(Pid, L, S)
    end.

close_owner(Pid, L, S) ->
    L1 = L#log{owners = lists:keydelete(Pid, 1, L#log.owners)},
    put(log, L1),
    S2 = do_unblock(Pid, get(log), S),
    unlink(Pid),
    do_close2(L1, S2).

close_user(Pid, #log{users = Users} = L, S) when Users > 0 ->
    L1 = L#log{users = Users - 1},
    put(log, L1),
    do_close2(L1, do_unblock(Pid, get(log), S));
close_user(_Pid, _L, S) ->
    {continue, S}.

do_close2(#log{users = 0, owners = []}, S) -> {stop, S};
do_close2(_L, S) -> {continue, S}.

system_continue(_Parent, _, State) -> loop(State).

system_terminate(Reason, _Parent, _, State) ->
    _ = do_stop(State),
    exit(Reason).

system_code_change(State, _Module, _OldVsn, _Extra) -> {ok, State}.

% Close the log, leave the registry, reply and exit with Reason.
do_exit(S, From, Message0, Reason) ->
    R = do_stop(S),
    Message =
        case S#state.cache_error of
            Err when Err =/= ok -> Err;
            _ when R =:= closed -> Message0;
            _ when Message0 =:= ok -> R;
            _ -> Message0
        end,
    _ = server_close(self()),
    ok = replies(From, Message),
    exit(Reason).

do_fast_exit(S, Server, Message) ->
    _ = do_stop(S),
    Server ! {disk_log, self(), Message},
    exit(normal).

do_stop(S) ->
    proc_q(S#state.queue ++ S#state.messages),
    close_disk_log(get(log)).

% Synchronous requests left unanswered get disk_log_stopped, which the caller waits past for the 'DOWN'.
proc_q([{From, _R} | Tail]) when is_pid(From) ->
    From ! {disk_log, self(), {error, disk_log_stopped}},
    proc_q(Tail);
proc_q([_ | T]) ->
    proc_q(T);
proc_q([]) ->
    ok.

opening_pid(Pid, Notify, L) ->
    {ok, L1} = add_pid(Pid, Notify, L),
    L1.

add_pid(Pid, Notify, L) when is_pid(Pid) ->
    case is_owner(Pid, L) of
        false ->
            link(Pid),
            {ok, L#log{owners = [{Pid, Notify} | L#log.owners]}};
        {true, Notify} ->
            {ok, L};
        {true, CurNotify} ->
            {error, {arg_mismatch, notify, CurNotify, Notify}}
    end;
add_pid(_NotAPid, _Notify, L) ->
    {ok, L#log{users = L#log.users + 1}}.

unblock_pid(#log{blocked_by = none}) ->
    ok;
unblock_pid(#log{blocked_by = Pid} = L) ->
    case is_owner(Pid, L) of
        {true, _Notify} -> ok;
        false -> unlink(Pid)
    end.

is_owner(Pid, L) ->
    case lists:keysearch(Pid, 1, L#log.owners) of
        {value, {_Pid, Notify}} -> {true, Notify};
        false -> false
    end.

rename_file(File, NewFile, #log{type = halt}) ->
    case file:rename(File, NewFile) of
        ok -> ok;
        Else -> file_error(NewFile, Else)
    end;
rename_file(File, NewFile, #log{type = wrap}) ->
    rename_files(wrap_file_extensions(File), File, NewFile, ok);
rename_file(File, NewFile, #log{type = rotate, extra = Handle}) ->
    {_MaxB, MaxF} = clause_disk_log:get_rotate_size(Handle),
    clause_disk_log:rotate_files(Handle#rotate_handle.file, MaxF),
    rename_files(rotate_file_extensions(File, MaxF), File, NewFile, ok).

rename_files([Ext | Exts], File, NewFile0, Res) ->
    NewFile = add_ext(NewFile0, Ext),
    NRes =
        case file:rename(add_ext(File, Ext), NewFile) of
            ok -> Res;
            Else -> file_error(NewFile, Else)
        end,
    rename_files(Exts, File, NewFile0, NRes);
rename_files([], _File, _NewFiles, Res) ->
    Res.

file_error(FileName, {error, Error}) -> {error, {file_error, FileName, Error}}.

% Options of another open/1 must equal those of the first; the file and mode give their own errors.
compare_arg([], _A, none, _OrigHead) ->
    ok;
compare_arg([], _A, Head, OrigHead) when Head =/= OrigHead -> {error, {arg_mismatch, head, OrigHead, Head}};
compare_arg([], _A, _Head, _OrigHead) ->
    ok;
compare_arg([{Attr, Val} | Tail], A, Head, OrigHead) ->
    case compare_option(Attr, Val, A) of
        {not_ok, OrigVal} -> {error, {arg_mismatch, Attr, OrigVal, Val}};
        ok -> compare_arg(Tail, A, Head, OrigHead);
        Error -> Error
    end.

compare_option(file, F, A) when F =/= A#arg.file -> {error, {name_already_open, A#arg.name}};
compare_option(mode, read_only, #arg{mode = read_write} = A) -> {error, {open_read_write, A#arg.name}};
compare_option(mode, read_write, #arg{mode = read_only} = A) -> {error, {open_read_only, A#arg.name}};
compare_option(type, T, A) when T =/= A#arg.type -> {not_ok, A#arg.type};
compare_option(format, F, A) when F =/= A#arg.format -> {not_ok, A#arg.format};
compare_option(repair, R, A) when R =/= A#arg.repair -> {not_ok, A#arg.repair};
% A size that does not match an open halt log's is ignored.
compare_option(size, Sz, #arg{type = wrap} = A) when Sz =/= A#arg.size -> {error, {size_mismatch, A#arg.size, Sz}};
compare_option(_Attr, _Val, _A) -> ok.

do_open(#arg{type = rotate} = A) ->
    #arg{name = Name, size = Size, mode = Mode, head = Head0, format = Format, type = Type, file = FName} = A,
    Head = mk_head(Head0, Format),
    case clause_disk_log:open_rotate_log_file(FName, Size, Head) of
        {ok, RotHandle} ->
            L = #log{
                name = Name,
                type = Type,
                format = Format,
                filename = FName,
                size = Size,
                mode = Mode,
                extra = RotHandle,
                format_type = rotate_ext,
                head = Head
            },
            {ok, {ok, Name}, L, 0};
        Error ->
            Error
    end;
do_open(A) ->
    #arg{
        type = Type,
        format = Format,
        name = Name,
        head = Head0,
        file = FName,
        repair = Repair,
        size = Size,
        mode = Mode,
        quiet = Quiet,
        version = V,
        old_size = OldSize
    } = A,
    clause_disk_log:set_quiet(Quiet),
    Head = mk_head(Head0, Format),
    case do_open2({Type, Format}, Name, FName, {Repair, OldSize, Mode, Head, V}) of
        {ok, Ret, Extra, FormatType, NoItems} ->
            L = #log{
                name = Name,
                type = Type,
                format = Format,
                filename = FName,
                size = Size,
                format_type = FormatType,
                head = Head,
                mode = Mode,
                version = V,
                extra = Extra
            },
            {ok, Ret, L, NoItems};
        Error ->
            Error
    end.

mk_head({head, Term}, internal) -> {ok, term_to_binary(Term)};
mk_head({head, Bytes}, external) -> {ok, ensure_binary(Bytes)};
mk_head(H, _) -> H.

ensure_binary_list(Bs) ->
    case lists:all(fun erlang:is_binary/1, Bs) of
        true -> Bs;
        false -> [ensure_binary(B) || B <- Bs]
    end.

ensure_binary(Bytes) -> iolist_to_binary(Bytes).

do_change_size(#log{type = halt, extra = #halt{curB = CurB} = Halt} = L, NewSize) when
    NewSize =:= infinity; CurB =< NewSize
->
    erase(is_full),
    put(log, L#log{extra = Halt#halt{size = NewSize}}),
    ok;
do_change_size(#log{type = halt, extra = #halt{curB = CurB}}, _NewSize) ->
    {big, CurB};
do_change_size(#log{type = wrap, extra = Extra, version = Version} = L, NewSize) ->
    {ok, Handle} = clause_disk_log:change_size_wrap(Extra, NewSize, Version),
    erase(is_full),
    put(log, L#log{extra = Handle}),
    ok;
do_change_size(#log{type = rotate, extra = Extra} = L, NewSize) ->
    {ok, Handle} = clause_disk_log:change_size_rotate(Extra, NewSize),
    erase(is_full),
    put(log, L#log{extra = Handle}),
    ok.

check_head({head, none}, _Format) ->
    {ok, none};
check_head({head_func, {M, F, A}}, _Format) when is_atom(M), is_atom(F), is_list(A) ->
    {ok, {M, F, A}};
check_head({head, Head}, external) ->
    try ensure_binary(Head) of
        _ -> {ok, {head, Head}}
    catch
        _:_ -> {error, {badarg, head}}
    end;
check_head({head, Term}, internal) ->
    {ok, {head, Term}};
check_head(_Head, _Format) ->
    {error, {badarg, head}}.

check_size(halt, NewSize) when is_integer(NewSize), NewSize > 0 ->
    ok;
check_size(halt, infinity) ->
    ok;
check_size(Type, {NewMaxB, NewMaxF}) when
    (Type =:= wrap orelse Type =:= rotate) andalso
        is_integer(NewMaxB) andalso is_integer(NewMaxF) andalso
        NewMaxB > 0 andalso NewMaxB =< ?MAX_BYTES andalso NewMaxF > 0 andalso NewMaxF < ?MAX_FILES
->
    ok;
check_size(_, _) ->
    not_ok.

do_inc_wrap_file(#log{format = Format, extra = Handle, head = Head} = L) ->
    Inc =
        case Format of
            internal -> clause_disk_log:mf_int_inc(Handle, Head);
            external -> clause_disk_log:mf_ext_inc(Handle, Head)
        end,
    case Inc of
        {ok, Handle2, Lost} -> {ok, L#log{extra = Handle2}, Lost};
        {error, Error, Handle2} -> {error, Error, L#log{extra = Handle2}}
    end.

do_open2({halt, internal}, Name, FName, {Repair, Size, Mode, Head, _V}) ->
    case catch clause_disk_log:int_open(FName, Repair, Mode, Head) of
        {ok, {_Alloc, FdC, {NoItems, _NoBytes}, FileSize}} ->
            {ok, {ok, Name}, #halt{fdc = FdC, curB = FileSize, size = Size}, halt_int, NoItems};
        {repaired, FdC, Rec, Bad, FileSize} ->
            Halt = #halt{fdc = FdC, curB = FileSize, size = Size},
            {ok, {repaired, Name, {recovered, Rec}, {badbytes, Bad}}, Halt, halt_int, Rec};
        Error ->
            Error
    end;
do_open2({wrap, internal}, Name, FName, {Repair, {MaxB, MaxF}, Mode, Head, V}) ->
    case catch clause_disk_log:mf_int_open(FName, MaxB, MaxF, Repair, Mode, Head, V) of
        {ok, Handle, Cnt} ->
            {ok, {ok, Name}, Handle, wrap_int, Cnt};
        {repaired, Handle, Rec, Bad, Cnt} ->
            {ok, {repaired, Name, {recovered, Rec}, {badbytes, Bad}}, Handle, wrap_int, Cnt};
        Error ->
            Error
    end;
do_open2({halt, external}, Name, FName, {Repair, Size, Mode, Head, _V}) ->
    case catch clause_disk_log:ext_open(FName, Repair, Mode, Head) of
        {ok, {_Alloc, FdC, {NoItems, _NoBytes}, FileSize}} ->
            {ok, {ok, Name}, #halt{fdc = FdC, curB = FileSize, size = Size}, halt_ext, NoItems};
        Error ->
            Error
    end;
do_open2({wrap, external}, Name, FName, {Repair, {MaxB, MaxF}, Mode, Head, V}) ->
    case catch clause_disk_log:mf_ext_open(FName, MaxB, MaxF, Repair, Mode, Head, V) of
        {ok, Handle, Cnt} -> {ok, {ok, Name}, Handle, wrap_ext, Cnt};
        Error -> Error
    end.

close_disk_log(undefined) ->
    closed;
close_disk_log(L) ->
    unblock_pid(L),
    lists:foreach(fun({Pid, _}) -> unlink(Pid) end, L#log.owners),
    R = (catch close_disk_log2(L)),
    erase(log),
    R.

close_disk_log2(L) ->
    case L of
        #log{format_type = halt_int, mode = Mode, extra = Halt} ->
            clause_disk_log:close(Halt#halt.fdc, L#log.filename, Mode);
        #log{format_type = wrap_int, mode = Mode, extra = Handle} ->
            clause_disk_log:mf_int_close(Handle, Mode);
        #log{format_type = halt_ext, extra = Halt} ->
            clause_disk_log:fclose(Halt#halt.fdc, L#log.filename);
        #log{format_type = wrap_ext, mode = Mode, extra = Handle} ->
            clause_disk_log:mf_ext_close(Handle, Mode);
        #log{type = rotate, extra = Handle} ->
            clause_disk_log:rotate_ext_close(Handle)
    end,
    closed.

do_format_error({error, Module, Error}) ->
    Module:format_error(Error);
do_format_error({error, Reason}) ->
    do_format_error(Reason);
do_format_error({Node, Error = {error, _Reason}}) ->
    lists:append(io_lib:format("~p: ", [Node]), do_format_error(Error));
do_format_error({badarg, Arg}) ->
    io_lib:format("The argument ~p is missing, not recognized or not wellformed~n", [Arg]);
do_format_error({size_mismatch, OldSize, ArgSize}) ->
    io_lib:format("The given size ~p does not match the size ~p found on the disk log size file~n", [ArgSize, OldSize]);
do_format_error({read_only_mode, Log}) ->
    io_lib:format(
        "The disk log ~tp has been opened read-only, but the requested operation needs read-write access~n", [Log]
    );
do_format_error({format_external, Log}) ->
    io_lib:format(
        "The requested operation can only be applied on internally formatted disk logs, but ~tp is externally "
        "formatted~n",
        [Log]
    );
do_format_error({blocked_log, Log}) ->
    io_lib:format(
        "The blocked disk log ~tp does not queue requests, or the log has been blocked by the calling process~n", [Log]
    );
do_format_error({full, Log}) ->
    io_lib:format("The halt log ~tp is full~n", [Log]);
do_format_error({not_blocked, Log}) ->
    io_lib:format("The disk log ~tp is not blocked~n", [Log]);
do_format_error({not_owner, Pid}) ->
    io_lib:format("The pid ~tp is not an owner of the disk log~n", [Pid]);
do_format_error({not_blocked_by_pid, Log}) ->
    io_lib:format("The disk log ~tp is blocked, but only the blocking pid can unblock a disk log~n", [Log]);
do_format_error({new_size_too_small, Log, CurrentSize}) ->
    io_lib:format("The current size ~p of the halt log ~tp is greater than the requested new size~n", [
        CurrentSize, Log
    ]);
do_format_error({halt_log, Log}) ->
    io_lib:format("The halt log ~tp cannot be wrapped~n", [Log]);
do_format_error({same_file_name, Log}) ->
    io_lib:format("Current and new file name of the disk log ~tp are the same~n", [Log]);
do_format_error({arg_mismatch, Option, FirstValue, ArgValue}) ->
    io_lib:format("The value ~tp of the disk log option ~p does not match the current value ~tp~n", [
        ArgValue, Option, FirstValue
    ]);
do_format_error({name_already_open, Log}) ->
    io_lib:format("The disk log ~tp has already opened another file~n", [Log]);
do_format_error({open_read_write, Log}) ->
    io_lib:format("The disk log ~tp has already been opened read-write~n", [Log]);
do_format_error({open_read_only, Log}) ->
    io_lib:format("The disk log ~tp has already been opened read-only~n", [Log]);
do_format_error({not_internal_wrap, Log}) ->
    io_lib:format("The requested operation cannot be applied since ~tp is not an internally formatted disk log~n", [
        Log
    ]);
do_format_error(no_such_log) ->
    io_lib:format("There is no disk log with the given name~n", []);
do_format_error(Down) when Down =:= nonode; Down =:= nodedown ->
    io_lib:format("There seems to be no node up that can handle the request~n", []);
do_format_error({corrupt_log_file, FileName}) ->
    io_lib:format("The disk log file \"~ts\" contains corrupt data~n", [FileName]);
do_format_error({need_repair, FileName}) ->
    io_lib:format("The disk log file \"~ts\" has not been closed properly and needs repair~n", [FileName]);
do_format_error({not_a_log_file, FileName}) ->
    io_lib:format("The file \"~ts\" is not a wrap log file~n", [FileName]);
do_format_error({invalid_header, InvalidHeader}) ->
    io_lib:format("The disk log header is not wellformed: ~p~n", [InvalidHeader]);
do_format_error(end_of_log) ->
    io_lib:format("An attempt was made to step outside a not yet full wrap log~n", []);
do_format_error({invalid_index_file, FileName}) ->
    io_lib:format("The wrap log index file \"~ts\" cannot be used~n", [FileName]);
do_format_error({no_continuation, BadCont}) ->
    io_lib:format("The term ~p is not a chunk continuation~n", [BadCont]);
do_format_error({file_error, FileName, Reason}) ->
    io_lib:format("\"~ts\": ~tp~n", [FileName, file:format_error(Reason)]);
do_format_error(E) ->
    io_lib:format("~tp~n", [E]).

% The info/1 list; a wrap log's overflow count since the last info/1 starts over.
do_info(L, Cnt) ->
    #log{
        name = Name,
        type = Type,
        mode = Mode,
        filename = File,
        extra = Extra,
        status = Status,
        owners = Owners,
        users = Users,
        format = Format,
        head = Head
    } = L,
    Size =
        case Type of
            wrap -> clause_disk_log:get_wrap_size(Extra);
            rotate -> clause_disk_log:get_rotate_size(Extra);
            halt -> Extra#halt.size
        end,
    HeadL =
        case Mode of
            read_write -> [{head, info_head(Head)}];
            read_only -> []
        end,
    Common =
        [
            {name, Name},
            {file, File},
            {type, Type},
            {format, Format},
            {size, Size},
            % Kept for backward compatibility (undocumented), as OTP.
            {items, Cnt},
            {owners, Owners},
            {users, Users}
        ] ++ HeadL ++ [{mode, Mode}, {status, Status}, {node, node()}],
    Common ++ info_counts(Type, Mode, L, Cnt).

info_head({ok, H}) -> H;
info_head(Head) -> Head.

info_counts(wrap, read_write, #log{extra = Extra} = L, Cnt) ->
    #handle{curB = CurB, curF = CurF, cur_cnt = CurCnt, acc_cnt = AccCnt, noFull = NoFull, accFull = AccFull} = Extra,
    NewAccFull = AccFull + NoFull,
    put(log, L#log{extra = Extra#handle{noFull = 0, accFull = NewAccFull}}),
    [
        {no_current_bytes, CurB},
        {no_current_items, CurCnt},
        {no_items, Cnt},
        {no_written_items, CurCnt + AccCnt},
        {current_file, CurF},
        {no_overflows, {NewAccFull, NoFull}}
    ];
info_counts(rotate, read_write, #log{extra = #rotate_handle{curB = CurB}}, Cnt) ->
    [{no_current_bytes, CurB}, {no_items, Cnt}];
info_counts(halt, read_write, _L, Cnt) ->
    [{full, get(is_full) =/= undefined}, {no_written_items, Cnt}];
info_counts(_, read_only, _L, _Cnt) ->
    [].

do_block(Pid, QueueLogRecs, L) ->
    L2 = L#log{status = {blocked, QueueLogRecs}, blocked_by = Pid},
    put(log, L2),
    case is_owner(Pid, L2) of
        {true, _Notify} -> ok;
        false -> link(Pid)
    end.

do_unblock(Pid, #log{blocked_by = Pid} = L, S) -> do_unblock(L, S);
do_unblock(_Pid, _L, S) -> S.

% The queued requests are handled next, in order: a block request is synchronous, so every request in messages was
% queued before the unblock came.
do_unblock(L, S) ->
    unblock_pid(L),
    put(log, L#log{blocked_by = none, status = ok}),
    [] = S#state.messages,
    S#state{queue = [], messages = lists:reverse(S#state.queue)}.

do_log(L, B) -> do_log(L, B, iolist_size(B)).

do_log(#log{type = halt, format = Format, extra = #halt{curB = CurSize, size = Sz} = Halt} = L, B, BSz) ->
    {Bs, BSize} = logl(B, Format, BSz),
    case get(is_full) of
        true -> {error, {error, {full, L#log.name}}, 0};
        undefined when Sz =:= infinity; CurSize + BSize =< Sz -> halt_write(Halt, L, B, Bs, BSize);
        undefined -> halt_write_full(L, B, Format, 0)
    end;
do_log(#log{format_type = wrap_int} = L, B, _BSz) ->
    wrap_logged(clause_disk_log:mf_int_log(L#log.extra, B, L#log.head), L);
do_log(#log{format_type = wrap_ext} = L, B, _BSz) ->
    wrap_logged(clause_disk_log:mf_ext_log(L#log.extra, B, L#log.head), L);
do_log(#log{type = rotate} = L, B, _BSz) ->
    {ok, Handle, Logged} = clause_disk_log:rotate_ext_log(L#log.extra, B, L#log.head),
    put(log, L#log{extra = Handle}),
    Logged.

wrap_logged({ok, Handle, Logged, Lost, Wraps}, L) ->
    [notify_owners({wrap, N}) || N <- Wraps],
    put(log, L#log{extra = Handle}),
    Logged - Lost;
wrap_logged({ok, Handle, Logged}, L) ->
    put(log, L#log{extra = Handle}),
    Logged;
wrap_logged({error, Error, Handle, Logged, Lost}, L) ->
    put(log, L#log{extra = Handle}),
    {error, Error, Logged - Lost}.

logl(B, external, undefined) -> {B, iolist_size(B)};
logl(B, external, Sz) -> {B, Sz};
logl(B, internal, _Sz) -> clause_disk_log:logl(B).

% Write item by item while they fit; then the log is full.
halt_write_full(L, [Bin | Bins], Format, N) ->
    B = [Bin],
    {Bs, BSize} = logl(B, Format, undefined),
    #halt{curB = CurSize, size = Sz} = Halt = L#log.extra,
    case CurSize + BSize =< Sz of
        true ->
            case halt_write(Halt, L, B, Bs, BSize) of
                N1 when is_integer(N1) -> halt_write_full(get(log), Bins, Format, N + N1);
                Error -> Error
            end;
        false ->
            halt_write_full(L, [], Format, N)
    end;
halt_write_full(L, _Bs, _Format, N) ->
    put(is_full, true),
    notify_owners(full),
    {error, {error, {full, L#log.name}}, N}.

halt_write(Halt, L, B, Bs, BSize) ->
    case clause_disk_log:fwrite(Halt#halt.fdc, L#log.filename, Bs, BSize) of
        {ok, NewFdC} ->
            put(log, L#log{extra = Halt#halt{fdc = NewFdC, curB = Halt#halt.curB + BSize}}),
            length(B);
        {Error, NewFdC} ->
            put(log, L#log{extra = Halt#halt{fdc = NewFdC}}),
            {error, Error, 0}
    end.

do_write_cache(#log{filename = FName, type = halt, extra = Halt} = Log) ->
    {Reply, NewFdC} = clause_disk_log:write_cache(Halt#halt.fdc, FName),
    put(log, Log#log{extra = Halt#halt{fdc = NewFdC}}),
    Reply;
do_write_cache(#log{type = wrap, extra = Handle} = Log) ->
    {Reply, NewHandle} = clause_disk_log:mf_write_cache(Handle),
    put(log, Log#log{extra = NewHandle}),
    Reply;
do_write_cache(#log{type = rotate, extra = Handle} = Log) ->
    {Reply, NewHandle} = clause_disk_log:rotate_write_cache(Handle),
    put(log, Log#log{extra = NewHandle}),
    Reply.

do_sync(#log{filename = FName, type = halt, extra = Halt} = Log) ->
    {Reply, NewFdC} = clause_disk_log:sync(Halt#halt.fdc, FName),
    put(log, Log#log{extra = Halt#halt{fdc = NewFdC}}),
    Reply;
do_sync(#log{type = Type, extra = Handle} = Log) when Type =:= wrap; Type =:= rotate ->
    {Reply, NewHandle} = clause_disk_log:mf_sync(Handle),
    put(log, Log#log{extra = NewHandle}),
    Reply.

do_trunc(#log{type = halt, filename = FName, extra = Halt, format = Format} = L, Head) ->
    {Reply1, FdC2} = halt_truncated(Format, Halt#halt.fdc, FName, Head),
    {Reply, NewHalt} =
        case clause_disk_log:position(FdC2, FName, cur) of
            {ok, NewFdC, FileSize} when Reply1 =:= ok -> {ok, Halt#halt{fdc = NewFdC, curB = FileSize}};
            {Reply2, NewFdC} -> {Reply2, Halt#halt{fdc = NewFdC}};
            {ok, NewFdC, _} -> {Reply1, Halt#halt{fdc = NewFdC}}
        end,
    put(log, L#log{extra = NewHalt}),
    Reply;
do_trunc(#log{type = wrap, extra = Handle, head = OldHead} = L, Head) ->
    {MaxB, MaxF} = clause_disk_log:get_wrap_size(Handle),
    ok = do_change_size(L, {MaxB, 1}),
    NewLog = trunc_wrap((get(log))#log{head = Head}),
    % Only to remove all files with a suffix above 1.
    NewLog2 = trunc_wrap(NewLog),
    NewHandle = (NewLog2#log.extra)#handle{noFull = 0, accFull = 0},
    do_change_size(NewLog2#log{extra = NewHandle, head = OldHead}, {MaxB, MaxF});
do_trunc(#log{type = rotate, head = Head, extra = Handle} = L, none) ->
    Handle1 = clause_disk_log:do_rotate(Handle, Head),
    clause_disk_log:remove_files(rotate, Handle1#rotate_handle.file, 0, Handle1#rotate_handle.maxF),
    put(log, L#log{extra = Handle1}),
    ok.

halt_truncated(internal, FdC, FName, Head) ->
    clause_disk_log:truncate(FdC, FName, Head);
halt_truncated(external, FdC, FName, Head) ->
    case clause_disk_log:truncate_at(FdC, FName, bof) of
        {ok, NFdC} when Head =:= none ->
            {ok, NFdC};
        {ok, NFdC} ->
            {ok, H} = Head,
            clause_disk_log:fwrite(NFdC, FName, H, byte_size(H));
        R ->
            R
    end.

trunc_wrap(L) ->
    case do_inc_wrap_file(L) of
        {ok, L2, _Lost} -> L2;
        {error, Error, _L2} -> throw(Error)
    end.

do_chunk(#log{format_type = halt_int, extra = Halt, mode = Mode, filename = FName} = L, Pos, B, N) ->
    {NewFdC, Reply} =
        case Mode of
            read_only -> clause_disk_log:chunk_read_only(Halt#halt.fdc, FName, Pos, B, N);
            read_write -> clause_disk_log:chunk(Halt#halt.fdc, FName, Pos, B, N)
        end,
    put(log, L#log{extra = Halt#halt{fdc = NewFdC}}),
    Reply;
do_chunk(#log{format_type = wrap_int, mode = read_only, extra = Handle} = Log, Pos, B, N) ->
    {NewHandle, Reply} = clause_disk_log:mf_int_chunk_read_only(Handle, Pos, B, N),
    put(log, Log#log{extra = NewHandle}),
    Reply;
do_chunk(#log{format_type = wrap_int, extra = Handle} = Log, Pos, B, N) ->
    {NewHandle, Reply} = clause_disk_log:mf_int_chunk(Handle, Pos, B, N),
    put(log, Log#log{extra = NewHandle}),
    Reply;
do_chunk(Log, _Pos, _B, _) ->
    {error, {format_external, Log#log.name}}.

do_chunk_step(#log{format_type = wrap_int, extra = Handle}, Pos, N) ->
    clause_disk_log:mf_int_chunk_step(Handle, Pos, N);
do_chunk_step(Log, _Pos, _N) ->
    {error, {not_internal_wrap, Log#log.name}}.

replies(Pid, Reply) when is_pid(Pid) ->
    Pid ! {disk_log, self(), Reply},
    ok;
replies(Pids, Reply) ->
    [Pid ! {disk_log, self(), Reply} || Pid <- Pids],
    ok.

reply(To, Reply, S) ->
    To ! {disk_log, self(), Reply},
    loop(S).

req(Log, R) ->
    case get_log_pid(Log) of
        undefined -> {error, no_such_log};
        Pid -> monitor_request(Pid, R)
    end.

% A request answered by {disk_log, Pid, Reply}; a log that stops meanwhile answers no_such_log.
monitor_request(Pid, Req) ->
    Ref = erlang:monitor(process, Pid),
    Pid ! {self(), Req},
    receive
        {'DOWN', Ref, process, Pid, _Info} ->
            {error, no_such_log};
        {disk_log, Pid, Reply} when not is_tuple(Reply) orelse element(2, Reply) =/= disk_log_stopped ->
            erlang:demonitor(Ref, [flush]),
            Reply
    end.

merge_head(none, Head) -> Head;
merge_head(Head, _) -> Head.

wrap_file_extensions(File) ->
    {_CurF, _CurFSz, _TotSz, NoOfFiles} = clause_disk_log:read_index_file(File),
    Exts = ["idx", "siz" | lists:seq(1, NoOfFiles)],
    [Ext || Ext <- Exts, element(1, file:read_file_info(add_ext(File, Ext))) =:= ok].

rotate_file_extensions(File, MaxF) ->
    Exts = [integer_to_list(N) ++ ".gz" || N <- lists:seq(0, MaxF - 1)],
    lists:reverse([Ext || Ext <- Exts, element(1, file:read_file_info(add_ext(File, Ext))) =:= ok]).

add_ext(File, Ext) -> lists:concat([File, ".", Ext]).

notify(Log, R) ->
    case get_log_pid(Log) of
        undefined ->
            {error, no_such_log};
        Pid ->
            Pid ! R,
            ok
    end.

notify_owners(Note) ->
    L = get(log),
    Msg = {disk_log, node(), L#log.name, Note},
    [Pid ! Msg || {Pid, true} <- L#log.owners],
    ok.

cache_error(#state{cache_error = Error} = S, Pids) ->
    ok = replies(Pids, Error),
    state_err(S#state{cache_error = ok}, Error).

state_ok(S) -> state_err(S, ok).

state_err(S, Err) when S#state.error_status =:= Err -> S;
state_err(S, Err) ->
    notify_owners({error_status, Err}),
    S#state{error_status = Err}.

% --- The registry ---

server_open({ok, A}) ->
    start(),
    clause_disk_log_server:open(A);
server_open(Other) ->
    Other.

server_close(Pid) -> clause_disk_log_server:close(Pid).

get_log_pid(LogName) ->
    try ets:lookup(?DISK_LOG_NAME_TABLE, LogName) of
        [{LogName, Pid}] -> Pid;
        [] -> undefined
    catch
        _:_ -> undefined
    end.
