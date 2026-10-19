%% OTP 29 digraph types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([d_type/0,edge/0,graph/0,label/0,vertex/0]).
-record(digraph,{vtab = notable :: ets:table(),
                 etab = notable :: ets:table(),
                 ntab = notable :: ets:table(),
                 cyclic = true :: boolean()}).
-opaque graph() :: #digraph{}.
-type edge() :: term().
-type label() :: term().
-type vertex() :: term().
-type d_protection() :: private | protected.
-type d_cyclicity() :: acyclic | cyclic.
-type d_type() :: d_cyclicity() | d_protection().
