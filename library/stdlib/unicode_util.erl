%% Clause's replacement for OTP's unicode_util (docs/library.md#runtime-module-replacements), generated from
%% unicode_util.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}).
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

casefold(_) -> erlang:error({not_implemented, casefold, 1}).
category(_) -> erlang:error({not_implemented, category, 1}).
cp(_) -> erlang:error({not_implemented, cp, 1}).
gc(_) -> erlang:error({not_implemented, gc, 1}).
get_case(_) -> erlang:error({not_implemented, get_case, 1}).
is_letter_not_pattern_syntax(_) -> erlang:error({not_implemented, is_letter_not_pattern_syntax, 1}).
is_other_id_continue(_) -> erlang:error({not_implemented, is_other_id_continue, 1}).
is_other_id_start(_) -> erlang:error({not_implemented, is_other_id_start, 1}).
is_whitespace(_) -> erlang:error({not_implemented, is_whitespace, 1}).
is_wide(_) -> erlang:error({not_implemented, is_wide, 1}).
lookup(_) -> erlang:error({not_implemented, lookup, 1}).
lowercase(_) -> erlang:error({not_implemented, lowercase, 1}).
nfc(_) -> erlang:error({not_implemented, nfc, 1}).
nfd(_) -> erlang:error({not_implemented, nfd, 1}).
nfkc(_) -> erlang:error({not_implemented, nfkc, 1}).
nfkd(_) -> erlang:error({not_implemented, nfkd, 1}).
pattern_whitespace() -> erlang:error({not_implemented, pattern_whitespace, 0}).
spec_version() -> erlang:error({not_implemented, spec_version, 0}).
titlecase(_) -> erlang:error({not_implemented, titlecase, 1}).
uppercase(_) -> erlang:error({not_implemented, uppercase, 1}).
