-module(clause_maps).
-export([entries/1]).

% Map cursors (docs/builtins.md#maps-module): the entries of a map as one {K1, V1, K2, V2, ...} tuple in key order, a
% runtime builtin that replaces this body at registration; maps:fold/3 walks it instead of a list of pairs.

entries(_) -> erlang:error(undef).
