%% OTP's xmerl_ucs as libraries call it (plan 21 step MIM3, docs/libraries/xmerl.md#xmerl_ucs): UTF-8 decoding and
%% encoding, UTF-16BE decoding and the Unicode code point check, with OTP's errors. Its other charset functions are
%% declined.
-module(xmerl_ucs).
-export([from_utf16be/1, from_utf8/1, is_unicode/1, to_utf8/1]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    from_latin9/1,
    from_ucs2be/1,
    from_ucs2be/2,
    from_ucs2le/1,
    from_ucs2le/2,
    from_ucs4be/1,
    from_ucs4be/2,
    from_ucs4le/1,
    from_ucs4le/2,
    from_utf16be/2,
    from_utf16le/1,
    from_utf16le/2,
    is_ascii/1,
    is_bmpchar/1,
    is_incharset/2,
    is_iso10646/1,
    is_iso646_basic/1,
    is_latin1/1,
    is_visible_ascii/1,
    is_visible_latin1/1,
    to_ucs2be/1,
    to_ucs2le/1,
    to_ucs4be/1,
    to_ucs4le/1,
    to_unicode/2,
    to_utf16be/1,
    to_utf16le/1
]).

% UTF-8 bytes (a list or binary) as code points; exit({ucs, {bad_utf8_character_code}}) on a bad sequence. Like
% OTP's, it takes surrogates and code points above 16#10FFFF of four bytes.
from_utf8(Bin) when is_binary(Bin) ->
    from_utf8(binary_to_list(Bin));
from_utf8(Bytes) when is_list(Bytes) ->
    utf8(Bytes, []).

utf8([], Acc) ->
    lists:reverse(Acc);
utf8([B | Rest], Acc) when B >= 0, B < 16#80 ->
    utf8(Rest, [B | Acc]);
