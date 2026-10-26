%% Clause's stdlib/include/assert.hrl (plan 21 step LIB75, docs/libraries/library.md): the assertion macros of OTP's
%% documentation, written for Clause, raising OTP's error terms. Each runs in a fun called at once, as OTP's do, so
%% variables bound inside stay local and failures name the same stack frames. NOASSERT, or NODEBUG without DEBUG,
%% turns every macro into ok unless ASSERT is defined.
-ifndef(ASSERT_HRL).
-define(ASSERT_HRL, true).

-ifdef(NODEBUG).
-ifndef(DEBUG).
-ifndef(NOASSERT).
-define(NOASSERT, true).
-endif.
-endif.
-endif.

-ifdef(ASSERT).
-undef(NOASSERT).
-endif.

-ifdef(NOASSERT).
-define(assert(BoolExpr), ok).
-define(assert(BoolExpr, Comment), ok).
-define(assertNot(BoolExpr), ok).
-define(assertNot(BoolExpr, Comment), ok).
-define(assertMatch(Guard, Expr), ok).
-define(assertMatch(Guard, Expr, Comment), ok).
-define(assertNotMatch(Guard, Expr), ok).
-define(assertNotMatch(Guard, Expr, Comment), ok).
-define(assertEqual(Expect, Expr), ok).
-define(assertEqual(Expect, Expr, Comment), ok).
-define(assertNotEqual(Unexpected, Expr), ok).
-define(assertNotEqual(Unexpected, Expr, Comment), ok).
-define(assertException(Class, Term, Expr), ok).
-define(assertException(Class, Term, Expr, Comment), ok).
-define(assertNotException(Class, Term, Expr), ok).
-define(assertNotException(Class, Term, Expr, Comment), ok).
-else.
-define(assert(BoolExpr), ?CLAUSE_ASSERT_BOOLEAN(BoolExpr, true, [])).
-define(assert(BoolExpr, Comment), ?CLAUSE_ASSERT_BOOLEAN(BoolExpr, true, [{comment, (Comment)}])).
-define(assertNot(BoolExpr), ?CLAUSE_ASSERT_BOOLEAN(BoolExpr, false, [])).
-define(assertNot(BoolExpr, Comment), ?CLAUSE_ASSERT_BOOLEAN(BoolExpr, false, [{comment, (Comment)}])).
-define(assertMatch(Guard, Expr), ?CLAUSE_ASSERT_MATCH(Guard, Expr, [])).
-define(assertMatch(Guard, Expr, Comment), ?CLAUSE_ASSERT_MATCH(Guard, Expr, [{comment, (Comment)}])).
-define(assertNotMatch(Guard, Expr), ?CLAUSE_ASSERT_NOT_MATCH(Guard, Expr, [])).
-define(assertNotMatch(Guard, Expr, Comment), ?CLAUSE_ASSERT_NOT_MATCH(Guard, Expr, [{comment, (Comment)}])).
-define(assertEqual(Expect, Expr), ?CLAUSE_ASSERT_EQUAL(Expect, Expr, [])).
-define(assertEqual(Expect, Expr, Comment), ?CLAUSE_ASSERT_EQUAL(Expect, Expr, [{comment, (Comment)}])).
-define(assertNotEqual(Unexpected, Expr), ?CLAUSE_ASSERT_NOT_EQUAL(Unexpected, Expr, [])).
-define(assertNotEqual(Unexpected, Expr, Comment),
    ?CLAUSE_ASSERT_NOT_EQUAL(Unexpected, Expr, [{comment, (Comment)}])
).
-define(assertException(Class, Term, Expr), ?CLAUSE_ASSERT_EXCEPTION(Class, Term, Expr, [])).
-define(assertException(Class, Term, Expr, Comment),
    ?CLAUSE_ASSERT_EXCEPTION(Class, Term, Expr, [{comment, (Comment)}])
).
-define(assertNotException(Class, Term, Expr), ?CLAUSE_ASSERT_NOT_EXCEPTION(Class, Term, Expr, [])).
-define(assertNotException(Class, Term, Expr, Comment),
    ?CLAUSE_ASSERT_NOT_EXCEPTION(Class, Term, Expr, [{comment, (Comment)}])
).
-endif.

-define(assertError(Term, Expr), ?assertException(error, Term, Expr)).
-define(assertError(Term, Expr, Comment), ?assertException(error, Term, Expr, Comment)).
-define(assertExit(Term, Expr), ?assertException(exit, Term, Expr)).
-define(assertExit(Term, Expr, Comment), ?assertException(exit, Term, Expr, Comment)).
-define(assertThrow(Term, Expr), ?assertException(throw, Term, Expr)).
-define(assertThrow(Term, Expr, Comment), ?assertException(throw, Term, Expr, Comment)).

