%% Clause's uri_string (plan 21 steps LIB5D and LIB5G, docs/builtins.md#uri-string): every function is a runtime
%% builtin with a clause_builtin_placeholder body: RFC 3986 parsing with OTP's error fragments, recomposition,
%% normalization, reference resolution, percent coding and the form-urlencoded query helpers.
-module(uri_string).
-export([
    allowed_characters/0,
    compose_query/1,
    compose_query/2,
    dissect_query/1,
    is_host/1,
    is_path/1,
    normalize/1,
    normalize/2,
    parse/1,
    percent_decode/1,
    quote/1,
    quote/2,
    recompose/1,
    resolve/2,
    resolve/3,
    transcode/2,
    unquote/1
]).

-include("uri_string.types.hrl").

allowed_characters() -> clause_builtin_placeholder.
compose_query(_) -> clause_builtin_placeholder.
compose_query(_, _) -> clause_builtin_placeholder.
dissect_query(_) -> clause_builtin_placeholder.
is_host(_) -> clause_builtin_placeholder.
is_path(_) -> clause_builtin_placeholder.
normalize(_) -> clause_builtin_placeholder.
normalize(_, _) -> clause_builtin_placeholder.
parse(_) -> clause_builtin_placeholder.
percent_decode(_) -> clause_builtin_placeholder.
quote(_) -> clause_builtin_placeholder.
quote(_, _) -> clause_builtin_placeholder.
recompose(_) -> clause_builtin_placeholder.
resolve(_, _) -> clause_builtin_placeholder.
resolve(_, _, _) -> clause_builtin_placeholder.
transcode(_, _) -> clause_builtin_placeholder.
unquote(_) -> clause_builtin_placeholder.
