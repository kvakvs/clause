%% OTP 29 sofs types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([a_function/0,
              a_set/0,
              anyset/0,
              binary_relation/0,
              external_set/0,
              family/0,
              ordset/0,
              relation/0,
              set_fun/0,
              set_of_sets/0,
              spec_fun/0,
              type/0]).
-record('Set',{data = [] :: list(), type = type :: term()}).
-record('OrdSet',{orddata = {} :: tuple() | atom(),
                  ordtype = type :: term()}).
-type anyset() :: ordset() | a_set().
-type binary_relation() :: relation().
-type external_set() :: term().
-type a_function() :: relation().
-type family() :: a_function().
-opaque ordset() :: #'OrdSet'{}.
-type relation() :: a_set().
-opaque a_set() :: #'Set'{}.
-type set_of_sets() :: a_set().
-type set_fun() ::
          pos_integer() |
          {external, fun((external_set()) -> external_set())} |
          fun((anyset()) -> anyset()).
-type spec_fun() ::
          {external, fun((external_set()) -> boolean())} |
          fun((anyset()) -> boolean()).
-type type() :: term().
