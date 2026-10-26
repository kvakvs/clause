%% OTP 29 rand types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([alg/0,
              alg_handler/0,
              alg_handler/1,
              alg_state/0,
              builtin_alg/0,
              dummy_state/0,
              export_state/0,
              exro928_state/0,
              exrop_state/0,
              exs1024_state/0,
              exs64_state/0,
              exsplus_state/0,
              mwc59_state/0,
              seed/0,
              splitmix64_state/0,
              state/0,
              uint58/0,
              uint64/0]).
-type uint64() :: 0..1 bsl 64 - 1.
-type uint58() :: 0..1 bsl 58 - 1.
-type alg_state() ::
          exsplus_state() |
          exro928_state() |
          exrop_state() |
          exs1024_state() |
          exs64_state() |
          dummy_state() |
          term().
-type alg_handler() :: alg_handler(alg()).
-type alg_handler(Alg) ::
          #{type := Alg,
            bits => non_neg_integer(),
            weak_low_bits => 0..3,
            max => non_neg_integer(),
            next :=
                fun((alg_state()) -> {non_neg_integer(), alg_state()}),
            uniform => fun((state()) -> {float(), state()}),
            uniform_n =>
                fun((pos_integer(), state()) -> {pos_integer(), state()}),
            jump => fun((state()) -> state()),
            bytes =>
                fun((non_neg_integer(), state()) -> {binary(), state()})}.
-type state() :: {alg_handler(), alg_state()}.
-type builtin_alg() ::
          exsss | exro928ss | exrop | exs1024s | exsp | exs64 |
          exsplus | exs1024 | dummy.
-type alg() :: builtin_alg() | atom().
-type export_state() :: {alg(), alg_state()}.
-type seed() ::
          [integer()] | integer() | {integer(), integer(), integer()}.
-opaque exs64_state() :: uint64().
-opaque exsplus_state() :: nonempty_improper_list(uint58(), uint58()).
-opaque exs1024_state() :: {[uint64()], [uint64()]}.
-opaque exro928_state() :: {[uint58()], [uint58()]}.
-opaque exrop_state() :: nonempty_improper_list(uint58(), uint58()).
-type dummy_state() :: uint58().
-type mwc59_state() :: 1..133850370 bsl 32 - 1 - 1.
-type splitmix64_state() :: uint64().
