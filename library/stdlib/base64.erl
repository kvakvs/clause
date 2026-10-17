%% Clause's base64 (plan 21 step LIB5, docs/builtins.md#encodings): every function is a runtime builtin with a
%% clause_builtin_placeholder body.
-module(base64).
-export([
    decode/1,
    decode/2,
    decode_to_string/1,
    decode_to_string/2,
    encode/1,
    encode/2,
    encode_to_string/1,
    encode_to_string/2,
    mime_decode/1,
    mime_decode/2,
    mime_decode_to_string/1,
    mime_decode_to_string/2
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
    format_error/2
]).

decode(_) -> clause_builtin_placeholder.
decode(_, _) -> clause_builtin_placeholder.
decode_to_string(_) -> clause_builtin_placeholder.
decode_to_string(_, _) -> clause_builtin_placeholder.
encode(_) -> clause_builtin_placeholder.
encode(_, _) -> clause_builtin_placeholder.
encode_to_string(_) -> clause_builtin_placeholder.
encode_to_string(_, _) -> clause_builtin_placeholder.
mime_decode(_) -> clause_builtin_placeholder.
mime_decode(_, _) -> clause_builtin_placeholder.
mime_decode_to_string(_) -> clause_builtin_placeholder.
mime_decode_to_string(_, _) -> clause_builtin_placeholder.

format_error(_, _) -> erlang:error({not_implemented, format_error, 2}).
