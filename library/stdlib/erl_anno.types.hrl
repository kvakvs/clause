%% OTP 29 erl_anno types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([anno/0,anno_term/0,column/0,line/0,location/0,text/0]).
-type annotation() ::
          {file, filename()} |
          {generated, generated()} |
          {location, location()} |
          {end_location, location()} |
          {record, record_local()} |
          {text, string()}.
-opaque anno() :: location() | [annotation(), ...].
-type anno_term() :: term().
-type column() :: pos_integer().
-type generated() :: boolean().
-type filename() :: file:filename_all().
-nominal line() :: non_neg_integer().
-nominal location() :: line() | {line(), column()}.
-type record_local() :: boolean().
-type text() :: string().
