%% OTP 29 beam_lib types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([
    attrib_entry/0,
    beam/0,
    chnk_rsn/0,
    chunkid/0,
    compinfo_entry/0,
    label/0,
    labeled_entry/0
]).
-type beam() :: file:filename() | binary().
-type label() :: integer().
-type chunkid() :: nonempty_string().
-type attrib_entry() ::
    {Attribute :: atom(), [AttributeValue :: term()]}.
-type compinfo_entry() :: {InfoKey :: atom(), term()}.
-type labeled_entry() :: {Function :: atom(), arity(), label()}.
-type info_rsn() ::
    {chunk_too_big, beam(), chunkid(), ChunkSize :: non_neg_integer(), FileSize :: non_neg_integer()}
    | {invalid_beam_file, beam(), Position :: non_neg_integer()}
    | {invalid_chunk, beam(), chunkid()}
    | {missing_chunk, beam(), chunkid()}
    | {not_a_beam_file, beam()}
    | {file_error, beam(), file:posix()}.
-type chnk_rsn() ::
    {unknown_chunk, beam(), atom()}
    | {key_missing_or_invalid, beam(), abstract_code | debug_info}
    | {missing_backend, beam(), module()}
    | info_rsn().
