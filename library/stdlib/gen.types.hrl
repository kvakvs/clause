%% OTP 29 gen types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([reply_tag/0,request_id/0,request_id_collection/0]).
-opaque reply_tag() ::
            reference() |
            nonempty_improper_list(alias, reference()) |
            nonempty_improper_list(nonempty_improper_list(alias,
                                                          reference()),
                                   term()).
-opaque request_id() :: reference().
-opaque request_id_collection() :: map().
