%% OTP 29 gb_sets types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([iter/0,iter/1,set/0,set/1]).
-type gb_set_node(Element) :: nil | {Element, _, _}.
-opaque set(Element) :: {non_neg_integer(), gb_set_node(Element)}.
-type set() :: set(_).
-opaque iter(Element) :: {ordered | reversed, [gb_set_node(Element)]}.
-type iter() :: iter(_).
