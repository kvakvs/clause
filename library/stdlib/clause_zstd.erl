-module(clause_zstd).
-export([
    init_compress/0,
    init_decompress/0,
    load_compress_dictionary/2,
    load_decompress_dictionary/2,
    ref_compress_dictionary/2,
    ref_decompress_dictionary/2,
    set_compress_parameter/3,
    get_compress_parameter/2,
    set_pledged_src_size/2,
    set_decompress_parameter/3,
    get_decompress_parameter/2,
    compress_stream/3,
    decompress_stream/3,
    compress_reset/1,
    decompress_reset/1,
    compress_close/1,
    decompress_close/1,
    create_cdict/2,
    create_ddict/1,
    dict_id_of_cdict/1,
    dict_id_of_ddict/1,
    dict_id_of_dict/1,
    dict_id_of_frame/1,
    get_frame_header/1
]).

% Clause's zstd contexts and dictionaries (plan 21 step LIB54E, docs/builtins.md#zstd-module): runtime builtins over
% the pinned libzstd, which replace these bodies at registration. library/stdlib/zstd.erl answers OTP's zstd functions
% over them.

init_compress() -> clause_builtin_placeholder.
init_decompress() -> clause_builtin_placeholder.
load_compress_dictionary(_, _) -> clause_builtin_placeholder.
load_decompress_dictionary(_, _) -> clause_builtin_placeholder.
ref_compress_dictionary(_, _) -> clause_builtin_placeholder.
ref_decompress_dictionary(_, _) -> clause_builtin_placeholder.
set_compress_parameter(_, _, _) -> clause_builtin_placeholder.
get_compress_parameter(_, _) -> clause_builtin_placeholder.
set_pledged_src_size(_, _) -> clause_builtin_placeholder.
set_decompress_parameter(_, _, _) -> clause_builtin_placeholder.
get_decompress_parameter(_, _) -> clause_builtin_placeholder.
compress_stream(_, _, _) -> clause_builtin_placeholder.
decompress_stream(_, _, _) -> clause_builtin_placeholder.
compress_reset(_) -> clause_builtin_placeholder.
decompress_reset(_) -> clause_builtin_placeholder.
compress_close(_) -> clause_builtin_placeholder.
decompress_close(_) -> clause_builtin_placeholder.
create_cdict(_, _) -> clause_builtin_placeholder.
create_ddict(_) -> clause_builtin_placeholder.
dict_id_of_cdict(_) -> clause_builtin_placeholder.
dict_id_of_ddict(_) -> clause_builtin_placeholder.
dict_id_of_dict(_) -> clause_builtin_placeholder.
dict_id_of_frame(_) -> clause_builtin_placeholder.
get_frame_header(_) -> clause_builtin_placeholder.
