-module(cleanup).
-export([kind/1, count/2]).

%% Each clause reads an atom after the previous clause's comparison failed: one claim check covers them all.
kind(a) -> 1;
kind(b) -> 2;
kind(c) -> 3;
kind(d) -> 4;
kind(e) -> 5;
kind(f) -> 6;
kind(g) -> 7;
kind(h) -> 8.

%% A self tail loop comparing with atoms checks the claim before it starts.
count([], N) -> N;
count([on | T], N) -> count(T, N + 1);
count([off | T], N) -> count(T, N);
count([_ | T], N) -> count(T, N + 2).
