%% OTP 29 wrap_log_reader types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([continuation/0]).
-record(continuation,{pid = self() :: pid(),
                      pos ::
                          non_neg_integer() |
                          {integer(), non_neg_integer()},
                      b :: binary() | [] | pos_integer()}).
-type dlog_cont() :: start | #continuation{}.
-record(wrap_reader,{fd :: file:fd(),
                     cont :: dlog_cont(),
                     file :: file:filename(),
                     file_no :: non_neg_integer(),
                     mod_time :: file:date_time(),
                     first_no :: non_neg_integer() | one}).
-opaque continuation() :: #wrap_reader{}.
