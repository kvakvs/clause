%% Clause's replacement for OTP's unicode (docs/library.md#runtime-module-replacements), generated from
%% unicode.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). The conversions, bin_is_7bit/1 and the byte order mark functions are
%% runtime builtins that replace their placeholders (plan step 41Y1, docs/builtins.md#unicode-module); normalization,
%% categories and identifiers wait for unicode_util (step 41O).
-module(unicode).
-export([
    bin_is_7bit/1,
    bom_to_encoding/1,
    category/1,
    characters_to_binary/1,
    characters_to_binary/2,
    characters_to_binary/3,
    characters_to_binary_int/2,
    characters_to_list/1,
    characters_to_list/2,
    characters_to_list_int/2,
    characters_to_nfc_binary/1,
    characters_to_nfc_list/1,
    characters_to_nfd_binary/1,
    characters_to_nfd_list/1,
    characters_to_nfkc_binary/1,
    characters_to_nfkc_list/1,
    characters_to_nfkd_binary/1,
    characters_to_nfkd_list/1,
    encoding_to_bom/1,
    is_id_continue/1,
    is_id_start/1,
    is_whitespace/1
]).
-include("unicode.types.hrl").

bin_is_7bit(_) -> clause_builtin_placeholder.
bom_to_encoding(_) -> clause_builtin_placeholder.
category(_) -> erlang:error({not_implemented, category, 1}).
characters_to_binary(_) -> clause_builtin_placeholder.
characters_to_binary(_, _) -> clause_builtin_placeholder.
characters_to_binary(_, _, _) -> clause_builtin_placeholder.
characters_to_binary_int(_, _) -> erlang:error({not_implemented, characters_to_binary_int, 2}).
characters_to_list(_) -> clause_builtin_placeholder.
characters_to_list(_, _) -> clause_builtin_placeholder.
characters_to_list_int(_, _) -> erlang:error({not_implemented, characters_to_list_int, 2}).
characters_to_nfc_binary(_) -> erlang:error({not_implemented, characters_to_nfc_binary, 1}).
characters_to_nfc_list(_) -> erlang:error({not_implemented, characters_to_nfc_list, 1}).
characters_to_nfd_binary(_) -> erlang:error({not_implemented, characters_to_nfd_binary, 1}).
characters_to_nfd_list(_) -> erlang:error({not_implemented, characters_to_nfd_list, 1}).
characters_to_nfkc_binary(_) -> erlang:error({not_implemented, characters_to_nfkc_binary, 1}).
characters_to_nfkc_list(_) -> erlang:error({not_implemented, characters_to_nfkc_list, 1}).
characters_to_nfkd_binary(_) -> erlang:error({not_implemented, characters_to_nfkd_binary, 1}).
characters_to_nfkd_list(_) -> erlang:error({not_implemented, characters_to_nfkd_list, 1}).
encoding_to_bom(_) -> clause_builtin_placeholder.
is_id_continue(_) -> erlang:error({not_implemented, is_id_continue, 1}).
is_id_start(_) -> erlang:error({not_implemented, is_id_start, 1}).
is_whitespace(_) -> erlang:error({not_implemented, is_whitespace, 1}).
