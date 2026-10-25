%% NODEBUG turns assert.hrl's macros off (plan 21 step LIB75): none evaluates its arguments.
-module(assert_off).
-export([run/0]).

-define(NODEBUG, true).
-include_lib("stdlib/include/assert.hrl").

run() ->
    Results = [
        ?assert(self() ! a),
        ?assertNot(self() ! b),
        ?assertMatch(x, self() ! c),
        ?assertEqual(y, self() ! d),
        ?assertNotEqual(e, self() ! e),
        ?assertError(badarg, self() ! f),
        ?assertNotException(throw, g, self() ! g)
    ],
    {Results, defined(), mailbox()}.

-ifdef(NOASSERT).
defined() -> noassert.
-else.
defined() -> assert.
-endif.

mailbox() ->
    receive
        Message -> [Message | mailbox()]
    after 0 -> []
    end.
