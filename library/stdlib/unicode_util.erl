%% Clause's replacement for OTP's unicode_util (docs/library.md#runtime-module-replacements). Code points,
%% categories, decompositions, case mappings and properties come from the generated tables of Clause's private
%% clause_unicode module (plan step 41O1, docs/builtins.md#unicode-util); the walks over chardata follow OTP's
%% result shapes. A stub raises error({not_implemented, Name, Arity}).
-module(unicode_util).
-export([
    casefold/1,
    category/1,
    cp/1,
    gc/1,
    get_case/1,
    is_letter_not_pattern_syntax/1,
    is_other_id_continue/1,
    is_other_id_start/1,
    is_whitespace/1,
    is_wide/1,
    lookup/1,
    lowercase/1,
    nfc/1,
    nfd/1,
    nfkc/1,
    nfkd/1,
    pattern_whitespace/0,
    spec_version/0,
    titlecase/1,
    uppercase/1
]).

-define(IS_CP(CP), (is_integer(CP) andalso CP >= 0 andalso CP =< 16#10FFFF)).
-define(IS_ASCII(CP), (is_integer(CP) andalso CP >= 0 andalso CP =< 127)).

%% Property bits of clause_unicode:flags/1 (runtime/src/builtins/unicode/tables.hpp).
-define(WIDE, 1).
-define(WHITESPACE, 2).
-define(OTHER_ID_START, 4).
-define(OTHER_ID_CONTINUE, 8).
-define(PATTERN_SYNTAX_LETTER, 16).

%% Whether a code point has a property bit.
has(CP, Bit) -> clause_unicode:flags(CP) band Bit =/= 0.

%% Code points

%% The first code point of chardata and the rest, [] when it is empty, or {error, Rest} at invalid UTF-8.
cp([C | _] = L) when ?IS_CP(C) -> L;
cp([List]) -> cp(List);
cp([List | R]) -> cpl(List, R);
cp([]) -> [];
cp(<<C/utf8, R/binary>>) -> [C | R];
cp(<<>>) -> [];
cp(<<R/binary>>) -> {error, R}.

cpl([C], R) when ?IS_CP(C) -> [C | cpl_1_cont(R)];
cpl([C | T], R) when ?IS_CP(C) -> [C | cpl_cont(T, R)];
cpl([List], R) -> cpl(List, R);
cpl([List | T], R) -> cpl(List, [T | R]);
cpl([], R) -> cp(R);
cpl(<<C/utf8, T/binary>>, R) -> [C, T | R];
cpl(<<>>, R) -> cp(R);
cpl(<<B/binary>>, R) -> {error, [B | R]}.

%% The rest after a code point taken from a nested list: up to three more code points are flattened.
cpl_cont([C | T], R) when is_integer(C) -> [C | cpl_cont2(T, R)];
cpl_cont([L], R) -> cpl_cont(L, R);
cpl_cont([L | T], R) -> cpl_cont(L, [T | R]);
cpl_cont([], R) -> cpl_1_cont(R);
cpl_cont(T, R) -> [T | R].

cpl_cont2([C | T], R) when is_integer(C) -> [C | cpl_cont3(T, R)];
cpl_cont2([L], R) -> cpl_cont2(L, R);
cpl_cont2([L | T], R) -> cpl_cont2(L, [T | R]);
cpl_cont2([], R) -> cpl_1_cont2(R);
cpl_cont2(T, R) -> [T | R].

cpl_cont3([C], R) when is_integer(C) -> [C | R];
cpl_cont3([C | T], R) when is_integer(C) -> [C, T | R];
cpl_cont3([L], R) -> cpl_cont3(L, R);
cpl_cont3([L | T], R) -> cpl_cont3(L, [T | R]);
cpl_cont3([], R) -> cpl_1_cont3(R);
cpl_cont3(T, R) -> [T | R].

cpl_1_cont([C | T]) when is_integer(C) -> [C | cpl_1_cont2(T)];
cpl_1_cont([L]) -> cpl_1_cont(L);
cpl_1_cont([L | T]) -> cpl_cont(L, T);
cpl_1_cont(T) -> T.

cpl_1_cont2([C | T]) when is_integer(C) -> [C | cpl_1_cont3(T)];
cpl_1_cont2([L]) -> cpl_1_cont2(L);
cpl_1_cont2([L | T]) -> cpl_cont2(L, T);
cpl_1_cont2(T) -> T.

cpl_1_cont3([C | _] = T) when is_integer(C) -> T;
cpl_1_cont3([L]) -> cpl_1_cont3(L);
cpl_1_cont3([L | T]) -> cpl_cont3(L, T);
cpl_1_cont3(T) -> T.

%% Tables

lookup(CP) when ?IS_CP(CP) ->
    {CCC, Canon, Compat, Category} = clause_unicode:table(CP),
    #{ccc => CCC, canon => Canon, compat => Compat, category => Category}.

category(CP) when ?IS_CP(CP) -> element(4, clause_unicode:table(CP)).

get_case(CP) when ?IS_CP(CP) ->
    {Upper, Lower, Title, Fold} = clause_unicode:cases(CP),
    #{upper => Upper, lower => Lower, title => Title, fold => Fold}.

%% The functions with runtime builtins: spec_version/0 and pattern_whitespace/0.
spec_version() -> erlang:error({not_implemented, spec_version, 0}).
pattern_whitespace() -> erlang:error({not_implemented, pattern_whitespace, 0}).

%% Case of the first code point

uppercase(Str) -> map_first(Str, 1).
lowercase(Str) -> map_first(Str, 2).
titlecase(Str) -> map_first(Str, 3).
casefold(Str) -> map_first(Str, 4).

%% The first code point replaced by one of its case mappings, followed by the rest.
map_first(Str0, Which) ->
    case cp(Str0) of
        [CP | Str] -> [element(Which, clause_unicode:cases(CP)) | Str];
        [] -> [];
        {error, Err} -> erlang:error({badarg, Err})
    end.

%% Properties

%% Whether a code point or grapheme cluster is wide outside an East Asian context.
is_wide(C) when ?IS_ASCII(C) -> false;
is_wide(C) when ?IS_CP(C) -> has(C, ?WIDE);
is_wide([_, 16#FE0E | _]) -> true;
is_wide([_, 16#FE0F | _]) -> true;
is_wide([C | Cs]) when ?IS_CP(C) -> has(C, ?WIDE) orelse is_wide(Cs);
is_wide([]) -> false.

is_whitespace([13, 10]) -> true;
is_whitespace(CP) when ?IS_CP(CP) -> has(CP, ?WHITESPACE);
is_whitespace(_) -> false.

is_other_id_start(CP) when ?IS_CP(CP) -> has(CP, ?OTHER_ID_START);
is_other_id_start(_) -> false.

is_other_id_continue(CP) when ?IS_CP(CP) -> has(CP, ?OTHER_ID_CONTINUE);
is_other_id_continue(_) -> false.

is_letter_not_pattern_syntax(CP) when ?IS_CP(CP) -> not has(CP, ?PATTERN_SYNTAX_LETTER);
is_letter_not_pattern_syntax(_) -> true.

%% Grapheme clusters and normalization (plan step 41O2)

gc(_) -> erlang:error({not_implemented, gc, 1}).
nfc(_) -> erlang:error({not_implemented, nfc, 1}).
nfd(_) -> erlang:error({not_implemented, nfd, 1}).
nfkc(_) -> erlang:error({not_implemented, nfkc, 1}).
nfkd(_) -> erlang:error({not_implemented, nfkd, 1}).
