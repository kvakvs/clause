%% OTP 29 erl_comment_scan types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([comment/0]).
-type comment() ::
          {Line :: integer(),
           Column :: integer(),
           Indentation :: integer(),
           Text :: [string()]}.
