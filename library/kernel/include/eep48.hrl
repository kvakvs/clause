%% Clause's kernel/include/eep48.hrl (plan 21 step LIB54F, docs/library.md): the records of EEP 48's documentation
%% chunk format, written for Clause from the format's description.
-ifndef(EEP48_HRL_).
-define(EEP48_HRL_, 1).

-define(NATIVE_FORMAT, <<"application/erlang+html">>).
-define(CURR_DOC_VERSION, {1, 0, 0}).

% A module's documentation: where it is, its language and format, the module doc, metadata and the entries.
-record(docs_v1, {
    anno,
    beam_language = erlang,
    format = ?NATIVE_FORMAT,
    module_doc,
    metadata = #{otp_doc_vsn => ?CURR_DOC_VERSION},
    docs
}).

% One documented function, type or callback.
-record(docs_v1_entry, {
    kind_name_arity,
    anno,
    signature,
    doc,
    metadata
}).

-endif.
