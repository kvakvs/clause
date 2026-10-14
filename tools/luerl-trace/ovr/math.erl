-module(math).
-export([acosh/1,asinh/1,atanh/1,erf/1,erfc/1,tau/0]).
%% Tracing stand-in: rough pure-Erlang math so execution can continue past OTP's NIF stubs.
-export([pi/0, pow/2, sqrt/1, exp/1, log/1, log10/1, log2/1, sin/1, cos/1, tan/1, floor/1, ceil/1, fmod/2,
         atan/1, asin/1, acos/1, atan2/2, sinh/1, cosh/1, tanh/1]).
pi() -> 3.141592653589793.
pow(X, Y) when is_integer(Y), Y >= 0 -> ipow(float(X), Y, 1.0);
pow(X, Y) when is_integer(Y) -> 1.0 / ipow(float(X), -Y, 1.0);
pow(X, Y) -> exp(Y * log(X)).
ipow(_, 0, Acc) -> Acc;
ipow(X, N, Acc) -> ipow(X, N - 1, Acc * X).
sqrt(X) when X < 0 -> erlang:error(badarith);
sqrt(X) when X == 0 -> 0.0;
sqrt(X) -> newton(float(X), float(X), 60).
newton(_, G, 0) -> G;
newton(X, G, N) -> newton(X, (G + X / G) / 2, N - 1).
exp(X) -> series_exp(float(X), 1.0, 1.0, 1).
series_exp(_, Sum, _, 60) -> Sum;
series_exp(X, Sum, Term, N) -> T = Term * X / N, series_exp(X, Sum + T, T, N + 1).
log(X) when X =< 0 -> erlang:error(badarith);
log(X) -> lognewton(float(X), 0.0, 80).
lognewton(_, Y, 0) -> Y;
lognewton(X, Y, N) -> lognewton(X, Y + 2 * (X - exp(Y)) / (X + exp(Y)), N - 1).
log10(X) -> log(X) / log(10).
log2(X) -> log(X) / log(2).
sin(X) -> trig(float(X), 1).
cos(X) -> trig(float(X) + pi() / 2, 1).
trig(X, _) -> R = X - 2 * pi() * erlang:floor(X / (2 * pi())), sin_series(R, R, R, 1).
sin_series(_, Sum, _, 40) -> Sum;
sin_series(X, Sum, Term, N) -> T = -Term * X * X / ((2 * N) * (2 * N + 1)), sin_series(X, Sum + T, T, N + 1).
tan(X) -> sin(X) / cos(X).
floor(X) -> float(erlang:floor(X)).
ceil(X) -> float(erlang:ceil(X)).
fmod(X, Y) -> X - Y * float(trunc(X / Y)).
atan(X) -> atan2(X, 1.0).
asin(X) -> atan2(X, sqrt(1 - X * X)).
acos(X) -> pi() / 2 - asin(X).
atan2(_Y, _X) -> erlang:error({tracing_stand_in, atan2}).
sinh(X) -> (exp(X) - exp(-X)) / 2.
cosh(X) -> (exp(X) + exp(-X)) / 2.
tanh(X) -> sinh(X) / cosh(X).
acosh(_) -> erlang:error({tracing_stand_in, acosh, 1}).
asinh(_) -> erlang:error({tracing_stand_in, asinh, 1}).
atanh(_) -> erlang:error({tracing_stand_in, atanh, 1}).
erf(_) -> erlang:error({tracing_stand_in, erf, 1}).
erfc(_) -> erlang:error({tracing_stand_in, erfc, 1}).
tau() -> erlang:error({tracing_stand_in, tau, 0}).
