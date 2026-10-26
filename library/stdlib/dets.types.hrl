%% OTP 29 dets types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([bindings_cont/0,
              cont/0,
              object_cont/0,
              select_cont/0,
              tab_name/0]).
-type tab_name() :: term().
-record(dets_cont,{what ::
                       undefined | bchunk | bindings | object | select,
                   no_objs :: default | pos_integer(),
                   bin :: eof | binary(),
                   alloc ::
                       binary() |
                       {From :: non_neg_integer(),
                        To :: non_neg_integer,
                        binary()},
                   tab :: tab_name(),
                   proc :: undefined | pid(),
                   match_program ::
                       true | undefined |
                       {match_spec, ets:compiled_match_spec()}}).
-opaque bindings_cont() :: #dets_cont{}.
-opaque cont() :: #dets_cont{}.
-opaque object_cont() :: #dets_cont{}.
-opaque select_cont() :: #dets_cont{}.
