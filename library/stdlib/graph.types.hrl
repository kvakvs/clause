%% OTP 29 graph types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([edge/0,
              graph/0,
              graph_cyclicity/0,
              graph_type/0,
              label/0,
              vertex/0]).
-type edge_map() :: #{vertex() => ordsets:ordset(vertex())}.
-type vertice_map() :: #{vertex() => label()}.
-record(graph,{vs = #{} :: vertice_map(),
               in_es = #{} :: edge_map(),
               out_es = #{} :: edge_map(),
               cyclic = true :: boolean(),
               next_vid = 0 :: non_neg_integer()}).
-type graph() :: #graph{}.
-type vertex() :: term().
-type label() :: term().
-type edge() :: {vertex(), vertex(), label()}.
-type graph_cyclicity() :: acyclic | cyclic.
-type graph_type() :: graph_cyclicity().
