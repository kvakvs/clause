%% OTP 29 queue types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([queue/0,queue/1]).
-opaque queue(Item) :: {[Item], [Item]}.
-type queue() :: queue(_).
