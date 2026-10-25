%% OTP 29 file_sorter types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([reason/0]).
-type file_name() :: file:name().
-type reason() ::
          bad_object |
          {bad_object, file_name()} |
          {bad_term, file_name()} |
          {file_error,
           file_name(),
           file:posix() | badarg | system_limit} |
          {premature_eof, file_name()}.
