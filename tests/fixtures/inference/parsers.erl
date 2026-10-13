%% Generated-parser shapes (yecc and leex output, the slowest recursive groups of luerl): many heads of one function
%% selected by an integer state, each calling the function again (or its dispatcher) with slightly altered arguments:
%% the next state, a longer token, an extended stack. The grammar is E -> int | E '+' int over "1 + 22 + 3_00".
%% Today local inputs never settle: widening relaxes only top-level integers, so the sums on the value stack (and the
%% counter in count_up/2's list) grow in every pass, and the last pass gives every local input term().
-module(parsers).
-export([main/1]).

%% expect: main(_) -> ok
main(_) ->
    io:format("~p~n", [parse(scan("1 + 22 + 3_00"))]),
    io:format("~p~n", [parse(scan("1 +"))]),
    io:format("~p~n", [count_up([0], 3)]),
    io:format("parsers: ok~n").

%% Scanner in leex style: yystate(State, Input, TokenLength, Value).

%% expect: scan([]) -> []; ([32 | 43 | 48..57 | 95, ...]) -> [{'+', 1} | {int, 1, non_neg_integer()}]
%% today: scan([]) -> []; (nonempty_maybe_improper_list()) -> [{'+', 1} | {int, 1, _}]
scan([]) ->
    [];
scan([$\s | Cs]) ->
    scan(Cs);
scan([$+ | Cs]) ->
    [{'+', 1} | scan(Cs)];
scan([C | _] = Cs) when C >= $0, C =< $9 ->
    {_Length, N, Rest} = yystate(0, Cs, 0, 0),
    [{int, 1, N} | scan(Rest)].

%% expect: yystate(0..2, [32 | 43 | 48..57 | 95], non_neg_integer(), non_neg_integer()) -> {pos_integer(), non_neg_integer(), [32 | 43 | 48..57 | 95]}
%% today: yystate(0, nonempty_maybe_improper_list(), _, _) -> {_, _, _}; (1, nonempty_maybe_improper_list(), _, _) -> {_, _, _}; (2, nonempty_maybe_improper_list(), _, _) -> {_, _, _}; (1, _, _, _) -> {_, _, _}; (_, _, _, _) -> none()
yystate(0, [C | Ics], Tlen, Acc) when C >= $0, C =< $9 ->
    yystate(1, Ics, Tlen + 1, Acc * 10 + C - $0);
yystate(1, [C | Ics], Tlen, Acc) when C >= $0, C =< $9 ->
    yystate(1, Ics, Tlen + 1, Acc * 10 + C - $0);
yystate(1, [$_ | Ics], Tlen, Acc) ->
    yystate(2, Ics, Tlen + 1, Acc);
yystate(2, [C | Ics], Tlen, Acc) when C >= $0, C =< $9 ->
    yystate(1, Ics, Tlen + 1, Acc * 10 + C - $0);
yystate(1, Ics, Tlen, Acc) ->
    {Tlen, Acc, Ics};
yystate(State, Ics, _, _) ->
    erlang:error({scan_error, State, Ics}).

%% Parser in yecc style: states 0..4, a state stack and a value stack.

%% expect: parse(list()) -> {ok, integer()} | {error, tuple()}
%% today: parse(_) -> {error, _} | {ok, _}
parse(Tokens) ->
    yeccpars0(Tokens, 0, [], []).

%% expect: yeccpars0(list(), 0, [], []) -> {ok, integer()} | {error, tuple()}
%% today: yeccpars0(_, _, _, _) -> {error, _} | {ok, _}
yeccpars0(Tokens, State, States, Vstack) ->
    try
        yeccpars1(Tokens, State, States, Vstack)
    catch
        error:{yecc_error, Token} -> {error, Token}
    end.

%% expect: yeccpars1(list(), 0, [], []) -> {ok, integer()}
%% today: yeccpars1(nonempty_maybe_improper_list(), _, _, _) -> {ok, _}; ([], _, _, _) -> {ok, _}
yeccpars1([Token | Tokens], State, States, Vstack) ->
    yeccpars2(State, element(1, Token), States, Vstack, Token, Tokens);
yeccpars1([], State, States, Vstack) ->
    yeccpars2(State, '$end', States, Vstack, {'$end', 999}, []).

%% Shift: push the current state and token, then read the next token.
%% expect: yeccpars1(0..3, 1..4, list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccpars1(_, _, _, _, _, nonempty_maybe_improper_list()) -> {ok, _}; (_, _, _, _, _, []) -> {ok, _}
yeccpars1(State1, State, States, Vstack, Token0, [Token | Tokens]) ->
    yeccpars2(State, element(1, Token), [State1 | States], [Token0 | Vstack], Token, Tokens);
yeccpars1(State1, State, States, Vstack, Token0, []) ->
    yeccpars2(State, '$end', [State1 | States], [Token0 | Vstack], {'$end', 999}, []).

%% expect: yeccpars2(0..4, atom(), list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccpars2(0, _, _, _, _, _) -> {ok, _}; (1, _, _, _, _, _) -> {ok, _}; (2, _, _, _, _, _) -> {ok, _}; (3, _, _, _, _, _) -> {ok, _}; (4, _, _, _, _, _) -> {ok, _}; (_, _, _, _, _, _) -> none()
yeccpars2(0 = S, Cat, Ss, Stack, T, Ts) ->
    yeccpars2_0(S, Cat, Ss, Stack, T, Ts);
yeccpars2(1 = S, Cat, Ss, Stack, T, Ts) ->
    yeccpars2_1(S, Cat, Ss, Stack, T, Ts);
yeccpars2(2 = S, Cat, Ss, Stack, T, Ts) ->
    yeccpars2_2(S, Cat, Ss, Stack, T, Ts);
yeccpars2(3 = S, Cat, Ss, Stack, T, Ts) ->
    yeccpars2_3(S, Cat, Ss, Stack, T, Ts);
yeccpars2(4 = S, Cat, Ss, Stack, T, Ts) ->
    yeccpars2_4(S, Cat, Ss, Stack, T, Ts);
yeccpars2(Other, _, _, _, _, _) ->
    erlang:error({yecc_bug, Other}).

%% expect: yeccpars2_0(0, atom(), list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccpars2_0(_, int, _, _, _, _) -> {ok, _}; (_, _, _, _, _, _) -> none()
yeccpars2_0(S, int, Ss, Stack, T, Ts) ->
    yeccpars1(S, 1, Ss, Stack, T, Ts);
yeccpars2_0(_, _, _, _, T, _) ->
    yeccerror(T).

%% Reduce E -> int: replace the token by its value and go to the state after E.
%% expect: yeccpars2_1(1, atom(), list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccpars2_1(_, _, nonempty_maybe_improper_list(), nonempty_maybe_improper_list(), _, _) -> {ok, _}
yeccpars2_1(_S, Cat, Ss, Stack, T, Ts) ->
    NewStack = yeccpars2_1_(Stack),
    yeccgoto_expr(hd(Ss), Cat, Ss, NewStack, T, Ts).

%% expect: yeccpars2_2(2, atom(), list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccpars2_2(_, '$end', _, _, _, _) -> {ok, _}; (_, '+', _, _, _, _) -> {ok, _}; (_, _, _, _, _, _) -> none()
yeccpars2_2(_S, '$end', _Ss, Stack, _T, _Ts) ->
    {ok, hd(Stack)};
yeccpars2_2(S, '+', Ss, Stack, T, Ts) ->
    yeccpars1(S, 3, Ss, Stack, T, Ts);
yeccpars2_2(_, _, _, _, T, _) ->
    yeccerror(T).

%% expect: yeccpars2_3(3, atom(), list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccpars2_3(_, int, _, _, _, _) -> {ok, _}; (_, _, _, _, _, _) -> none()
yeccpars2_3(S, int, Ss, Stack, T, Ts) ->
    yeccpars1(S, 4, Ss, Stack, T, Ts);
yeccpars2_3(_, _, _, _, T, _) ->
    yeccerror(T).

%% Reduce E -> E '+' int: pop two more states, add the values.
%% expect: yeccpars2_4(4, atom(), list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccpars2_4(_, _, _, nonempty_maybe_improper_list(), _, _) -> {ok, _}
yeccpars2_4(_S, Cat, Ss, Stack, T, Ts) ->
    [_, _ | Nss] = Ss,
    NewStack = yeccpars2_4_(Stack),
    yeccgoto_expr(hd(Nss), Cat, Nss, NewStack, T, Ts).

%% expect: yeccgoto_expr(0, atom(), list(0..3), list(), tuple(), list()) -> {ok, integer()}
%% today: yeccgoto_expr(0, _, _, _, _, _) -> {ok, _}; (_, _, _, _, _, _) -> none()
yeccgoto_expr(0, Cat, Ss, Stack, T, Ts) ->
    yeccpars2_2(2, Cat, Ss, Stack, T, Ts);
yeccgoto_expr(State, _, _, _, _, _) ->
    erlang:error({yecc_bug, State}).

%% expect: yeccpars2_1_(nonempty_list()) -> [integer(), ...]
%% today: yeccpars2_1_(nonempty_maybe_improper_list()) -> nonempty_maybe_improper_list()
yeccpars2_1_([{int, _, V} | Stack]) ->
    [V | Stack].

%% expect: yeccpars2_4_(nonempty_list()) -> [integer(), ...]
%% today: yeccpars2_4_(nonempty_maybe_improper_list()) -> nonempty_maybe_improper_list()
yeccpars2_4_([{int, _, B}, _Plus, A | Stack]) ->
    [A + B | Stack].

%% expect: yeccerror(tuple()) -> none()
%% today: yeccerror(_) -> none()
yeccerror(Token) ->
    erlang:error({yecc_error, Token}).

%% The smallest shape that never settles: an integer inside a list argument grows with each call.
%% expect: count_up([non_neg_integer(), ...], integer()) -> [non_neg_integer(), ...]
%% today: count_up(Stack, 0) -> Stack; (nonempty_maybe_improper_list(), _) -> nonempty_maybe_improper_list()
count_up(Stack, 0) ->
    Stack;
count_up([A | Stack], N) ->
    count_up([A + 1 | Stack], N - 1).
