%% OTP 29 ets types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([comp_match_spec/0,
              compiled_match_spec/0,
              match_pattern/0,
              match_spec/0,
              tab/0,
              table/0,
              table_access/0,
              table_type/0,
              tid/0]).
-type table_access() :: public | protected | private.
-type table() :: atom() | tid().
-type table_type() :: set | ordered_set | bag | duplicate_bag.
-opaque tid() :: reference().
-type match_pattern() :: atom() | tuple().
-type match_spec() :: [{match_pattern(), [_], [_]}].
-type tab() :: table().
-opaque compiled_match_spec() :: reference().
-type comp_match_spec() :: compiled_match_spec().
