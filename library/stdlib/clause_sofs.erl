%% Private data helpers for sofs callbacks and integer selectors (LIB7A).
-module(clause_sofs).
-export([
    projection/2,
    substitution/2,
    partition/2,
    partition_family/2,
    restriction/3,
    drestriction/3,
    partition/3,
    element/2,
    accept/2,
    numbered/1,
    order_probe/1,
    finish/4,
    filter/3,
    finish_external/5,
    filter_external/4,
    match_types/2
]).

% Native projection/2 helper; callback invocation stays in sofs.
projection(_, _) -> clause_builtin_placeholder.

% Native substitution/2 helper; callback invocation stays in sofs.
substitution(_, _) -> clause_builtin_placeholder.

% Native partition/2 helper; callback invocation stays in sofs.
partition(_, _) -> clause_builtin_placeholder.

% Native partition_family/2 helper; callback invocation stays in sofs.
partition_family(_, _) -> clause_builtin_placeholder.

% Native restriction/3 helper; callback invocation stays in sofs.
restriction(_, _, _) -> clause_builtin_placeholder.

% Native drestriction/3 helper; callback invocation stays in sofs.
drestriction(_, _, _) -> clause_builtin_placeholder.

% Native partition/3 helper; callback invocation stays in sofs.
partition(_, _, _) -> clause_builtin_placeholder.

% Native element/2 helper; callback invocation stays in sofs.
element(_, _) -> clause_builtin_placeholder.

% Native accept/2 helper; callback invocation stays in sofs.
accept(_, _) -> clause_builtin_placeholder.

% Native numbered/1 helper; callback invocation stays in sofs.
numbered(_) -> clause_builtin_placeholder.

% Native order_probe/1 helper; callback invocation stays in sofs.
order_probe(_) -> clause_builtin_placeholder.

% Native finish/4 helper; callback invocation stays in sofs.
finish(_, _, _, _) -> clause_builtin_placeholder.

% Native filter/3 helper; callback invocation stays in sofs.
filter(_, _, _) -> clause_builtin_placeholder.

% External transforms retain the numbered ordering probe's sorting decision.
finish_external(_, _, _, _, _) -> clause_builtin_placeholder.

% External restrictions follow the probe when traversing result keys.
filter_external(_, _, _, _) -> clause_builtin_placeholder.

% Check external result types before invoking callbacks on restriction data.
match_types(_, _) -> clause_builtin_placeholder.
