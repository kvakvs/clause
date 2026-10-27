%% OTP 29 erl_syntax_lib types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([info_pair/0]).
-type key() ::
          attributes | errors | exports | functions | imports | module |
          records | warnings.
-type info_pair() :: {key(), term()}.
