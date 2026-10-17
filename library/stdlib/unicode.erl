%% Clause's replacement for OTP's unicode (docs/library.md#runtime-module-replacements), generated from
%% unicode.terms by tests/compiler/library/library_modules.py; edited by hand from here on. Every function is a
%% runtime builtin that replaces its placeholder: the conversions, bin_is_7bit/1 and the byte order mark functions
%% (plan step 41Y1, docs/builtins.md#unicode-module), the normalization forms, categories and identifier properties
%% (plan 21 step LIB4B).
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
category(_) -> clause_builtin_placeholder.
characters_to_binary(_) -> clause_builtin_placeholder.
characters_to_binary(_, _) -> clause_builtin_placeholder.
characters_to_binary(_, _, _) -> clause_builtin_placeholder.
characters_to_binary_int(_, _) -> clause_builtin_placeholder.
characters_to_list(_) -> clause_builtin_placeholder.
characters_to_list(_, _) -> clause_builtin_placeholder.
characters_to_list_int(_, _) -> clause_builtin_placeholder.
characters_to_nfc_binary(_) -> clause_builtin_placeholder.
characters_to_nfc_list(_) -> clause_builtin_placeholder.
characters_to_nfd_binary(_) -> clause_builtin_placeholder.
characters_to_nfd_list(_) -> clause_builtin_placeholder.
characters_to_nfkc_binary(_) -> clause_builtin_placeholder.
characters_to_nfkc_list(_) -> clause_builtin_placeholder.
characters_to_nfkd_binary(_) -> clause_builtin_placeholder.
characters_to_nfkd_list(_) -> clause_builtin_placeholder.
encoding_to_bom(_) -> clause_builtin_placeholder.
is_id_continue(_) -> clause_builtin_placeholder.
is_id_start(_) -> clause_builtin_placeholder.
is_whitespace(_) -> clause_builtin_placeholder.
