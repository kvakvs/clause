%% mnesia's default event handler, installed in the mnesia_event manager when the event_module parameter is
%% mnesia_event (plan 21 step MIM1C, docs/libraries/mnesia.md#events): it reports mnesia's system events as OTP's
%% handler does, info reports printed and warnings and errors logged. The manager also subscribes to table events at
%% the debug levels debug and trace (MIM1E), which arrive as messages and are printed.
-module(clause_mnesia_event).
-behaviour(gen_event).
-export([add_handler/0, handler/0]).
-export([init/1, handle_event/2, handle_call/2, handle_info/2]).

% The event_module parameter's handler joins the manager.
add_handler() ->
    gen_event:add_handler(mnesia_event, handler(), []).

% The handler module: this module for the default mnesia_event.
handler() ->
    case clause_mnesia_monitor:get_env(event_module) of
        mnesia_event -> ?MODULE;
        Module -> Module
    end.

init(_) ->
    {ok, []}.

handle_event(Event, State) ->
    handle_any_event(Event),
    {ok, State}.

handle_info(Message, State) ->
    handle_any_event(Message),
    {ok, State}.

handle_call(Event, State) ->
    handle_any_event(Event),
    {ok, ok, State}.

handle_any_event({mnesia_system_event, Event}) ->
    system_event(Event);
handle_any_event({mnesia_table_event, {Op, Record, Tid}}) ->
    report_info("~p performed by ~p on record:~n\t~0tp~n", [Op, Tid, Record]);
handle_any_event(Message) ->
    report_error("~p got unexpected event: ~tp~n", [mnesia_event, Message]).

% Each system event's report; mnesia's comings and goings and checkpoint activations need none on one node.
system_event({mnesia_info, Format, Args} = Event) ->
    % An info report printed once when the same report comes again in a row.
    case put(last, Event) of
        Event -> ok;
        _ -> report_info(Format, Args)
    end;
system_event({mnesia_user, Event}) ->
    report_info("User event: ~tp~n", [Event]);
system_event({mnesia_warning, Format, Args}) ->
    report_warning(Format, Args);
system_event({mnesia_overload, Details}) ->
    report_warning("Mnesia is overloaded: ~tw~n", [Details]);
system_event({mnesia_error, Format, Args}) ->
    report_error(Format, Args);
system_event({mnesia_fatal, Format, Args, _Core}) ->
    report_error("(ignoring core) ** FATAL ** " ++ Format, Args);
system_event({inconsistent_database, Reason, Node}) ->
    report_error("mnesia_event got {inconsistent_database, ~tw, ~w}~n", [Reason, Node]);
system_event({mnesia_checkpoint_deactivated, Checkpoint}) ->
    report_error("Checkpoint '~p' has been deactivated, last table copy deleted.\n", [Checkpoint]);
system_event({Quiet, _}) when Quiet =:= mnesia_up; Quiet =:= mnesia_down; Quiet =:= mnesia_checkpoint_activated ->
    ok;
system_event(Event) ->
    report_error("mnesia_event got unexpected system event: ~tp~n", [Event]).

% Info reports go to the terminal, or to the process registered globally as mnesia_global_logger.
report_info(Format, Args) ->
    case global:whereis_name(mnesia_global_logger) of
        undefined -> io:format("Mnesia(~p): " ++ Format, [node() | Args]);
        Logger -> io:format(Logger, "Mnesia(~p): " ++ Format, [node() | Args])
    end.

report_warning(Format, Args) ->
    error_logger:warning_msg("Mnesia(~p): ** WARNING ** " ++ Format, [node() | Args]),
    copy_to_global_logger("Mnesia(~p): ** WARNING ** " ++ Format, [node() | Args]).

report_error(Format, Args) ->
    error_logger:format("Mnesia(~p): ** ERROR ** " ++ Format, [node() | Args]),
    copy_to_global_logger("Mnesia(~p): ** ERROR ** " ++ Format, [node() | Args]).

copy_to_global_logger(Format, Args) ->
    case global:whereis_name(mnesia_global_logger) of
        undefined -> ok;
        Logger -> io:format(Logger, Format, Args)
    end.