utf8([B | Rest], Acc) when B >= 16#C0, B < 16#E0 ->
    continued(Rest, 1, B band 16#1F, 16#80, Acc);
utf8([B | Rest], Acc) when B >= 16#E0, B < 16#F0 ->
    continued(Rest, 2, B band 16#0F, 16#800, Acc);
utf8([B | Rest], Acc) when B >= 16#F0, B < 16#F8 ->
    continued(Rest, 3, B band 16#07, 16#10000, Acc);
utf8(_, _) ->
    bad_utf8().

% N continuation bytes after a lead byte's bits; the code point must need them (no overlong form).
continued(Rest, 0, Code, Least, Acc) when Code >= Least ->
    utf8(Rest, [Code | Acc]);
continued([B | Rest], N, Code, Least, Acc) when N > 0, B >= 16#80, B < 16#C0 ->
    continued(Rest, N - 1, (Code bsl 6) bor (B band 16#3F), Least, Acc);
continued(_, _, _, _, _) ->
    bad_utf8().

bad_utf8() ->
    exit({ucs, {bad_utf8_character_code}}).

% Code points (a list, nested lists flattened, or one) as UTF-8 bytes; if_clause for a surrogate, 16#FFFE or
% 16#FFFF, as OTP's.
to_utf8(List) when is_list(List) ->
    lists:flatmap(fun to_utf8/1, List);
to_utf8(Code) when is_integer(Code), Code >= 0 ->
    char_to_utf8(Code);
to_utf8(Other) ->
    erlang:error(function_clause, [Other]).

char_to_utf8(Code) when Code < 16#80 ->
    [Code];
char_to_utf8(Code) when Code < 16#800 ->
    [16#C0 bor (Code bsr 6), 16#80 bor (Code band 16#3F)];
char_to_utf8(Code) when Code < 16#10000 ->
    if
        Code < 16#D800; Code > 16#DFFF, Code < 16#FFFE ->
            [16#E0 bor (Code bsr 12) | tail(Code, 2)]
    end;
char_to_utf8(Code) when Code < 16#200000 ->
    [16#F0 bor (Code bsr 18) | tail(Code, 3)];
char_to_utf8(Code) when Code < 16#4000000 ->
    [16#F8 bor (Code bsr 24) | tail(Code, 4)];
char_to_utf8(Code) when Code < 16#80000000 ->
    [16#FC bor (Code bsr 30) | tail(Code, 5)];
char_to_utf8(Code) ->
    erlang:error(function_clause, [Code]).

% The N continuation bytes of a code point.
tail(Code, N) ->
    [16#80 bor ((Code bsr (6 * I)) band 16#3F) || I <- lists:seq(N - 1, 0, -1)].

is_unicode(C) when is_integer(C), C >= 0, C < 16#D800 -> true;
is_unicode(C) when is_integer(C), C > 16#DFFF, C < 16#FFFE -> true;
is_unicode(C) when is_integer(C), C >= 16#10000, C =< 16#10FFFF -> true;
is_unicode(_) -> false.

% UTF-16BE bytes (a list or binary) as code points; {error, not_utf16be} on a lone surrogate or odd byte, reported
% as OTP's reports it; if_clause for 16#FFFE or 16#FFFF.
from_utf16be(Bin) when is_binary(Bin) ->
    utf16be(Bin, []);
from_utf16be(List) ->
    utf16be(list_to_binary(List), []).

utf16be(<<>>, Acc) ->
    lists:reverse(Acc);
utf16be(<<High:16, Low:16, Rest/binary>>, Acc) when High >= 16#D800, High < 16#DC00, Low >= 16#DC00, Low < 16#E000 ->
    utf16be(Rest, [16#10000 + ((High - 16#D800) bsl 10) + (Low - 16#DC00) | Acc]);
utf16be(<<Unit:16, Rest/binary>>, Acc) when Unit < 16#D800; Unit > 16#DFFF ->
    if
        Unit < 16#FFFE -> utf16be(Rest, [Unit | Acc])
    end;
utf16be(Bin, Acc) ->
    error_logger:error_msg("xmerl_ucs: Bin=~p~n     Acc=~p~n     Tail=~p~n", [Bin, Acc, []]),
    {error, not_utf16be}.

from_latin9(_) -> erlang:error({not_implemented, from_latin9, 1}).
from_ucs2be(_) -> erlang:error({not_implemented, from_ucs2be, 1}).
from_ucs2be(_, _) -> erlang:error({not_implemented, from_ucs2be, 2}).
from_ucs2le(_) -> erlang:error({not_implemented, from_ucs2le, 1}).
from_ucs2le(_, _) -> erlang:error({not_implemented, from_ucs2le, 2}).
from_ucs4be(_) -> erlang:error({not_implemented, from_ucs4be, 1}).
from_ucs4be(_, _) -> erlang:error({not_implemented, from_ucs4be, 2}).
from_ucs4le(_) -> erlang:error({not_implemented, from_ucs4le, 1}).
from_ucs4le(_, _) -> erlang:error({not_implemented, from_ucs4le, 2}).
from_utf16be(_, _) -> erlang:error({not_implemented, from_utf16be, 2}).
from_utf16le(_) -> erlang:error({not_implemented, from_utf16le, 1}).
from_utf16le(_, _) -> erlang:error({not_implemented, from_utf16le, 2}).
is_ascii(_) -> erlang:error({not_implemented, is_ascii, 1}).
is_bmpchar(_) -> erlang:error({not_implemented, is_bmpchar, 1}).
is_incharset(_, _) -> erlang:error({not_implemented, is_incharset, 2}).
is_iso10646(_) -> erlang:error({not_implemented, is_iso10646, 1}).
is_iso646_basic(_) -> erlang:error({not_implemented, is_iso646_basic, 1}).
is_latin1(_) -> erlang:error({not_implemented, is_latin1, 1}).
is_visible_ascii(_) -> erlang:error({not_implemented, is_visible_ascii, 1}).
is_visible_latin1(_) -> erlang:error({not_implemented, is_visible_latin1, 1}).
to_ucs2be(_) -> erlang:error({not_implemented, to_ucs2be, 1}).
to_ucs2le(_) -> erlang:error({not_implemented, to_ucs2le, 1}).
to_ucs4be(_) -> erlang:error({not_implemented, to_ucs4be, 1}).
to_ucs4le(_) -> erlang:error({not_implemented, to_ucs4le, 1}).
to_unicode(_, _) -> erlang:error({not_implemented, to_unicode, 2}).
to_utf16be(_) -> erlang:error({not_implemented, to_utf16be, 1}).
to_utf16le(_) -> erlang:error({not_implemented, to_utf16le, 1}).
