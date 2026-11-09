%% OTP 29 xmerl_scan types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([document/0,xmlElement/0]).
-type xmlElement() :: xmerl:xmlElement().
-type xmlDocument() :: xmerl:xmlDocument().
-type document() :: xmlElement() | xmlDocument().
