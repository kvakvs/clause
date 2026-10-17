%% Clause's replacement for OTP's unicode_util (docs/library.md#runtime-module-replacements). Code points,
%% categories, decompositions, case mappings and properties come from the generated tables of Clause's private
%% clause_unicode module (plan step 41O1, docs/builtins.md#unicode-util). The walks over chardata (cp/1, gc/1, the
%% nf* forms and the case mappings of the first code point) are runtime builtins in OTP's result shapes (plan 21
%% step LIB4B).
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

%% Tables

lookup(CP) when ?IS_CP(CP) ->
    {CCC, Canon, Compat, Category} = clause_unicode:table(CP),
    #{ccc => CCC, canon => Canon, compat => Compat, category => Category}.

category(CP) when ?IS_CP(CP) -> element(4, clause_unicode:table(CP)).

get_case(CP) when ?IS_CP(CP) ->
    {Upper, Lower, Title, Fold} = clause_unicode:cases(CP),
    #{upper => Upper, lower => Lower, title => Title, fold => Fold}.

%% The functions with runtime builtins: spec_version/0 and pattern_whitespace/0.
spec_version() -> clause_builtin_placeholder.
pattern_whitespace() -> clause_builtin_placeholder.

%% The chardata walks with runtime builtins (plan 21 step LIB4B).
cp(_) -> clause_builtin_placeholder.
gc(_) -> clause_builtin_placeholder.
nfd(_) -> clause_builtin_placeholder.
nfkd(_) -> clause_builtin_placeholder.
nfc(_) -> clause_builtin_placeholder.
nfkc(_) -> clause_builtin_placeholder.
uppercase(_) -> clause_builtin_placeholder.
lowercase(_) -> clause_builtin_placeholder.
titlecase(_) -> clause_builtin_placeholder.
casefold(_) -> clause_builtin_placeholder.

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
