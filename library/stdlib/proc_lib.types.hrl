%% OTP 29 proc_lib types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([spawn_option/0,start_spawn_option/0]).
-type start_spawn_option() ::
          link |
          {priority, erlang:priority_level()} |
          {fullsweep_after, non_neg_integer()} |
          {min_heap_size, non_neg_integer()} |
          {min_bin_vheap_size, non_neg_integer()} |
          {max_heap_size, erlang:max_heap_size()} |
          {message_queue_data, erlang:message_queue_data()}.
-type spawn_option() :: erlang:spawn_opt_option().
