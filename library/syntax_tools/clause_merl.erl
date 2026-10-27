%% clause_merl: the internal builtin of merl (plan 21 step LIB77E5, runtime/src/builtins/merl/shapes.cpp). Its catalog
%% builtin replaces the clause_builtin_placeholder body.
-module(clause_merl).
-export([subtrees/1]).

%% subtrees(Tree): merl's groups of a node: [] as a list with two empty groups, {} with its element group, and fixed
%% groups for lists, binary fields, clauses, receives and record nodes.
subtrees(_) -> clause_builtin_placeholder.
