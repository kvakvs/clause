%% OTP 29 json types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([array_finish_fun/0,
              array_push_fun/0,
              array_start_fun/0,
              continuation_state/0,
              decode_value/0,
              decoders/0,
              encode_value/0,
              encoder/0,
              formatter/0,
              from_binary_fun/0,
              object_finish_fun/0,
              object_push_fun/0,
              object_start_fun/0]).
-type encoder() :: fun((dynamic(), encoder()) -> iodata()).
-type encode_value() ::
          integer() |
          float() |
          boolean() |
          null |
          binary() |
          atom() |
          [encode_value()] |
          encode_map(encode_value()).
-type encode_map(Value) :: #{binary() | atom() | integer() => Value}.
-type formatter() ::
          fun((Term :: dynamic(),
               Encoder :: formatter(),
               State :: map()) ->
                  iodata()).
-type from_binary_fun() :: fun((binary()) -> dynamic()).
-type array_start_fun() ::
          fun((Acc :: dynamic()) -> ArrayAcc :: dynamic()).
-type array_push_fun() ::
          fun((Value :: dynamic(), Acc :: dynamic()) ->
                  NewAcc :: dynamic()).
-type array_finish_fun() ::
          fun((ArrayAcc :: dynamic(), OldAcc :: dynamic()) ->
                  {dynamic(), dynamic()}).
-type object_start_fun() ::
          fun((Acc :: dynamic()) -> ObjectAcc :: dynamic()).
-type object_push_fun() ::
          fun((Key :: dynamic(), Value :: dynamic(), Acc :: dynamic()) ->
                  NewAcc :: dynamic()).
-type object_finish_fun() ::
          fun((ObjectAcc :: dynamic(), OldAcc :: dynamic()) ->
                  {dynamic(), dynamic()}).
-type decoders() ::
          #{array_start => array_start_fun(),
            array_push => array_push_fun(),
            array_finish => array_finish_fun(),
            object_start => object_start_fun(),
            object_push => object_push_fun(),
            object_finish => object_finish_fun(),
            float => from_binary_fun(),
            integer => from_binary_fun(),
            string => from_binary_fun(),
            null => term()}.
-opaque continuation_state() :: tuple().
-type decode_value() ::
          integer() |
          float() |
          boolean() |
          null |
          binary() |
          [decode_value()] |
          #{binary() => decode_value()}.
