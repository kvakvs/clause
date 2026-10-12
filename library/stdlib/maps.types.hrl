%% OTP 29 maps types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([iterator/0, iterator/2, iterator_order/0, iterator_order/1]).
-opaque iterator(Key, Value) ::
    {Key, Value, iterator(Key, Value)}
    | none
    | nonempty_improper_list(integer(), #{Key => Value})
    | nonempty_improper_list([Key], #{Key => Value}).
-type iterator() :: iterator(term(), term()).
-type iterator_order(Key) ::
    undefined
    | ordered
    | reversed
    | fun((A :: Key, B :: Key) -> boolean()).
-type iterator_order() :: iterator_order(term()).
