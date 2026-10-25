%% The registry of open disk logs (plan 21 step LIB52), as OTP's disk_log_server: a gen_server registered as
%% disk_log_server, started at the first use, that owns the named tables disk_log_names (name to pid) and
%% disk_log_pids (pid to name), starts a log process for the first open of a name and passes later opens to it. An
%% open is answered by the log process in a helper process, so the server goes on meanwhile; opens of a name being
%% opened wait behind the first.
-module(clause_disk_log_server).
-behaviour(gen_server).
-export([close/1, open/1, start/0]).
-export([handle_call/3, handle_cast/2, handle_info/2, init/1, terminate/2]).

-include("clause_disk_log.hrl").

% An open being answered: the log, its process, the request, the caller, whether it attaches to a running log, and
% the opens of the same name waiting behind it.
-record(pending, {log, pid, req, from, attach, clients}).

start() ->
    case whereis(disk_log_server) of
        undefined ->
            case gen_server:start({local, disk_log_server}, ?MODULE, [], []) of
                {ok, _} -> ok;
                {error, {already_started, _}} -> ok
            end;
        _ ->
            ok
    end.

open(A) -> gen_server:call(disk_log_server, {open, A}, infinity).

close(Pid) -> gen_server:call(disk_log_server, {close, Pid}, infinity).

init([]) ->
    process_flag(trap_exit, true),
    _ = ets:new(?DISK_LOG_NAME_TABLE, [named_table, set]),
    _ = ets:new(?DISK_LOG_PID_TABLE, [named_table, set]),
    {ok, []}.

handle_call({open, A}, From, Pending) ->
    {noreply, opens([{{open, A}, From}], Pending)};
handle_call({close, Pid}, _From, Pending) ->
    case get(Pid) of
        undefined ->
            ok;
        Name ->
            erase_log(Name, Pid),
            unlink(Pid)
    end,
    {reply, ok, Pending}.

handle_cast(_, Pending) -> {noreply, Pending}.

handle_info({pending_reply, Pid, Result}, Pending) ->
    {value, #pending{log = Name, from = From, req = Request, attach = Attach, clients = Clients}} =
        lists:keysearch(Pid, #pending.pid, Pending),
    Rest = lists:keydelete(Pid, #pending.pid, Pending),
    case Attach andalso Result =:= {error, no_such_log} of
        % The log process ended meanwhile: try again.
        true ->
            {noreply, opens([{Request, From} | Clients], Rest)};
        false ->
            registered(Attach, Result, Pid, Name),
            gen_server:reply(From, Result),
            {noreply, opens(Clients, Rest)}
    end;
handle_info({'EXIT', Pid, _Reason}, Pending) ->
    % Opens waiting to attach to this log get {pending_reply, Pid, {error, no_such_log}} soon.
    case get(Pid) of
        undefined -> ok;
        Name -> erase_log(Name, Pid)
    end,
    {noreply, Pending};
handle_info(_, Pending) ->
    {noreply, Pending}.

terminate(_Reason, _) -> ok.

% A first open that succeeded registers the log.
registered(false, Result, Pid, Name) when element(1, Result) =/= error ->
    put(Pid, Name),
    link(Pid),
    ets:insert(?DISK_LOG_PID_TABLE, {Pid, Name}),
    ets:insert(?DISK_LOG_NAME_TABLE, {Name, Pid});
registered(_, _, _, _) ->
    ok.

opens([{{open, #arg{name = Name} = A} = Req, From} | L], Pending) ->
    Next =
        case lists:keysearch(Name, #pending.log, Pending) of
            {value, #pending{clients = Clients} = P} ->
                lists:keyreplace(Name, #pending.log, Pending, P#pending{clients = Clients ++ [{Req, From}]});
            false ->
                case log_pid(Name) of
                    undefined -> start_log(Name, A, From, Pending);
                    Pid -> internal_opening(Name, Pid, {From, Req, true}, Pending)
                end
        end,
    opens(L, Next);
opens([], Pending) ->
    Pending.

start_log(Name, A, From, Pending) ->
    {ok, Pid} = disk_log:istart_link(self()),
    internal_opening(Name, Pid, {From, {open, A}, false}, Pending).

internal_opening(Name, Pid, {From, {open, A} = Req, Attach}, Pending) ->
    Server = self(),
    _ = spawn(fun() -> Server ! {pending_reply, Pid, disk_log:internal_open(Pid, A)} end),
    [#pending{log = Name, pid = Pid, req = Req, from = From, attach = Attach, clients = []} | Pending].

log_pid(Name) ->
    case ets:lookup(?DISK_LOG_NAME_TABLE, Name) of
        [{Name, Pid}] -> Pid;
        [] -> undefined
    end.

erase_log(Name, Pid) ->
    case log_pid(Name) of
        Pid ->
            true = ets:delete(?DISK_LOG_NAME_TABLE, Name),
            true = ets:delete(?DISK_LOG_PID_TABLE, Pid);
        _ ->
            ok
    end,
    erase(Pid).
