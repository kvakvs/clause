%% Clause's log_mf_h (plan 21 step LIB23, docs/library.md): a gen_event handler writing every accepted event to
%% wrap files 1..MaxFiles of a directory as a 2-byte size and the external term of {LocalTime, Event}. The file
%% `index` holds the number of the file written last, so a restarted handler continues with the next one.
-module(log_mf_h).
-behaviour(gen_event).
-export([init/3, init/4]).
-export([init/1, handle_event/2, handle_info/2, terminate/2, handle_call/2, code_change/3]).

-include("log_mf_h.types.hrl").

%% Created for LIB23: the directory, the size limit of a file and the number of files, the bytes written to the
%% current file, its number and descriptor, and the predicate that selects the events to write.
-record(state, {dir, max_bytes, max_files, bytes, file, fd, pred}).

init(Dir, MaxBytes, MaxFiles) -> init(Dir, MaxBytes, MaxFiles, fun(_) -> true end).

init(Dir, MaxBytes, MaxFiles, Pred) -> {Dir, MaxBytes, MaxFiles, Pred}.

init({Dir, MaxBytes, MaxFiles, Pred}) when is_integer(MaxFiles), MaxFiles > 0, MaxFiles < 256 ->
    First =
        case read_index(Dir) of
            {ok, Last} -> next(Last, MaxFiles);
            error -> 1
        end,
    try open(Dir, First) of
        Fd ->
            {ok, #state{
                dir = Dir, max_bytes = MaxBytes, max_files = MaxFiles, bytes = 0, file = First, fd = Fd, pred = Pred
            }}
    catch
        exit:Reason -> {'EXIT', Reason}
    end.

handle_event(Event, #state{pred = Pred} = State) ->
    case catch Pred(Event) of
        true -> {ok, write(term_to_binary({erlang:localtime(), Event}), State)};
        _ -> {ok, State}
    end.

%% Moves to the next file when this one would reach its limit.
write(Bin, #state{bytes = Bytes, max_bytes = MaxBytes} = State0) ->
    Size = byte_size(Bin),
    State =
        case Bytes + Size < MaxBytes of
            true -> State0;
            false -> wrap(State0)
        end,
    case file:write(State#state.fd, [<<Size:16>>, Bin]) of
        ok -> ok;
        {error, Reason} -> exit({file_exit, Reason})
    end,
    State#state{bytes = State#state.bytes + Size + 2}.

wrap(#state{dir = Dir, fd = Fd, file = File, max_files = MaxFiles} = State) ->
    ok = file:close(Fd),
    Next = next(File, MaxFiles),
    State#state{fd = open(Dir, Next), file = Next, bytes = 0}.

handle_info({emulator, _, _} = Event, State) -> handle_event(Event, State);
handle_info(_, State) -> {ok, State}.

terminate(_, #state{fd = Fd} = State) ->
    ok = file:close(Fd),
    State.

handle_call(null, State) -> {ok, null, State}.

code_change(_, State, _) -> {ok, State}.

open(Dir, Number) ->
    case file:open(filename:join(Dir, integer_to_list(Number)), [raw, write, binary]) of
        {ok, Fd} ->
            write_index(Dir, Number),
            Fd;
        _ ->
            exit(file_open)
    end.

read_index(Dir) ->
    case file:read_file(filename:join(Dir, "index")) of
        {ok, <<Index, _/binary>>} -> {ok, Index};
        _ -> error
    end.

%% Replaces the index through a temporary file.
write_index(Dir, Index) ->
    File = filename:join(Dir, "index"),
    Temporary = File ++ ".tmp",
    case file:write_file(Temporary, <<Index>>) of
        ok -> ok = file:rename(Temporary, File);
        _ -> exit(write_index_file)
    end.

next(N, Max) when N < Max -> N + 1;
next(_, _) -> 1.