%% The report every failure starts with: the module and line, then the optional comment, then the macro's own items.
-define(CLAUSE_ASSERT_REPORT(Comment, Items), [{module, ?MODULE}, {line, ?LINE} | Comment ++ Items]).

%% The pattern text of the exception macros, as OTP spells it.
-define(CLAUSE_ASSERT_PATTERN(Class, Term), "{ " ++ (??Class) ++ " , " ++ (??Term) ++ " , [...] }").

%% assert/assertNot: the expected boolean comes from a call, so a constant expression draws no compiler warning.
-define(CLAUSE_ASSERT_BOOLEAN(BoolExpr, Expected, Comment), begin
    ((fun() ->
        X__E = (Expected =:= is_process_alive(self())),
        case (BoolExpr) of
            X__E ->
                ok;
            X__V ->
                erlang:error(
                    {assert,
                        ?CLAUSE_ASSERT_REPORT(Comment, [
                            {expression, (??BoolExpr)},
                            {expected, X__E},
                            case not X__E of
                                X__V -> {value, X__V};
                                _ -> {not_boolean, X__V}
                            end
                        ])}
                )
        end
    end)())
end).

-define(CLAUSE_ASSERT_MATCH(Guard, Expr, Comment), begin
    ((fun() ->
        case (Expr) of
            Guard ->
                ok;
            X__V ->
                erlang:error(
                    {assertMatch,
                        ?CLAUSE_ASSERT_REPORT(Comment, [
                            {expression, (??Expr)}, {pattern, (??Guard)}, {value, X__V}
                        ])}
                )
        end
    end)())
end).

-define(CLAUSE_ASSERT_NOT_MATCH(Guard, Expr, Comment), begin
    ((fun() ->
        X__V = (Expr),
        case X__V of
            Guard ->
                erlang:error(
                    {assertNotMatch,
                        ?CLAUSE_ASSERT_REPORT(Comment, [
                            {expression, (??Expr)}, {pattern, (??Guard)}, {value, X__V}
                        ])}
                );
            _ ->
                ok
        end
    end)())
end).

-define(CLAUSE_ASSERT_EQUAL(Expect, Expr, Comment), begin
    ((fun() ->
        X__X = (Expect),
        case (Expr) of
            X__X ->
                ok;
            X__V ->
                erlang:error(
                    {assertEqual,
                        ?CLAUSE_ASSERT_REPORT(Comment, [
                            {expression, (??Expr)}, {expected, X__X}, {value, X__V}
                        ])}
                )
        end
    end)())
end).

-define(CLAUSE_ASSERT_NOT_EQUAL(Unexpected, Expr, Comment), begin
    ((fun() ->
        X__X = (Unexpected),
        case (Expr) of
            X__X ->
                erlang:error(
                    {assertNotEqual, ?CLAUSE_ASSERT_REPORT(Comment, [{expression, (??Expr)}, {value, X__X}])}
                );
            _ ->
                ok
        end
    end)())
end).

-define(CLAUSE_ASSERT_EXCEPTION(Class, Term, Expr, Comment), begin
    ((fun() ->
        try (Expr) of
            X__V ->
                erlang:error(
                    {assertException,
                        ?CLAUSE_ASSERT_REPORT(Comment, [
                            {expression, (??Expr)},
                            {pattern, ?CLAUSE_ASSERT_PATTERN(Class, Term)},
                            {unexpected_success, X__V}
                        ])}
                )
        catch
            Class:Term ->
                ok;
            X__C:X__T:X__S ->
                erlang:error(
                    {assertException,
                        ?CLAUSE_ASSERT_REPORT(Comment, [
                            {expression, (??Expr)},
                            {pattern, ?CLAUSE_ASSERT_PATTERN(Class, Term)},
                            {unexpected_exception, {X__C, X__T, X__S}}
                        ])}
                )
        end
    end)())
end).

%% assertNotException: OTP matches the class first, then the term, so a term pattern may use the class's bindings.
-define(CLAUSE_ASSERT_NOT_EXCEPTION(Class, Term, Expr, Comment), begin
    ((fun() ->
        try (Expr) of
            _ ->
                ok
        catch
            X__C:X__T:X__S ->
                case X__C of
                    Class ->
                        case X__T of
                            Term ->
                                erlang:error(
                                    {assertNotException,
                                        ?CLAUSE_ASSERT_REPORT(Comment, [
                                            {expression, (??Expr)},
                                            {pattern, ?CLAUSE_ASSERT_PATTERN(Class, Term)},
                                            {unexpected_exception, {X__C, X__T, X__S}}
                                        ])}
                                );
                            _ ->
                                ok
                        end;
                    _ ->
                        ok
                end
        end
    end)())
end).

-endif.
