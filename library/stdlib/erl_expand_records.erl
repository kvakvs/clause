%% Clause's erl_expand_records (plan 21 steps LIB34, LIB34C, docs/builtins.md#erl-expand-records): module/2 is a
%% runtime builtin (runtime/src/builtins/front_end/expand_records.cpp) written from OTP's documentation and observed
%% output. Tuple records become tuple operations: constructions tuples with the defaults filled in, patterns tuple
%% patterns, field access a case (or element/2 behind an is_record/3 test in guards), updates setelement/3 chains (or a
%% case rebuilding the tuple with the dialyzer option), is_record/2 is_record/3, record_info/2 its value. Native
%% records stay, imported names gaining their module. Calls of imported functions and of BIFs get their module. New
%% variables are named rec0, rec1, ... in the order OTP makes them, and is_record/3 tests of a clause's guard become
%% patterns when the guard cannot hold without them.
-module(erl_expand_records).
-export([module/2]).

module(_, _) -> clause_builtin_placeholder.
