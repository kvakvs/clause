%% OTP 29 eldap types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([dereference/0,scope/0]).
-opaque scope() :: baseObject | singleLevel | wholeSubtree.
-opaque dereference() ::
            neverDerefAliases | derefInSearching | derefFindingBaseObj |
            derefAlways.
