%% OTP 29 disk_log types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([continuation/0]).
-record(continuation,{pid = self() :: pid(),
                      pos ::
                          non_neg_integer() |
                          {integer(), non_neg_integer()},
                      b :: binary() | [] | pos_integer()}).
-opaque continuation() :: #continuation{}.
