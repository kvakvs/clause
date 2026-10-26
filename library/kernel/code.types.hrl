%% OTP 29 code types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([coverage_mode/0,
              debug_atom_or_var/0,
              debug_call/0,
              debug_frame/0,
              debug_info/0,
              debug_line/0,
              debug_name/0,
              debug_source/0,
              debug_value/0,
              debug_var/0,
              load_error_rsn/0,
              load_ret/0,
              module_status/0,
              prepared_code/0]).
-type coverage_mode() ::
          none | function | function_counters | line | line_counters.
-nominal debug_line() :: pos_integer().
-nominal debug_frame() :: non_neg_integer() | entry | none.
-nominal debug_var() :: binary().
-nominal debug_name() :: debug_var() | 1..255.
-nominal debug_source() ::
             {x, non_neg_integer()} |
             {y, non_neg_integer()} |
             {value, _}.
-nominal debug_value() :: {debug_name(), debug_source()}.
-nominal debug_atom_or_var() :: atom() | debug_var().
-nominal debug_call() ::
             MFA ::
                 {debug_atom_or_var(), debug_atom_or_var(), arity()} |
                 (FA :: {debug_atom_or_var(), arity()} | debug_var()).
-nominal debug_info() ::
             [{debug_line(),
               #{frame_size => debug_frame(),
                 vars => [debug_value()],
                 calls => [debug_call()]}}].
-type load_error_rsn() ::
          badfile | nofile | not_purged | on_load_failure |
          sticky_directory.
-type load_ret() ::
          {error, What :: load_error_rsn()} |
          {module, Module :: module()}.
-opaque prepared_code() ::
            {'$prepared$', [{module(), {binary(), string(), _}}]}.
-type module_status() :: not_loaded | loaded | modified | removed.
