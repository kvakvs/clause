%% Clause's seq_trace (plan 21 step LIB25, docs/libraries/library.md): each process keeps its sequential trace token, with
%% OTP's components and answers. Clause's runtime does not trace: tokens are not passed on with messages, serial
%% numbers stay where they are set, and there is no tracer to print to (LIB71).
-module(seq_trace).
-export([
    get_system_tracer/0,
    get_token/0,
    get_token/1,
    print/1,
    print/2,
    reset_trace/0,
    set_system_tracer/1,
    set_token/1,
    set_token/2
]).

-define(KEY, '$clause_seq_trace_token').
-define(TRACER_KEY, {?MODULE, system_tracer}).

%% The flag bits of a token's first element, as the emulator numbers them.
flag_bit(send) -> 1;
flag_bit('receive') -> 2;
flag_bit(print) -> 4;
flag_bit(timestamp) -> 8;
flag_bit(monotonic_timestamp) -> 16;
flag_bit(strict_monotonic_timestamp) -> 32.

%% The process's token: [] or {Flags, Label, Serial, From, LastCount}.
get_token() ->
    case get(?KEY) of
        undefined -> [];
        Token -> Token
    end.

%% An empty token has no label or serial: [].
get_token(Component) when Component =:= label; Component =:= serial ->
    case get_token() of
        [] -> [];
        {_, Label, _, _, _} when Component =:= label -> {label, Label};
        {_, _, Serial, _, Last} -> {serial, {Last, Serial}}
    end;
get_token(Flag) ->
    {Flag, element(1, token()) band flag_bit(Flag) =/= 0}.

%% A token with no label or flags, for reading components of an empty token.
token() ->
    case get_token() of
        [] -> {0, 0, 0, self(), 0};
        Token -> Token
    end.

set_token([]) ->
    Old = get_token(),
    erase(?KEY),
    Old;
set_token({Flags, _, Serial, From, Last} = Token) when
    is_integer(Flags), is_integer(Serial), is_pid(From), is_integer(Last)
->
    Old = get_token(),
    put(?KEY, Token),
    Old;
set_token(Token) ->
    erlang:error(badarg, [Token]).

%% Sets one component and returns its previous value.
set_token(label, Label) ->
    {Flags, Old, Serial, From, Last} = token(),
    put(?KEY, {Flags, Label, Serial, From, Last}),
    Old;
set_token(serial, {Last, Serial}) when is_integer(Last), is_integer(Serial) ->
    {Flags, Label, OldSerial, From, OldLast} = token(),
    put(?KEY, {Flags, Label, Serial, From, Last}),
    {OldLast, OldSerial};
set_token(Flag, Bool) when is_boolean(Bool) ->
    {Flags, Label, Serial, From, Last} = token(),
    Bit = flag_bit(Flag),
    New =
        case Bool of
            true -> Flags bor Bit;
            false -> Flags band bnot Bit
        end,
    put(?KEY, {New, Label, Serial, From, Last}),
    Flags band Bit =/= 0;
set_token(Component, Value) ->
    erlang:error(badarg, [Component, Value]).

%% Printing goes to the system tracer, which never receives trace messages here.
print(_) -> ok.

print(_, _) -> ok.

reset_trace() ->
    erase(?KEY),
    true.

set_system_tracer(Tracer) when is_pid(Tracer); is_port(Tracer); Tracer =:= false ->
    Old = get_system_tracer(),
    persistent_term:put(?TRACER_KEY, Tracer),
    Old.

get_system_tracer() -> persistent_term:get(?TRACER_KEY, false).
