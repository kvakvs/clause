%% OTP 29 erlang types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([
    any/0,
    arity/0,
    atom/0,
    binary/0,
    bitstring/0,
    bool/0,
    boolean/0,
    byte/0,
    char/0,
    deprecated_time_unit/0,
    dist_handle/0,
    dynamic/0,
    float/0,
    function/0,
    identifier/0,
    integer/0,
    iodata/0,
    iolist/0,
    iovec/0,
    link_option/0,
    list/0,
    list/1,
    map/0,
    max_heap_size/0,
    maybe_improper_list/0,
    maybe_improper_list/2,
    message_queue_data/0,
    mfa/0,
    module/0,
    monitor_option/0,
    neg_integer/0,
    nif_resource/0,
    nil/0,
    no_return/0,
    node/0,
    non_neg_integer/0,
    none/0,
    nonempty_binary/0,
    nonempty_bitstring/0,
    nonempty_improper_list/2,
    nonempty_list/0,
    nonempty_list/1,
    nonempty_maybe_improper_list/0,
    nonempty_maybe_improper_list/2,
    nonempty_string/0,
    number/0,
    pid/0,
    port/0,
    pos_integer/0,
    prepared_code/0,
    priority_level/0,
    processes_iter_ref/0,
    raise_stacktrace/0,
    record/0,
    reference/0,
    send_destination/0,
    spawn_opt_option/0,
    stacktrace/0,
    string/0,
    term/0,
    time_unit/0,
    timeout/0,
    timestamp/0,
    tuple/0
]).
-type any() :: any().
-type arity() :: arity().
-type atom() :: atom().
-type binary() :: <<_:_*8>>.
-type bitstring() :: <<_:_*1>>.
-type bool() :: boolean().
-type boolean() :: true | false.
-type byte() :: 0..255.
-type char() :: 0..1114111.
-type dynamic() :: dynamic().
-type float() :: float().
-type function() :: fun().
-type identifier() :: pid() | port() | reference().
-type integer() :: integer().
-type iodata() :: iolist() | binary().
-type iolist() ::
    maybe_improper_list(
        byte() | binary() | iolist(),
        binary() | []
    ).
-type list() :: [any()].
-type list(ContentType) :: [ContentType].
-type map() :: #{any() => any()}.
-type maybe_improper_list() :: maybe_improper_list(any(), any()).
-type maybe_improper_list(ContentType, TerminationType) ::
    maybe_improper_list(ContentType, TerminationType).
-type mfa() :: {module(), atom(), arity()}.
-type module() :: atom().
-type neg_integer() :: neg_integer().
-type nil() :: [].
-type no_return() :: none().
-type node() :: atom().
-type non_neg_integer() :: non_neg_integer().
-type none() :: none().
-type nonempty_binary() :: <<_:8, _:_*8>>.
-type nonempty_bitstring() :: <<_:1, _:_*1>>.
-type nonempty_improper_list(ContentType, TerminationType) ::
    nonempty_improper_list(ContentType, TerminationType).
-type nonempty_list() :: [any(), ...].
-type nonempty_list(ContentType) :: [ContentType, ...].
-type nonempty_maybe_improper_list() ::
    nonempty_maybe_improper_list(any(), any()).
-type nonempty_maybe_improper_list(ContentType, TerminationType) ::
    nonempty_maybe_improper_list(ContentType, TerminationType).
-type nonempty_string() :: [char(), ...].
-type number() :: integer() | float().
-type pid() :: pid().
-type port() :: port().
-type pos_integer() :: pos_integer().
-type record() :: record().
-type reference() :: reference().
-type string() :: [char()].
-type term() :: any().
-type timeout() :: infinity | non_neg_integer().
-type tuple() :: tuple().
-type stacktrace_extrainfo() ::
    {line, pos_integer()}
    | {file, unicode:chardata()}
    | {error_info, #{module => module(), function => atom(), cause => term()}}
    | {atom(), term()}.
-type stacktrace() ::
    [
        {module(), atom(), arity() | [term()], [stacktrace_extrainfo()]}
        | {function(), arity() | [term()], [stacktrace_extrainfo()]}
    ].
-type timestamp() ::
    {MegaSecs :: non_neg_integer(), Secs :: non_neg_integer(), MicroSecs :: non_neg_integer()}.
-type time_unit() ::
    pos_integer()
    | second
    | millisecond
    | microsecond
    | nanosecond
    | native
    | perf_counter
    | deprecated_time_unit().
-type deprecated_time_unit() ::
    seconds | milli_seconds | micro_seconds | nano_seconds.
-opaque prepared_code() :: reference().
-opaque nif_resource() :: reference().
-opaque dist_handle() :: atom().
-type iovec() :: [binary()].
-type raise_stacktrace() ::
    [
        {module(), atom(), arity() | [term()]}
        | {function(), arity() | [term()]}
    ]
    | stacktrace().
-type monitor_option() ::
    {alias, explicit_unalias | demonitor | reply_demonitor}
    | {tag, term()}
    | priority.
-type link_option() :: priority.
-opaque processes_iter_ref() :: {integer(), [pid()]}.
-type priority_level() :: low | normal | high | max.
-type message_queue_data() :: off_heap | on_heap.
-type max_heap_size() ::
    Size ::
    non_neg_integer()
    | #{
        size => non_neg_integer(),
        kill => boolean(),
        error_logger => boolean(),
        include_shared_binaries => boolean()
    }.
-type spawn_opt_option() ::
    link
    | {link, LinkOpts :: [link_option()]}
    | monitor
    | {monitor, MonitorOpts :: [monitor_option()]}
    | {priority, Level :: priority_level()}
    | {fullsweep_after, Number :: non_neg_integer()}
    | {min_heap_size, Size :: non_neg_integer()}
    | {min_bin_vheap_size, VSize :: non_neg_integer()}
    | {max_heap_size, Size :: max_heap_size()}
    | {message_queue_data, MQD :: message_queue_data()}
    | {async_dist, Enabled :: boolean()}.
-type send_destination() ::
    pid()
    | reference()
    | port()
    | (RegName :: atom())
    | {RegName :: atom(), Node :: node()}.
