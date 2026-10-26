%% OTP 29 gb_trees types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([iter/0,iter/2,tree/0,tree/2]).
-type gb_tree_node(K, V) ::
          nil | {K, V, gb_tree_node(K, V), gb_tree_node(K, V)}.
-opaque tree(Key, Value) ::
            {non_neg_integer(), gb_tree_node(Key, Value)}.
-type tree() :: tree(_, _).
-opaque iter(Key, Value) ::
            {ordered | reversed, [gb_tree_node(Key, Value)]}.
-type iter() :: iter(_, _).
