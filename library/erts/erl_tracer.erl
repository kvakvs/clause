%% Clause's erl_tracer (plan 21 step LIB71, docs/libraries/library.md): the tracer behaviour's callbacks as OTP lists them; the
%% default tracer's functions raise error:notsup, since tracing is not supported.
-module(erl_tracer).
-export([enabled/3, trace/5, on_load/0]).

-callback enabled(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard | remove.
-callback enabled_call(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback enabled_garbage_collection(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback enabled_ports(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback enabled_procs(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback enabled_receive(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback enabled_running_ports(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback enabled_running_procs(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback enabled_send(TraceTag :: atom(), TracerState :: term(), Tracee :: pid() | port() | undefined) ->
    trace | discard.
-callback trace(TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()) ->
    term().
-callback trace_call(TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()) ->
    term().
-callback trace_garbage_collection(
    TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()
) -> term().
-callback trace_ports(TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()) ->
    term().
-callback trace_procs(TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()) ->
    term().
-callback trace_receive(
    TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()
) ->
    term().
-callback trace_running_ports(
    TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()
) -> term().
-callback trace_running_procs(
    TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()
) -> term().
-callback trace_send(TraceTag :: atom(), TracerState :: term(), Tracee :: term(), TraceTerm :: term(), Opts :: map()) ->
    term().
%% OTP 29 leaves enabled_running_procs/3 and trace_running_procs/5 required.
-optional_callbacks([
    enabled_call/3,
    enabled_garbage_collection/3,
    enabled_ports/3,
    enabled_procs/3,
    enabled_receive/3,
    enabled_running_ports/3,
    enabled_send/3,
    trace_call/5,
    trace_garbage_collection/5,
    trace_ports/5,
    trace_procs/5,
    trace_receive/5,
    trace_running_ports/5,
    trace_send/5
]).

enabled(_, _, _) -> erlang:error(notsup).
trace(_, _, _, _, _) -> erlang:error(notsup).
%% OTP loads the default tracer's NIF here; Clause has none to load.
on_load() -> erlang:error(notsup).
