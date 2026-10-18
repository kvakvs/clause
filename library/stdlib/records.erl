%% Clause's records (plan 21 step LIB8B, docs/native-records.md#reflection): native record reflection; every
%% function is a runtime builtin over the definition a record captures.
-module(records).
-export([
    create/4,
    get/2,
    get_definition/2,
    get_field_names/1,
    get_module/1,
    get_name/1,
    is_exported/1,
    update/4
]).

create(_, _, _, _) -> clause_builtin_placeholder.
get(_, _) -> clause_builtin_placeholder.
get_definition(_, _) -> clause_builtin_placeholder.
get_field_names(_) -> clause_builtin_placeholder.
get_module(_) -> clause_builtin_placeholder.
get_name(_) -> clause_builtin_placeholder.
is_exported(_) -> clause_builtin_placeholder.
update(_, _, _, _) -> clause_builtin_placeholder.
