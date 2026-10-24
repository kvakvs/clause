%% OTP 29 sys types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([dbg_fun/0,dbg_opt/0,debug_option/0,system_event/0]).
-type system_event() ::
          {in, Msg :: _} |
          {in, Msg :: _, State :: _} |
          {out, Msg :: _, To :: _} |
          {out, Msg :: _, To :: _, State :: _} |
          {noreply, State :: _} |
          {continue, Continuation :: _} |
          {postpone, Event :: _, State :: _, NextState :: _} |
          {consume, Event :: _, State :: _, NextState :: _} |
          {start_timer, Action :: _, State :: _} |
          {insert_timeout, Event :: _, State :: _} |
          {enter, Module :: module(), State :: _} |
          {module, Module :: module(), State :: _} |
          {terminate, Reason :: _, State :: _} |
          term().
-opaque dbg_opt() ::
            {trace, true} |
            {log,
             {N :: non_neg_integer(),
              [{Event :: system_event(),
                FuncState :: _,
                FormFunc :: format_fun()}]}} |
            {statistics,
             {file:date_time(),
              {reductions, non_neg_integer()},
              MessagesIn :: non_neg_integer(),
              MessagesOut :: non_neg_integer()}} |
            {log_to_file, file:io_device()} |
            {Func :: dbg_fun(), FuncState :: term()} |
            {FuncId :: term(), Func :: dbg_fun(), FuncState :: term()}.
-type dbg_fun() ::
          fun((FuncState :: _, Event :: system_event(), ProcState :: _) ->
                  done | (NewFuncState :: _)).
-type format_fun() ::
          fun((Device :: io:device() | file:io_device(),
               Event :: system_event(),
               Extra :: term()) ->
                  any()).
-type debug_option() ::
          trace | log |
          {log, N :: pos_integer()} |
          statistics |
          {log_to_file, FileName :: file:name()} |
          {install,
           {Func :: dbg_fun(), FuncState :: term()} |
           {FuncId :: term(), Func :: dbg_fun(), FuncState :: term()}}.
