%% OTP 29 supervisor types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([child_spec/0,
              startchild_err/0,
              startchild_ret/0,
              startlink_err/0,
              startlink_ret/0,
              strategy/0,
              sup_flags/0,
              sup_name/0,
              sup_ref/0]).
-type auto_shutdown() :: never | any_significant | all_significant.
-type child() :: undefined | pid().
-type child_id() :: term().
-type mfargs() ::
          {M :: module(), F :: atom(), A :: [term()] | undefined}.
-type modules() :: [module()] | dynamic.
-type restart() :: permanent | transient | temporary.
-type significant() :: boolean().
-type shutdown() :: brutal_kill | timeout().
-type worker() :: worker | supervisor.
-type sup_name() ::
          {local, Name :: atom()} |
          {global, Name :: term()} |
          {via, Module :: module(), Name :: any()}.
-type sup_ref() ::
          (Name :: atom()) |
          {Name :: atom(), Node :: node()} |
          {global, Name :: term()} |
          {via, Module :: module(), Name :: any()} |
          pid().
-type child_spec() ::
          #{id := child_id(),
            start := mfargs(),
            restart => restart(),
            significant => significant(),
            shutdown => shutdown(),
            type => worker(),
            modules => modules()} |
          {Id :: child_id(),
           StartFunc :: mfargs(),
           Restart :: restart(),
           Shutdown :: shutdown(),
           Type :: worker(),
           Modules :: modules()}.
-type strategy() ::
          one_for_all | one_for_one | rest_for_one | simple_one_for_one.
-type sup_flags() ::
          #{strategy => strategy(),
            intensity => non_neg_integer(),
            period => pos_integer(),
            auto_shutdown => auto_shutdown(),
            hibernate_after => timeout()} |
          {RestartStrategy :: strategy(),
           Intensity :: non_neg_integer(),
           Period :: pos_integer()}.
-type startlink_err() ::
          {already_started, pid()} | {shutdown, term()} | term().
-type startlink_ret() :: {ok, pid()} | ignore | {error, startlink_err()}.
-type startchild_err() ::
          already_present | {already_started, Child :: child()} | term().
-type startchild_ret() ::
          {ok, Child :: child()} |
          {ok, Child :: child(), Info :: term()} |
          {error, startchild_err()}.
