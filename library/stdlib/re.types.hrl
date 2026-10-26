%% OTP 29 re types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([compile_options/0,mp/0,options/0]).
-type mp() :: {re_pattern, _, _, _, _}.
-type nl_spec() :: cr | crlf | lf | nul | anycrlf | any.
-type compile_options() :: [compile_option()].
-type compile_option() ::
          unicode | anchored | caseless | dollar_endonly | dotall |
          extended | firstline | multiline | no_auto_capture |
          dupnames | ungreedy |
          {newline, nl_spec()} |
          bsr_anycrlf | bsr_unicode | no_start_optimize | ucp |
          never_utf.
-type options() :: [option()].
-type option() ::
          anchored | global | notbol | noteol | notempty |
          notempty_atstart | report_errors |
          {offset, non_neg_integer()} |
          {match_limit, non_neg_integer()} |
          {match_limit_recursion, non_neg_integer()} |
          {capture, ValueSpec :: capture()} |
          {capture,
           ValueSpec :: capture(),
           Type :: index | list | binary} |
          compile_option().
-type capture() ::
          all | all_but_first | all_names | first | none |
          (ValueList :: [integer() | string() | atom()]).
