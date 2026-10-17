%% OTP 29 array types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([array/0,array/1]).
-type leaf_tuple(T) :: {T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T}.
-type element_tuple(T) ::
          leaf_tuple(T) |
          [] |
          {element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T),
           element_tuple(T)}.
-type elements(T) :: [] | element_tuple(T).
-type cache() :: leaf_tuple(dynamic()).
-record(array,{size :: non_neg_integer(),
               zero :: non_neg_integer(),
               fix :: boolean(),
               default :: dynamic(),
               cache :: cache(),
               cache_index :: non_neg_integer(),
               elements :: elements(dynamic()),
               bits :: integer()}).
-type array() :: array(dynamic()).
-opaque array(Type) ::
            #array{default :: Type, elements :: elements(Type)}.
