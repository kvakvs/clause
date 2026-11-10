-module(ea_all).
-compile([export_all, nowarn_export_all]).
-export([double/1]).

double(X) -> X * 2.

helper(X) -> {helper, X}.

pair(A, B) -> {A, B}.
