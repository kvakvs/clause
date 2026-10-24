%% epp's source encoding comments (plan 21 step LIB33, docs/library.md), provided before epp itself (LIB34) for
%% file:consult/1 and its kin.
-module(clause_epp).
-export([read_encoding_from_binary/2]).

%% read_encoding_from_binary(Binary, Options): latin1 or utf8 as a "coding: Name" (or "coding=Name") comment in the
%% first two lines names it, or none; with {in_comment_only, false} the words need not follow a %. The search ends at
%% the first such words, whatever they name.
read_encoding_from_binary(Binary, Options) ->
    line(Binary, 0, proplists:get_value(in_comment_only, Options, true)).

line(_, 2, _) -> none;
line(Bin, N, true) -> before_comment(Bin, N);
line(Bin, N, false) -> comment(Bin, N, false).

before_comment(<<"\n", Rest/binary>>, N) -> line(Rest, N + 1, true);
before_comment(<<"%", Rest/binary>>, N) -> comment(Rest, N, true);
before_comment(<<_, Rest/binary>>, N) -> before_comment(Rest, N);
before_comment(<<>>, _) -> none.

comment(<<"coding", Rest/binary>>, N, InComment) -> separator(Rest, N, InComment);
comment(<<"\n", Rest/binary>>, N, InComment) -> line(Rest, N + 1, InComment);
comment(<<_, Rest/binary>>, N, InComment) -> comment(Rest, N, InComment);
comment(<<>>, _, _) -> none.

%% Spaces, then : or =, then spaces before the name; anything else goes back to looking for "coding".
separator(<<C, Rest/binary>>, _N, _InComment) when C =:= $:; C =:= $= ->
    name(skip_spaces(Rest), [], []);
separator(<<"\s", Rest/binary>>, N, InComment) ->
    separator(Rest, N, InComment);
separator(Bin, N, InComment) ->
    comment(Bin, N, InComment).

skip_spaces(<<"\s", Rest/binary>>) -> skip_spaces(Rest);
skip_spaces(Bin) -> Bin.

%% The name's letters and digits in parts separated by -: latin-1 and utf-8 in any case, with anything after them.
name(<<C, Rest/binary>>, Part, Parts) when C >= $a, C =< $z; C >= $A, C =< $Z; C >= $0, C =< $9 ->
    name(Rest, [C | Part], Parts);
name(<<"-", Rest/binary>>, Part, Parts) ->
    name(Rest, [], [Part | Parts]);
name(_, Part, Parts) ->
    encoding([string:lowercase(lists:reverse(P)) || P <- lists:reverse([Part | Parts])]).

encoding(["latin", "1" | _]) -> latin1;
encoding(["utf", "8" | _]) -> utf8;
encoding(_) -> none.
