-module(fun_hashing).
-export([main/1]).

%% Fun information, hashing and MD5 (plan step 41G3). MD5 digests are byte-exact; fun identities and hash values
%% are Clause's own, so only their shapes and guarantees are printed.
main(_) ->
    funs(),
    hashes(),
    digests(),
    io:format("fun_hashing: ok~n").

funs() ->
    X = id(5),
    Local = fun(Y) -> X + Y end,
    External = fun lists:map/2,
    show([Key || {Key, _} <- erlang:fun_info(Local)]),
    show([Key || {Key, _} <- erlang:fun_info(External)]),
    show([erlang:fun_info(Local, Item) || Item <- [module, arity, env, type]]),
    show(erlang:fun_info(External)),
    show(erlang:fun_info(External, pid)),
    {name, Name} = erlang:fun_info(Local, name),
    show(is_atom(Name)),
    show(erlang:fun_to_list(External)),
    "#Fun<fun_hashing." ++ _ = erlang:fun_to_list(Local),
    show(run(fun() -> erlang:fun_info(Local, bogus) end)),
    show(run(fun() -> erlang:fun_info(not_a_fun) end)),
    show(run(fun() -> erlang:fun_to_list(not_a_fun) end)),
    Builtins = [
        {erlang, abs, 1}, {erlang, apply, 3}, {erlang, nope, 0}, {lists, map, 2}, {erlang, '+', 2}
    ],
    show([erlang:is_builtin(M, F, A) || {M, F, A} <- Builtins]),
    show(run(fun() -> erlang:is_builtin(1, 2, 3) end)).

hashes() ->
    Terms = [
        abc, 1, 1.0, "text", <<1, 2:3>>, {a, [b]}, #{a => 1}, self(), make_ref(), fun lists:map/2
    ],
    show([erlang:phash2(T) >= 0 andalso erlang:phash2(T) < 1 bsl 27 || T <- Terms]),
    show([erlang:phash2(T, 100) < 100 || T <- Terms]),
    show([
        apply(erlang, phash, [T, 100]) >= 1 andalso apply(erlang, phash, [T, 100]) =< 100
     || T <- Terms
    ]),
    show(erlang:phash2(#{a => 1, b => 2}) =:= erlang:phash2(maps:from_list([{b, 2}, {a, 1}]))),
    show(erlang:phash2({abc, "x"}) =:= erlang:phash2({abc, [$x]})),
    show(erlang:phash2(1, 1) =:= 0),
    show(erlang:phash2(abc, 1 bsl 32) >= 0),
    show(run(fun() -> erlang:phash2(abc, 0) end)),
    show(run(fun() -> erlang:phash2(abc, (1 bsl 32) + 1) end)).

digests() ->
    show(erlang:md5(<<>>)),
    show(erlang:md5(<<"abc">>)),
    show(erlang:md5([<<"a">>, "bc"])),
    Long = list_to_binary([X rem 256 || X <- lists:seq(1, 1000)]),
    show(erlang:md5(Long)),
    Context = erlang:md5_update(erlang:md5_init(), "ab"),
    show(erlang:md5_final(erlang:md5_update(Context, <<"c">>))),
    Split = lists:foldl(
        fun(B, C) -> erlang:md5_update(C, [B]) end, erlang:md5_init(), binary_to_list(Long)
    ),
    show(erlang:md5_final(Split) =:= erlang:md5(Long)),
    show(run(fun() -> erlang:md5_final(<<1, 2, 3>>) end)),
    show(run(fun() -> erlang:md5(abc) end)),
    show(run(fun() -> erlang:md5_update(erlang:md5_init(), [256]) end)).

id(X) -> X.

show(Value) -> io:format("~p~n", [Value]).

% The value of Fun, or {error, Reason}.
run(Fun) ->
    try
        Fun()
    catch
        error:Reason -> {error, Reason}
    end.
