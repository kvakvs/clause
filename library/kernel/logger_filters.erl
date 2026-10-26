%% Clause's logger_filters (plan 21 step LIB23, docs/libraries/library.md): the filter functions logger offers for primary and
%% handler filters. Each returns the log event (Action log), stop (Action stop) when the event matches, and ignore
%% otherwise.
-module(logger_filters).
-export([domain/2, level/2, progress/2, remote_gl/2]).

-define(IS_ACTION(A), (A =:= log orelse A =:= stop)).
-define(IS_LEVEL(L),
    (L =:= emergency orelse L =:= alert orelse L =:= critical orelse L =:= error orelse L =:= warning orelse
        L =:= notice orelse L =:= info orelse L =:= debug)
).

%% Compares the event's domain with MatchDomain: super (the event's domain is a prefix of it), sub (it is a prefix
%% of the event's), equal, not_equal, or undefined (the event has no domain).
domain(#{meta := Meta} = Event, {Action, Compare, MatchDomain} = Extra) when ?IS_ACTION(Action), is_list(MatchDomain) ->
    case lists:member(Compare, [super, sub, equal, not_equal, undefined]) of
        true -> by_domain(Compare, Meta, MatchDomain, matched(Action, Event));
        false -> erlang:error(badarg, [Event, Extra])
    end;
domain(Event, Extra) ->
    erlang:error(badarg, [Event, Extra]).

by_domain(super, #{domain := Domain}, Match, OnMatch) ->
    prefix(Domain, Match, OnMatch);
by_domain(sub, #{domain := Domain}, Match, OnMatch) ->
    prefix(Match, Domain, OnMatch);
by_domain(equal, #{domain := Domain}, Domain, OnMatch) ->
    OnMatch;
by_domain(not_equal, #{domain := Domain}, Match, OnMatch) when Domain =/= Match -> OnMatch;
by_domain(Compare, Meta, _, OnMatch) ->
    case is_map_key(domain, Meta) of
        false when Compare =:= undefined; Compare =:= not_equal -> OnMatch;
        _ -> ignore
    end.

prefix(Short, Long, OnMatch) when is_list(Short), is_list(Long) ->
    case lists:prefix(Short, Long) of
        true -> OnMatch;
        false -> ignore
    end;
prefix(_, _, _) ->
    ignore.

%% Compares the event's level with Level by Operator (neq, eq, lt, gt, lteq, gteq).
level(#{level := EventLevel} = Event, {Action, Operator, Level} = Extra) when ?IS_ACTION(Action), ?IS_LEVEL(Level) ->
    case lists:member(Operator, [neq, eq, lt, gt, lteq, gteq]) of
        true -> by_level(Operator, logger:compare_levels(EventLevel, Level), matched(Action, Event));
        false -> erlang:error(badarg, [Event, Extra])
    end;
level(Event, Extra) ->
    erlang:error(badarg, [Event, Extra]).

by_level(Operator, eq, OnMatch) when Operator =:= eq; Operator =:= lteq; Operator =:= gteq -> OnMatch;
by_level(Operator, lt, OnMatch) when Operator =:= lt; Operator =:= lteq; Operator =:= neq -> OnMatch;
by_level(Operator, gt, OnMatch) when Operator =:= gt; Operator =:= gteq; Operator =:= neq -> OnMatch;
by_level(_, _, _) -> ignore.

%% Matches the progress reports of supervisors and applications.
progress(Event, Action) when ?IS_ACTION(Action) ->
    case Event of
        #{msg := {report, #{label := {_, progress}}}} -> matched(Action, Event);
        _ -> ignore
    end;
progress(Event, Action) ->
    erlang:error(badarg, [Event, Action]).

%% Matches events whose group leader is on another node.
remote_gl(Event, Action) when ?IS_ACTION(Action) ->
    case Event of
        #{meta := #{gl := GroupLeader}} when node(GroupLeader) =/= node() -> matched(Action, Event);
        _ -> ignore
    end;
remote_gl(Event, Action) ->
    erlang:error(badarg, [Event, Action]).

matched(log, Event) -> Event;
matched(stop, _) -> stop.
