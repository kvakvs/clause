%% mnesia's default event handler, installed in the mnesia_event manager when the event_module parameter is
%% mnesia_event (plan 21 step MIM1C, docs/libraries/mnesia.md#processes): it prints mnesia's info reports as OTP's
%% handler does. Subscriptions and the other system events follow in MIM1E.
-module(clause_mnesia_event).
-behaviour(gen_event).
-export([add_handler/0]).
-export([init/1, handle_event/2, handle_call/2]).

% The event_module parameter's handler joins the manager: this module for the default mnesia_event.
add_handler() ->
    Handler =
        case clause_mnesia_monitor:get_env(event_module) of
            mnesia_event -> ?MODULE;
            Module -> Module
        end,
    gen_event:add_handler(mnesia_event, Handler, []).

init(_) ->
    {ok, []}.

% An info report, printed once when the same report comes again in a row.
handle_event({mnesia_system_event, {mnesia_info, Format, Args} = Event}, State) ->
    case put(last, Event) of
        Event -> ok;
        _ -> io:format("Mnesia(~p): " ++ Format, [node() | Args])
    end,
    {ok, State};
handle_event(_, State) ->
    {ok, State}.

handle_call(_, State) ->
    {ok, ok, State}.
