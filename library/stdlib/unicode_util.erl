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
-define(IS_LATIN1(CP), (is_integer(CP) andalso CP >= 0 andalso CP =< 255)).
-define(IS_HANGUL(CP), (is_integer(CP) andalso CP >= 16#AC00 andalso CP =< 16#D7A3)).

%% Property bits of clause_unicode:flags/1 (runtime/src/builtins/unicode/tables.hpp).
-define(WIDE, 1).
-define(WHITESPACE, 2).
-define(OTHER_ID_START, 4).
-define(OTHER_ID_CONTINUE, 8).
-define(PATTERN_SYNTAX_LETTER, 16).
%% Grapheme cluster break classes (UAX #29) and Indic conjunct break classes, as OTP's generator groups them.
-define(CONTROL, 32).
-define(EXTEND, 64).
-define(ZWJ, 128).
-define(PREPEND, 256).
-define(REGIONAL, 512).
-define(HANGUL_L, 1024).
-define(HANGUL_V, 2048).
-define(HANGUL_T, 4096).
-define(HANGUL_LV, 8192).
-define(HANGUL_LVT, 16384).
-define(EXT_PICT, 32768).
-define(INDIC_CONSONANT, 65536).
-define(INDIC_LINKER, 131072).

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

%% The first code point of a list and the rest, or binary_found where a binary would have to be decoded.
cp_no_bin([C | _] = L) when is_integer(C) -> L;
cp_no_bin([List]) -> cp_no_bin(List);
cp_no_bin([List | R]) -> cp_no_binl(List, R);
cp_no_bin([]) -> [];
cp_no_bin(_) -> binary_found.

cp_no_binl([C], R) when is_integer(C) -> [C | cpl_1_cont(R)];
cp_no_binl([C | T], R) when is_integer(C) -> [C | cpl_cont(T, R)];
cp_no_binl([List], R) -> cp_no_binl(List, R);
cp_no_binl([List | T], R) -> cp_no_binl(List, [T | R]);
cp_no_binl([], R) -> cp_no_bin(R);
cp_no_binl(_, _) -> binary_found.

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

%% Grapheme clusters (UAX #29, plan step 41O2): the rules in OTP's order, with its result shapes, so the tail after a
%% cluster stays the chardata it came from where OTP keeps it.

%% The first grapheme cluster of chardata (a code point or a list of them) and the rest, [] when it is empty, or
%% {error, Rest} at invalid UTF-8.
gc([] = R) ->
    R;
gc([CP] = R) when ?IS_CP(CP) ->
    R;
gc([$\r | R0] = Str) ->
    case cp(R0) of
        [$\n | R1] -> [[$\r, $\n] | R1];
        _ -> Str
    end;
gc([CP1 | T1] = T) when ?IS_LATIN1(CP1) ->
    case T1 of
        [CP2 | _] when ?IS_LATIN1(CP2) ->
            T;
        _ ->
            case cp_no_bin(T1) of
                [CP2 | _] = T3 when ?IS_LATIN1(CP2) -> [CP1 | T3];
                binary_found -> gc_1(T1, CP1);
                T4 -> gc_1(T4, CP1)
            end
    end;
gc(<<>>) ->
    [];
gc(<<CP1/utf8, Rest/binary>>) when CP1 < 256, CP1 =/= $\r ->
    case Rest of
        <<CP2/utf8, _/binary>> when CP2 < 256 -> [CP1 | Rest];
        _ -> gc_1(Rest, CP1)
    end;
gc(<<CP1/utf8, Rest/binary>>) ->
    gc_1(Rest, CP1);
gc([CP | T]) when ?IS_CP(CP) ->
    gc_1(T, CP);
gc(Str) ->
    case cp(Str) of
        {error, _} = Error -> Error;
        CPs -> gc(CPs)
    end.

%% The cluster that starts with CP, followed by R0.
gc_1(R0, $\r) ->
    case cp(R0) of
        [$\n | R1] -> [[$\r, $\n] | R1];
        _ -> [$\r | R0]
    end;
gc_1(R0, CP) when ?IS_LATIN1(CP) ->
    gc_latin1(R0, CP, clause_unicode:flags(CP));
gc_1(_, CP) when not ?IS_CP(CP) ->
    erlang:error({badarg, CP});
gc_1(R0, CP) ->
    gc_class(R0, CP, clause_unicode:flags(CP)).

gc_latin1(R0, CP, Flags) when Flags band ?CONTROL =/= 0 -> [CP | R0];
gc_latin1(R0, CP, Flags) when Flags band ?EXT_PICT =/= 0 -> gc_ext_pict(R0, [CP]);
gc_latin1([CP2 | _] = R0, CP, _) when ?IS_LATIN1(CP2) -> [CP | R0];
gc_latin1(R0, CP, _) -> gc_extend(cp(R0), R0, CP).

gc_class(R0, CP, Flags) when Flags band ?CONTROL =/= 0 -> [CP | R0];
gc_class(R0, CP, Flags) when Flags band ?PREPEND =/= 0 -> gc_prepend(R0, CP);
gc_class(R0, CP, Flags) when Flags band ?HANGUL_L =/= 0 -> gc_h_L(R0, [CP]);
gc_class(R0, CP, Flags) when Flags band ?HANGUL_V =/= 0 -> gc_h_V(R0, [CP]);
gc_class(R0, CP, Flags) when Flags band ?HANGUL_T =/= 0 -> gc_h_T(R0, [CP]);
gc_class(R0, CP, _) when CP >= 44000, CP =< 56000 ->
    R = [CP | R0],
    gc_h_lv_lvt(R, R, []);
gc_class(R0, CP, Flags) when Flags band ?REGIONAL =/= 0 -> gc_regional(R0, CP);
gc_class(R0, CP, Flags) when Flags band ?EXT_PICT =/= 0 -> gc_ext_pict(R0, [CP]);
gc_class(R0, CP, Flags) when Flags band ?INDIC_CONSONANT =/= 0 -> gc_indic(cp(R0), R0, false, [CP]);
gc_class(R0, CP, _) ->
    gc_extend(cp(R0), R0, CP).

%% A prepended code point joins the cluster after it unless a control follows.
gc_prepend(R00, CP0) ->
    case cp(R00) of
        [CP1 | R0] ->
            case has(CP1, ?CONTROL) of
                true ->
                    [CP0 | R00];
                false ->
                    case gc_1(R0, CP1) of
                        [GC | R1] when is_integer(GC) -> [[CP0, GC] | R1];
                        [GC | R1] -> [[CP0 | GC] | R1]
                    end
            end;
        [] ->
            [CP0];
        {error, R} ->
            [CP0 | R]
    end.

%% zwj, true for the other Extend and SpacingMark code points, or false.
is_extend(CP) ->
    Flags = clause_unicode:flags(CP),
    if
        Flags band ?ZWJ =/= 0 -> zwj;
        Flags band ?EXTEND =/= 0 -> true;
        true -> false
    end.

%% Extend code points after CP0; the tail stays T0 when none follows.
gc_extend([CP | T], T0, CP0) ->
    case is_extend(CP) of
        false -> [CP0 | T0];
        _ -> gc_extend2(cp(T), T, [CP, CP0])
    end;
gc_extend([], T0, CP) ->
    [CP | T0];
gc_extend({error, R}, _, CP) ->
    [CP | R].

gc_extend2([CP | T], T0, Acc) ->
    case is_extend(CP) of
        false -> [lists:reverse(Acc) | T0];
        _ -> gc_extend2(cp(T), T, [CP | Acc])
    end;
gc_extend2([], T0, Acc) ->
    [lists:reverse(Acc) | T0];
gc_extend2({error, R}, _, Acc) ->
    [lists:reverse(Acc)] ++ [R].

%% An extended pictographic sequence: extends, and ZWJ followed by another pictographic.
gc_ext_pict(T, Acc) ->
    gc_ext_pict(cp(T), T, Acc).

gc_ext_pict([CP | R1], T0, Acc) ->
    case is_extend(CP) of
        zwj -> gc_ext_pict_zwj(cp(R1), R1, [CP | Acc]);
        true -> gc_ext_pict(R1, [CP | Acc]);
        false -> add_acc(Acc, T0)
    end;
gc_ext_pict([], T0, Acc) ->
    add_acc(Acc, T0);
gc_ext_pict({error, R}, T, Acc) ->
    gc_ext_pict([], T, Acc) ++ [R].

gc_ext_pict_zwj([CP | R1], T0, Acc) ->
    case has(CP, ?EXT_PICT) of
        true -> gc_ext_pict(R1, [CP | Acc]);
        false -> add_acc(Acc, T0)
    end;
gc_ext_pict_zwj([], T0, Acc) ->
    add_acc(Acc, T0);
gc_ext_pict_zwj({error, R}, T, Acc) ->
    gc_ext_pict_zwj([], T, Acc) ++ [R].

%% Regional indicators pair up.
gc_regional(R0, CP0) ->
    case cp(R0) of
        [CP | R1] ->
            case has(CP, ?REGIONAL) of
                true -> gc_extend2(cp(R1), R1, [CP, CP0]);
                false -> gc_extend([CP | R1], R0, CP0)
            end;
        R1 ->
            gc_extend(R1, R0, CP0)
    end.

%% Hangul syllables: L* (V+ | LV V* | LVT) T*.
gc_h_L(R0, Acc) ->
    case cp(R0) of
        [CP | R1] = R ->
            Flags = clause_unicode:flags(CP),
            if
                Flags band ?HANGUL_L =/= 0 -> gc_h_L(R1, [CP | Acc]);
                Flags band ?HANGUL_V =/= 0 -> gc_h_V(R1, [CP | Acc]);
                true -> gc_h_lv_lvt(R, R0, Acc)
            end;
        R ->
            gc_h_lv_lvt(R, R0, Acc)
    end.

gc_h_V(R0, Acc) ->
    case cp(R0) of
        [CP | R1] = R ->
            Flags = clause_unicode:flags(CP),
            if
                Flags band ?HANGUL_V =/= 0 -> gc_h_V(R1, [CP | Acc]);
                Flags band ?HANGUL_T =/= 0 -> gc_h_T(R1, [CP | Acc]);
                true -> gc_h_end(R, R0, Acc)
            end;
        R ->
            gc_h_end(R, R0, Acc)
    end.

gc_h_T(R0, Acc) ->
    case cp(R0) of
        [CP | R1] = R ->
            case has(CP, ?HANGUL_T) of
                true -> gc_h_T(R1, [CP | Acc]);
                false -> gc_h_end(R, R0, Acc)
            end;
        R ->
            gc_h_end(R, R0, Acc)
    end.

gc_h_end(R1, R0, [CP]) -> gc_extend(R1, R0, CP);
gc_h_end(R1, R0, Acc) -> gc_extend2(R1, R0, Acc).

gc_h_lv_lvt([CP | _], _, _) when not ?IS_CP(CP) ->
    erlang:error(badarg);
gc_h_lv_lvt([CP | R1] = R, R0, Acc) ->
    Flags = clause_unicode:flags(CP),
    if
        Flags band ?HANGUL_LV =/= 0 -> gc_h_V(R1, [CP | Acc]);
        Flags band ?HANGUL_LVT =/= 0 -> gc_h_T(R1, [CP | Acc]);
        Acc =:= [] -> gc_extend(cp(R1), R1, CP);
        true -> gc_h_end(R, R0, Acc)
    end;
gc_h_lv_lvt(R1, R0, Acc) ->
    gc_h_end(R1, R0, Acc).

%% Indic conjuncts: consonants joined by linkers, with extends between them.
gc_indic([CP | R1], R0, FetchedLinker, CPs) ->
    case has(CP, ?INDIC_LINKER) of
        true ->
            gc_indic(cp(R1), R1, true, [CP | CPs]);
        false ->
            case is_extend(CP) of
                false when FetchedLinker ->
                    case has(CP, ?INDIC_CONSONANT) of
                        true -> gc_indic(cp(R1), R1, false, [CP | CPs]);
                        false -> add_acc(CPs, R0)
                    end;
                false ->
                    add_acc(CPs, R0);
                _ ->
                    gc_indic(cp(R1), R1, FetchedLinker, [CP | CPs])
            end
    end;
gc_indic([], R0, _, CPs) ->
    add_acc(CPs, R0);
gc_indic({error, R0}, _, _, CPs) ->
    add_acc(CPs, R0).

add_acc([CP], R) -> [CP | R];
add_acc(CPs, R) -> [lists:reverse(CPs) | R].

%% Normalization (plan step 41O2): one grapheme cluster at a time, as OTP normalizes.

nfd(Str0) ->
    case gc(Str0) of
        [GC | R] when ?IS_ASCII(GC) -> [GC | R];
        [GC | Str] -> [decompose(GC) | Str];
        [] -> [];
        {error, _} = Error -> Error
    end.

nfkd(Str0) ->
    case gc(Str0) of
        [GC | R] when ?IS_ASCII(GC) -> [GC | R];
        [GC | Str] -> [decompose_compat(GC) | Str];
        [] -> [];
        {error, _} = Error -> Error
    end.

nfc(Str0) ->
    case gc(Str0) of
        [GC | R] when ?IS_LATIN1(GC) -> [GC | R];
        [GC | Str] -> [compose(decompose(GC)) | Str];
        [] -> [];
        {error, _} = Error -> Error
    end.

nfkc(Str0) ->
    case gc(Str0) of
        [GC | R] when ?IS_ASCII(GC) -> [GC | R];
        [GC | Str] -> [compose_compat_0(decompose_compat(GC)) | Str];
        [] -> [];
        {error, _} = Error -> Error
    end.

%% The canonical decomposition of a code point or cluster, canonically ordered.
decompose(CP) when is_integer(CP), not ?IS_HANGUL(CP) ->
    case clause_unicode:table(CP) of
        {_, [], _, _} -> CP;
        {_, CPs, _, _} -> canonical_order(CPs)
    end;
decompose(CP) ->
    canonical_order(decompose_1(CP)).

%% [{CCC, CP}] of a code point or cluster.
decompose_1(CP) when ?IS_HANGUL(CP) ->
    hangul_jamo(CP);
decompose_1(CP) when is_integer(CP) ->
    case clause_unicode:table(CP) of
        {CCC, [], _, _} -> [{CCC, CP}];
        {_, CPs, _, _} -> CPs
    end;
decompose_1([CP | CPs]) ->
    decompose_1(CP) ++ decompose_1(CPs);
decompose_1([]) ->
    [].

%% The compatibility decomposition, canonically ordered.
decompose_compat(CP) when is_integer(CP), not ?IS_HANGUL(CP) ->
    case clause_unicode:table(CP) of
        {_, [], [], _} -> CP;
        {_, _, {_, CPs}, _} -> canonical_order(CPs);
        {_, CPs, _, _} -> canonical_order(CPs)
    end;
decompose_compat(CP) ->
    canonical_order(decompose_compat_1(CP)).

decompose_compat_1(CP) when ?IS_HANGUL(CP) ->
    hangul_jamo(CP);
decompose_compat_1(CP) when is_integer(CP) ->
    case clause_unicode:table(CP) of
        {CCC, [], [], _} -> [{CCC, CP}];
        {_, _, {_, CPs}, _} -> CPs;
        {_, CPs, _, _} -> CPs
    end;
decompose_compat_1([CP | CPs]) ->
    decompose_compat_1(CP) ++ decompose_compat_1(CPs);
decompose_compat_1([]) ->
    [].

%% The jamo of a precomposed Hangul syllable.
hangul_jamo(CP) ->
    Syllable = CP - 16#AC00,
    Lead = 16#1100 + Syllable div 588,
    Vowel = 16#1161 + (Syllable rem 588) div 28,
    case Syllable rem 28 of
        0 -> [{0, Lead}, {0, Vowel}];
        Trail -> [{0, Lead}, {0, Vowel}, {0, Trail + 16#11A7}]
    end.

%% Code points with runs of non-starters sorted by combining class (stable).
canonical_order([{_, CP}]) -> CP;
canonical_order(CPs) -> canonical_order_1(CPs).

canonical_order_1([{0, CP} | TaggedCPs]) -> [CP | canonical_order_1(TaggedCPs)];
canonical_order_1([_ | _] = TaggedCPs) -> canonical_order_2(TaggedCPs, []);
canonical_order_1([]) -> [].

canonical_order_2([{CCC, _} = First | Cont], Seq) when CCC > 0 ->
    canonical_order_2(Cont, [First | Seq]);
canonical_order_2(Cont, Seq) ->
    [CP || {_, CP} <- lists:keysort(1, lists:reverse(Seq))] ++ canonical_order_1(Cont).

%% The canonical composition of a decomposed cluster.
compose(CP) when is_integer(CP) ->
    CP;
compose([Lead, Vowel | Trail]) when
    is_integer(Lead), Lead >= 16#1100, Lead =< 16#1112, is_integer(Vowel)
->
    compose_hangul(Lead, Vowel, Trail);
compose([Base, Accent] = GC0) ->
    case clause_unicode:compose_pair(Base, Accent) of
        false -> GC0;
        GC -> GC
    end;
compose([CP | Many]) ->
    compose_many(Many, CP, [], clause_unicode:class(CP)).

%% A leading Hangul syllable composed from its jamo, or the lead kept before the composed rest.
compose_hangul(Lead, Vowel, Trail) when Vowel >= 16#1161, Vowel =< 16#1175 ->
    CP = 16#AC00 + ((Lead - 16#1100) * 588) + ((Vowel - 16#1161) * 28),
    case Trail of
        [T | Acc] when is_integer(T), T >= 16#11A7, T =< 16#11C2 -> nolist(CP + T - 16#11A7, Acc);
        Acc -> nolist(CP, Acc)
    end;
compose_hangul(Lead, Vowel, Trail) ->
    case compose([Vowel | Trail]) of
        [_ | _] = CPs -> [Lead | CPs];
        CP -> [Lead, CP]
    end.

compose_many([CP | Rest], Base, Accents, Prev) ->
    Class = clause_unicode:class(CP),
    case (Prev =:= 0 orelse Prev < Class) andalso clause_unicode:compose_pair(Base, CP) of
        false when Class =:= 0 ->
            Begin = [Base | lists:reverse(Accents)],
            case compose_many(Rest, CP, [], 0) of
                [_ | _] = GC -> Begin ++ GC;
                Composed -> Begin ++ [Composed]
            end;
        false ->
            compose_many(Rest, Base, [CP | Accents], Class);
        Combined ->
            compose_many(Rest, Combined, Accents, Prev)
    end;
compose_many([], Base, [], _) ->
    Base;
compose_many([], Base, Accents, _) ->
    [Base | lists:reverse(Accents)].

%% The compatibility composition: the canonical composition of every cluster of the decomposition.
compose_compat_0(CP) when is_integer(CP) ->
    CP;
compose_compat_0(L) ->
    case gc(L) of
        [First | Rest] ->
            case compose(First) of
                [_ | _] = GC -> GC ++ compose_compat_0(Rest);
                CP -> [CP | compose_compat_0(Rest)]
            end;
        [] ->
            []
    end.

nolist(CP, []) -> CP;
nolist(CP, L) -> [CP | L].
