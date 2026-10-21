-module(integer_limit).
-export([main/1]).

%% ERTS caps an integer magnitude at 4,194,240 bits on 64-bit hosts
%% (BIG_ARITY_MAX = 65,535 words of 64 bits): a larger result raises
%% error:system_limit in a body and fails a guard. Multiplications use a
%% small factor, so no run multiplies two huge numbers.

%% The first argument selects a scenario.
main(["limit"]) ->
    Top = top(),
    show(Top bsr 4194239),
    show(Top band 255),
    [
        show(attempt(Operation, Top))
     || Operation <- [
            increment,
            negate,
            negate_decrement,
            complement,
            complement_negated,
            power,
            power_past,
            multiply,
            multiply_past,
            square,
            difference
        ]
    ];
main(["guard"]) ->
    Top = top(),
    show(guarded(Top)),
    show(guarded(Top - 1)),
    show(either(Top));
main(["bits"]) ->
    Top = top(),
    <<Value:4194240>> = <<Top:4194240>>,
    show(Value =:= Top),
    show(match(<<-1:4194240>>) =:= Top);
main(["extract"]) ->
    show(over(<<-1:4194241>>));
main(["scale"]) ->
    Big = 1 bsl id(64),
    [
        show(Product)
     || Product <- [
            Big * id(3),
            id(3) * Big,
            Big * id(-3),
            id(-3) * -Big,
            -Big * id(0),
            (1 bsl id(127)) * id(1 bsl 58),
            id(1 bsl 40) * id(1 bsl 40),
            id(-(1 bsl 40)) * id(1 bsl 40),
            (1 bsl id(59)) * id(-1) =:= -576460752303423488,
            id(-576460752303423488) * id(-1),
            factorial(id(100), 1)
        ]
    ];
main(["uncaught"]) ->
    show(top() + id(1)).

%% A bignum times a small factor, both operand orders: 100! ends in zero
%% limbs, and 2^59 * -1 is the smallest small integer.
factorial(0, Acc) -> Acc;
factorial(N, Acc) -> factorial(N - 1, Acc * N).

show(Value) -> erlang:display(Value).

id(Value) -> Value.

%% 2^4194240 - 1: every one of the 4,194,240 magnitude bits set.
top() ->
    Half = 1 bsl id(4194239),
    Half - 1 + Half.

%% The result of an operation on Top, reduced to a small value, or the class
%% and reason it raised.
attempt(Operation, Top) ->
    try
        {ok, operate(Operation, Top)}
    catch
        Class:Reason -> {Class, Reason}
    end.

operate(increment, Top) -> Top + 1;
operate(negate, Top) -> -Top =:= 0 - Top;
operate(negate_decrement, Top) -> -Top - 1;
operate(complement, Top) -> bnot Top;
operate(complement_negated, Top) -> bnot (-Top) =:= Top - 1;
operate(power, _) -> (1 bsl id(4194239)) bsr 4194239;
operate(power_past, _) -> 1 bsl id(4194240);
operate(multiply, _) -> ((1 bsl id(4194238)) * 3) bsr 4194238;
operate(multiply_past, _) -> (1 bsl id(4194238)) * 4;
operate(square, Top) -> Top * Top;
operate(difference, Top) -> Top - Top.

%% A guard whose arithmetic exceeds the limit fails like any guard error.
guarded(X) when X + 1 > 0 -> fits;
guarded(_) -> failed.

%% Only the failing guard alternative is skipped.
either(X) when X + 1 > 0; X > 0 -> second;
either(_) -> neither.

match(<<Value:4194240>>) -> Value.

%% An unsigned segment one bit wider than the limit, all ones, does not match
%% in Clause; OTP 29's JIT builds an invalid term from it.
over(<<Value:4194241>>) -> Value;
over(_) -> none.
