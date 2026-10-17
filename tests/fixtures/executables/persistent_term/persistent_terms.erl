-module(persistent_terms).
-export([main/1]).

%% persistent_term (plan step 41M2): put/2, put_new/2, get/0,1,2, erase/1 and
%% info/0 with large values, several processes, collections of the reader and
%% OTP's errors.
main([]) ->
    Key = {?MODULE, config},
    Big = {lists:seq(1, 5000), list_to_binary(lists:duplicate(1000, $x)), #{a => 1}},
    ok = persistent_term:put(Key, Big),
    Big = persistent_term:get(Key),
    io:format("~w~n", [
        [
            persistent_term:get({?MODULE, missing}, default),
            persistent_term:get(Key, default) =:= Big,
            outcome(fun() -> persistent_term:get({?MODULE, missing}) end),
            persistent_term:put(Key, Big),
            persistent_term:put_new({?MODULE, fresh}, 1),
            outcome(fun() -> persistent_term:put_new({?MODULE, fresh}, 2) end),
            persistent_term:put_new({?MODULE, fresh}, 1),
            persistent_term:get({?MODULE, fresh}),
            persistent_term:erase({?MODULE, fresh}),
            persistent_term:erase({?MODULE, fresh}),
            lists:member({Key, Big}, persistent_term:get()),
            is_map(persistent_term:info()),
            lists:sort(maps:keys(persistent_term:info())),
            persistent_term:put(1.0, float),
            persistent_term:get(1, int_default),
            persistent_term:get(1.0)
        ]
    ]),
    readers(Key, Big),
    shared(Key, Big),
    replaced(Key),
    io:format("persistent_terms: ok~n").

%% Several processes read the value, also after collecting their heaps.
readers(Key, Big) ->
    Self = self(),
    Pids = [
        spawn(fun() ->
            Value = persistent_term:get(Key),
            garbage_collect(),
            Self ! {self(), Value =:= Big andalso persistent_term:get(Key) =:= Big}
        end)
     || _ <- lists:seq(1, 8)
    ],
    io:format("~w~n", [
        [
            receive
                {Pid, Same} -> Same
            end
         || Pid <- Pids
        ]
    ]).

%% A stored value sent to another process, wrapped in new terms across
%% collections, and stored again under another key stays the same value.
shared(Key, Big) ->
    Self = self(),
    Echo = spawn(fun() ->
        receive
            {From, Value} ->
                garbage_collect(),
                From ! {self(), {wrapped, Value}}
        end
    end),
    Echo ! {Self, persistent_term:get(Key)},
    Wrapped =
        receive
            {Echo, Reply} -> Reply
        end,
    Built = [persistent_term:get(Key) || _ <- lists:seq(1, 3)],
    garbage_collect(),
    ok = persistent_term:put({?MODULE, again}, {again, persistent_term:get(Key)}),
    Again = persistent_term:get({?MODULE, again}),
    true = persistent_term:erase({?MODULE, again}),
    io:format("~w~n", [
        [Wrapped =:= {wrapped, Big}, Built =:= [Big, Big, Big], Again =:= {again, Big}]
    ]).

%% A reader keeps its value after the key is replaced and erased.
replaced(Key) ->
    Old = persistent_term:get(Key),
    ok = persistent_term:put(Key, new),
    true = persistent_term:erase(Key),
    garbage_collect(),
    io:format("~w~n", [[element(3, Old), persistent_term:get(Key, gone)]]).

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
