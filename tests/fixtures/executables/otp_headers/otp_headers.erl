%% OTP's public headers through include_lib, without OTP on any path (plan 21 step LIB75): assert.hrl's macros and
%% their error terms, inet.hrl's record, and ms_transform.hrl (in ms_headers) naming the transform.
-module(otp_headers).
-export([main/1]).

-include_lib("stdlib/include/assert.hrl").
-include_lib("kernel/include/inet.hrl").

main(_) ->
    [io:format("~s: ~p~n", [Name, outcome(Case)]) || {Name, Case} <- asserts()],
    io:format("assert_hrl: ~p~n", [assert_hrl()]),
    io:format("scoped: ~p~n", [scoped()]),
    io:format("nodebug: ~p~n", [assert_off:run()]),
    io:format("forced: ~p~n", [assert_forced:run()]),
    io:format("hostent: ~p~n", [#hostent{}]),
    io:format("hostent_fields: ~p~n", [record_info(fields, hostent)]),
    {ok, Host} = inet:gethostbyname("localhost", inet),
    io:format("localhost: ~p~n", [{is_record(Host, hostent), Host#hostent.h_addrtype, Host#hostent.h_length}]),
    io:format("ms: ~p~n", [ms_headers:run()]),
    io:format("otp_headers: ok~n").

% Each macro passing and failing, with and without a comment.
asserts() ->
    [
        {assert_true, fun() -> ?assert(id(true)) end},
        {assert_false, fun() -> ?assert(id(false)) end},
        {assert_not_boolean, fun() -> ?assert(id(42)) end},
        {assert_macro_text, fun() -> ?assert(?MODULE =:= id(other)) end},
        {assert_comment, fun() -> ?assert(id(1) > 2, "too small") end},
        {assert_not_false, fun() -> ?assertNot(id(false)) end},
        {assert_not_true, fun() -> ?assertNot(id(true)) end},
        {assert_not_atom, fun() -> ?assertNot(id(perhaps), {note, 1}) end},
        {match, fun() -> ?assertMatch({ok, _}, id({ok, 1})) end},
        {match_fail, fun() -> ?assertMatch({ok, _}, id({error, 1})) end},
        {match_guard, fun() -> ?assertMatch({X, _} when X > 1, id({1, 2})) end},
        {match_comment, fun() -> ?assertMatch([_], id([]), "one") end},
        {not_match, fun() -> ?assertNotMatch({ok, _}, id({error, 1})) end},
        {not_match_fail, fun() -> ?assertNotMatch({ok, _}, id({ok, 1}), <<"bin">>) end},
        {equal, fun() -> ?assertEqual(3, id(1) + 2) end},
        {equal_fail, fun() -> ?assertEqual(3, id(1) + 1) end},
        {equal_comment, fun() -> ?assertEqual(a, id(b), lists:concat([c, d])) end},
        {not_equal, fun() -> ?assertNotEqual(3, id(4)) end},
        {not_equal_fail, fun() -> ?assertNotEqual(3, id(3)) end},
        {not_equal_comment, fun() -> ?assertNotEqual(x, id(x), "same") end},
        {exception, fun() -> ?assertException(throw, {t, _}, throw(id({t, 1}))) end},
        {exception_success, fun() -> ?assertException(error, badarg, id(fine)) end},
        {exception_other, fun() -> ?assertException(exit, x, throw(id(y))) end},
        {exception_comment, fun() -> ?assertException(throw, a, id(b), "c") end},
        {error, fun() -> ?assertError(badarg, list_to_atom(id(1))) end},
        {error_fail, fun() -> ?assertError(badarith, list_to_atom(id(1))) end},
        {error_comment, fun() -> ?assertError(badarg, id(ok), "d") end},
        {exit, fun() -> ?assertExit(normal, exit(id(normal))) end},
        {exit_fail, fun() -> ?assertExit(normal, id(ok)) end},
        {exit_comment, fun() -> ?assertExit(normal, exit(id(other)), e) end},
        {throw, fun() -> ?assertThrow(z, throw(id(z))) end},
        {throw_fail, fun() -> ?assertThrow(z, throw(id(w))) end},
        {throw_comment, fun() -> ?assertThrow(z, id(z), f) end},
        {not_exception, fun() -> ?assertNotException(error, badarg, id(ok)) end},
        {not_exception_other_class, fun() -> ?assertNotException(error, badarg, throw(id(badarg))) end},
        {not_exception_other_term, fun() -> ?assertNotException(error, badarg, error(id(badarith))) end},
        {not_exception_fail, fun() -> ?assertNotException(error, badarg, error(id(badarg))) end},
        {not_exception_comment, fun() -> ?assertNotException(throw, 1, throw(id(1)), "g") end}
    ].

-ifdef(ASSERT_HRL).
assert_hrl() -> defined.
-else.
assert_hrl() -> undefined.
-endif.

% A variable bound inside a macro stays inside its fun: X is still free here.
scoped() ->
    ok = ?assertMatch({X, _} when X > 0, id({1, 2})),
    X = id(3),
    X.

% The call's result, or the exception's class and reason with any stack trace left out.
outcome(Case) ->
    try Case() of
        Value -> {value, Value}
    catch
        Class:{Kind, Items} when is_list(Items) -> {Class, Kind, [without_stack(Item) || Item <- Items]};
        Class:Reason -> {Class, Reason}
    end.

without_stack({unexpected_exception, {Class, Reason, _Stack}}) -> {unexpected_exception, {Class, Reason}};
without_stack(Item) -> Item.

id(Value) -> Value.
