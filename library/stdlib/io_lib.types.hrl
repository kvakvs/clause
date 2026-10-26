%% OTP 29 io_lib types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([chars/0,
              chars_limit/0,
              continuation/0,
              format_options/0,
              format_spec/0,
              fread_error/0,
              fread_item/0,
              latin1_string/0]).
-type chars() :: [char() | chars()].
-type latin1_string() :: [unicode:latin1_char()].
-opaque continuation() ::
            {Format :: string(),
             Stack :: chars(),
             Nchars :: non_neg_integer(),
             Results :: [term()]}.
-type fread_error() ::
          atom | based | character | float | format | input | integer |
          string | unsigned.
-type fread_item() :: string() | atom() | integer() | float().
-type format_spec() ::
          #{control_char := char(),
            args := [any()],
            width := none | integer(),
            adjust := left | right,
            precision := none | integer(),
            pad_char := char(),
            encoding := unicode | latin1,
            strings := boolean(),
            maps_order => maps:iterator_order()}.
-type chars_limit() :: integer().
-type format_options() :: [{chars_limit, chars_limit()}].
