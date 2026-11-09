%% Record wildcards `_ = Value` in constructions and patterns (plan 21 step MIM9): a construction naming every field
%% still takes a wildcard, which then initializes nothing and is not evaluated (MongooseIM's mod_muc_mnesia builds
%% match_object patterns this way); otherwise the wildcard's expression is evaluated once per omitted field.
-module(record_wildcards).
-export([main/1]).

-record(room, {name, host, opts}).
-record(pair, {a, b}).
-record(empty, {}).

main(_) ->
    show(#room{name = <<"r">>, host = <<"h">>, opts = [], _ = '_'}),
    show(#room{name = <<"r">>, host = <<"h">>, opts = [], _ = told(all)}),
    show(#room{name = <<"r">>, _ = told(each)}),
    show(#room{_ = told(every)}),
    show(#empty{_ = told(empty)}),
    show(#pair{a = 1, b = 2, _ = undefined_function()}),
    show(closed(7)),
    show(matched(#room{name = n, host = x, opts = x})),
    show(matched(#room{name = n, host = x, opts = y})),
    show(matched(#pair{a = 1, b = 2})),
    show(listed()),
    io:format("record_wildcards: ok~n").

%% A value that says when it is evaluated.
told(Label) ->
    io:format("evaluated ~p~n", [Label]),
    Label.

%% Never called: a wildcard initializing no field is not evaluated.
undefined_function() -> erlang:error(evaluated).

%% A fun in a wildcard that initializes nothing is never made.
closed(X) -> #pair{a = 1, b = 2, _ = fun() -> X end}.

%% A pattern's wildcard binds one variable to every omitted field, so they must be equal.
matched(#room{name = n, _ = Same}) -> {same, Same};
matched(#pair{a = 1, _ = B}) -> {pair, B};
matched(Other) -> {other, Other}.

%% Patterns built with every field given, as match specifications and match_object patterns use them.
listed() ->
    Rooms = [#room{name = N, host = h, opts = [N]} || N <- [a, b, c]],
    Pattern = #room{name = b, host = h, opts = '_', _ = '_'},
    Spec = [{#room{name = '$1', host = h, opts = '_', _ = '_'}, [{'=/=', '$1', a}], ['$1']}],
    {[R || R <- Rooms, matches(Pattern, R)], ets:match_spec_run(Rooms, ets:match_spec_compile(Spec))}.

matches(Pattern, Record) ->
    lists:all(fun({P, V}) -> P =:= '_' orelse P =:= V end, lists:zip(tuple_to_list(Pattern), tuple_to_list(Record))).

show(Value) -> io:format("~p~n", [Value]).
