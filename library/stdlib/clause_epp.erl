%% Erlang sources as epp and file:consult/1 read them (plan 21 steps LIB33, LIB34, docs/libraries/library.md): the encoding a
%% coding comment names, a file's characters decoded by it, and the next form scanned from them as io:scan_erl_form/4
%% reads it from OTP's file server.
-module(clause_epp).
-export([read_encoding_from_binary/2, read_all/1, source/2, scan/3, eval_if/1]).

%% A source being scanned: {Chars, valid}, {Chars, invalid} when only Chars could be decoded, or terminated once a
%% read reached the bytes that could not (OTP's file server ends then).

%% read_all(Fd): {ok, Bytes} of the rest of an open file, or its read error.
read_all(Fd) -> read_all(Fd, []).

read_all(Fd, Acc) ->
    case file:read(Fd, 65536) of
        {ok, Data} -> read_all(Fd, [Data | Acc]);
        eof -> {ok, iolist_to_binary(lists:reverse(Acc))};
        Error -> Error
    end.

%% source(Bytes, Default): {Encoding, Source}, Encoding the one a coding comment in the first 512 bytes names (or
%% none), the bytes decoded by it or else by Default.
source(Bytes, Default) ->
    Named = read_encoding_from_binary(binary:part(Bytes, 0, min(512, byte_size(Bytes))), []),
    Encoding =
        case Named of
            none -> Default;
            _ -> Named
        end,
    {Named, decode(Bytes, Encoding)}.

decode(Bytes, latin1) ->
    {binary_to_list(Bytes), valid};
decode(Bytes, utf8) ->
    case unicode:characters_to_list(Bytes, utf8) of
        Chars when is_list(Chars) -> {Chars, valid};
        {_, Chars, _} -> {Chars, invalid}
    end.

%% scan(Source, Location, Options): {Result, Source1}, Result as io:scan_erl_form(Fd, '', Location, Options) gives
%% it: {ok, Tokens, End}, {error, ErrorInfo, End}, {eof, End}, or {error, Reason} when the read fails.
scan(terminated, _Location, _Options) ->
    {{error, terminated}, terminated};
scan({[], invalid}, _Location, _Options) ->
    %% The read starts at the bytes that cannot be decoded: OTP's server has no scan state to locate the error by.
    {{error, tokens}, terminated};
scan({Chars, Validity}, Location, Options) ->
    case erl_scan:tokens([], Chars, Location, Options) of
        {done, Result, Rest} ->
            {Result, {Rest, Validity}};
        {more, Cont} when Validity =:= valid ->
            {done, Result, Rest} = erl_scan:tokens(Cont, eof, Location, Options),
            {Result, {Rest, valid}};
        {more, Cont} ->
            At = erl_scan:continuation_location(Cont),
            {{error, {At, file_io_server, invalid_unicode}, At}, terminated}
    end.

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

%% eval_if(Expr): the value of a -if or -elif guard expression, false when it raises (a runtime builtin,
%% runtime/src/builtins/front_end/epp_eval.cpp).
eval_if(_) -> clause_builtin_placeholder.
