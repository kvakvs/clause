%% OTP 29 prettypr types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([document/0]).
-type deep_string() :: [char() | deep_string()].
-record(text,{s :: deep_string()}).
-record(nest,{n :: integer(), d :: document()}).
-record(beside,{d1 :: document(), d2 :: document()}).
-record(above,{d1 :: document(), d2 :: document()}).
-record(sep,{ds :: [document()],
             i = 0 :: integer(),
             p = false :: boolean()}).
-record(float,{d :: document(), h :: integer(), v :: integer()}).
-record(union,{d1 :: document(), d2 :: document()}).
-record(fit,{d :: document()}).
-type document() ::
          null |
          #text{} |
          #nest{} |
          #beside{} |
          #above{} |
          #sep{} |
          #float{} |
          #union{} |
          #fit{}.
