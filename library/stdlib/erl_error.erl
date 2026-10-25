%% Clause's erl_error (plan 21 step LIB34, docs/library.md): exceptions in the shell's words, written from OTP's
%% documentation and observed output. The explanation of a reason comes first, then the stack trace, one line per
%% frame ("in function", "in call from"), a frame with arguments followed by its call as written, and the argument
%% errors a frame's error_info module describes. Terms are written by the format fun (io_lib:print/4 by default);
%% layouts that would wrap are moved to a line of their own when that wraps less. A characters limit, when not -1, is
%% shared by the stack trace (a third) and the explanation.
-module(erl_error).
-export([
    format_call/4,
    format_call/5,
    format_exception/3,
    format_exception/4,
    format_exception/6,
    format_exception/7,
    format_exception/8,
    format_fun/1,
    format_fun/2,
    format_stacktrace/4,
    format_stacktrace/5
]).

-include("erl_error.types.hrl").
%% erl_error's run-time atoms are printing placeholders; the modules its error_info calls name are the raising
%% code's atoms.
-compile({clause_liveness, strict}).

%% The extra information a frame's error_info module gives about an exception.
-callback format_error(Reason :: term(), StackTrace :: erlang:stacktrace()) ->
    #{pos_integer() | general | reason => unicode:chardata()}.

%% The written pieces of one exception: the indentation, the format fun (Term, Column, Limit) -> {Chars, Limit}, the
%% output encoding (latin1 or unicode) and the characters left.
-record(out, {indent, print, encoding, limit}).

format_exception(Class, Reason, StackTrace) ->
    format_exception(Class, Reason, StackTrace, #{}).

format_exception(Class, Reason, StackTrace, Options) ->
    Column = maps:get(column, Options, 1),
    Trim = maps:get(stack_trim_fun, Options, fun(_, _, _) -> false end),
    Print = maps:get(format_fun, Options, fun(Term, I) -> io_lib:print(Term, I, 80, 30) end),
    format_exception(Column, Class, Reason, StackTrace, Trim, Print, unicode).

format_exception(I, Class, Reason, StackTrace, Trim, Print) ->
    format_exception(I, Class, Reason, StackTrace, Trim, Print, latin1).

format_exception(I, Class, Reason, StackTrace, Trim, Print, Encoding) ->
    format_exception(I, Class, Reason, StackTrace, Trim, limited(Print), Encoding, -1).

format_exception(I, Class, Reason, StackTrace, Trim, Print, Encoding, CharsLimit) when
    is_integer(I), I >= 1, is_function(Trim, 3), is_function(Print, 3), is_integer(CharsLimit)
->
    Indent = spaces(I - 1),
    {Explained, Frame, Trace} = split_exception(Class, Reason, StackTrace),
    TraceLimit =
        case CharsLimit < 0 of
            true -> CharsLimit;
            false -> CharsLimit div 3
        end,
    Extra = extended_error(Reason, Trace),
    Out = #out{indent = Indent, print = Print, encoding = Encoding, limit = TraceLimit},
    Stack = stack_lines(Out, Trace, Trim, Extra),
    Left = take(take(CharsLimit, header(Class), latin1), Stack, Encoding),
    Default = explain(Explained, Class, Frame, Out#out{limit = Left}),
    Explanation = io_lib:fwrite(
        case Encoding of
            latin1 -> "~s~s";
            _ -> "~s~ts"
        end,
        [header(Class), maps:get(reason, Extra, Default)]
    ),
    case Stack of
        [] -> Explanation;
        _ -> [Explanation, $\n, Stack]
    end.

format_stacktrace(I, StackTrace, Trim, Print) ->
    format_stacktrace(I, StackTrace, Trim, Print, latin1).

format_stacktrace(I, StackTrace, Trim, Print, Encoding) when
    is_integer(I), I >= 1, is_function(Trim, 3), is_function(Print, 2)
->
    Out = #out{indent = spaces(I - 1), print = limited(Print), encoding = Encoding, limit = -1},
    stack_lines(Out, StackTrace, Trim, extended_error(none, StackTrace)).

format_call(I, Callee, As, Print) ->
    format_call(I, Callee, As, Print, latin1).

format_call(I, Callee, As, Print, Encoding) when
    is_integer(I), I >= 1, is_list(As), is_function(Print, 2)
->
    call("", spaces(I - 1), Callee, As, #out{
        indent = "", print = limited(Print), encoding = Encoding, limit = -1
    }).

format_fun(Fun) ->
    format_fun(Fun, latin1).

format_fun(Fun, Encoding) when is_function(Fun) ->
    {module, M} = erlang:fun_info(Fun, module),
    {name, F} = erlang:fun_info(Fun, name),
    {arity, A} = erlang:fun_info(Fun, arity),
    case erlang:fun_info(Fun, type) of
        {type, local} when F =:= "" -> io_lib:fwrite(<<"~w">>, [Fun]);
        {type, local} when M =:= erl_eval ->
            io_lib:fwrite(<<"interpreted function with arity ~w">>, [A]);
        {type, _} ->
            mfa_text(M, F, A, Encoding)
    end.

%% A two-argument format fun as one that also passes the characters limit along.
limited(Print) -> fun(Term, I, Limit) -> {Print(Term, I), Limit} end.

%% The term to explain, the frame it is about (the call of a function_clause or undef) and the frames to list.
split_exception(error, Reason, Stack) ->
    case {is_stacktrace(Stack), Stack, Reason} of
        {true, [{_, _, As, _} = Frame | Rest], function_clause} when is_list(As) ->
            {Reason, [Frame], Rest};
        {true, [{shell, F, A, _}], function_clause} when is_integer(A) -> {Reason, [{F, A}], []};
        {true, [{_, _, _, _} = Frame | Rest], undef} ->
            {Reason, [Frame], Rest};
        {true, _, _} ->
            {Reason, [], Stack};
        {false, _, _} ->
            {{Reason, Stack}, [], []}
    end;
split_exception(_Class, Reason, Stack) ->
    case is_stacktrace(Stack) of
        true -> {Reason, [], Stack};
        false -> {{Reason, Stack}, [], []}
    end.

is_stacktrace([]) ->
    true;
is_stacktrace([{M, F, A, Info} | Rest]) when is_atom(M), is_atom(F), is_integer(A), is_list(Info) ->
    is_stacktrace(Rest);
is_stacktrace([{M, F, As, Info} | Rest]) when
    is_atom(M), is_atom(F), length(As) >= 0, is_list(Info)
->
    is_stacktrace(Rest);
is_stacktrace(_) ->
    false.

%% The shell's words for the reasons ERTS, erl_eval and the restricted shell raise; any other term printed.
explain(badarg, error, [], _Out) ->
    <<"bad argument">>;
explain(badarith, error, [], _Out) ->
    <<"an error occurred when evaluating an arithmetic expression">>;
explain({badarity, {Fun, As}}, error, [], #out{encoding = Encoding}) when is_function(Fun) ->
    io_lib:fwrite(<<"~ts called with ~s">>, [format_fun(Fun, Encoding), arguments(length(As))]);
explain({badfield, F}, error, [], _Out) ->
    io_lib:bformat(<<"bad field name: ~ts">>, [record_field(F)]);
explain(function_clause, error, [{F, A}], _Out) ->
    [<<"no function clause matching call to ">> | io_lib:fwrite(<<"~w/~w">>, [F, A])];
explain(function_clause, error = Class, [{M, F, As, Location}], Out) ->
    [
        explained_call(<<"no function clause matching ">>, Class, {M, F}, As, Out),
        $\s
        | location(Location)
    ];
explain(if_clause, error, [], _Out) ->
    <<"no true branch found when evaluating an if expression">>;
explain(noproc, error, [], _Out) ->
    <<"no such process or port">>;
explain(notalive, error, [], _Out) ->
    <<"the node cannot be part of a distributed system">>;
explain(system_limit, error, [], _Out) ->
    <<"a system limit has been reached">>;
explain(timeout_value, error, [], _Out) ->
    <<"bad receive timeout value">>;
explain(undef, error, [{M, F, A, _}], #out{encoding = Encoding}) ->
    io_lib:fwrite(<<"undefined function ~ts">>, [mfa_text(M, F, argument_count(A), Encoding)]);
explain({shell_undef, F, A, _}, error, [], #out{encoding = Encoding}) ->
    io_lib:fwrite(<<"undefined shell command ~ts/~w">>, [atom_text(F, Encoding), argument_count(A)]);
explain({argument_limit, _Fun}, error, [], _Out) ->
    io_lib:fwrite(<<"limit of number of arguments to interpreted function exceeded">>, []);
explain({unbound, V}, error, [], _Out) ->
    io_lib:fwrite(<<"variable ~w is unbound">>, [V]);
explain({restricted_shell_disallowed, {Callee, As}}, exit = Class, [], Out) ->
    explained_call(<<"restricted shell does not allow ">>, Class, Callee, As, Out);
explain(restricted_shell_started, exit, [], _Out) ->
    <<"restricted shell starts now">>;
explain(restricted_shell_stopped, exit, [], _Out) ->
    <<"restricted shell stopped">>;
explain(calling_self, exit, [], _Out) ->
    <<"the current process attempted to call itself">>;
explain({novalue, Name}, error, [], _Out) ->
    io_lib:fwrite(<<"no value provided for field ~ts">>, [record_field(Name)]);
explain({Tag, V} = Reason, Class, [], Out) when is_atom(Tag) ->
    case value_reason(Tag, Class) of
        none -> printed(Reason, Class, Out);
        Words -> value(V, Words, Class, Out)
    end;
explain(Reason, Class, [], Out) ->
    printed(Reason, Class, Out).

%% Reasons {Tag, Value} told as words followed by the value.
value_reason(badarg, error) ->
    <<"bad argument: ">>;
value_reason(badkey, error) ->
    <<"bad key: ">>;
value_reason(badmap, error) ->
    <<"bad map: ">>;
value_reason(badfun, error) ->
    <<"bad function ">>;
value_reason(badmatch, error) ->
    <<"no match of right hand side value ">>;
value_reason(case_clause, error) ->
    <<"no case clause matching ">>;
value_reason(else_clause, error) ->
    <<"no else clause matching ">>;
value_reason(try_clause, error) ->
    <<"no try clause matching ">>;
value_reason(bad_filter, error) ->
    <<"bad filter ">>;
value_reason(bad_generator, error) ->
    <<"bad generator ">>;
value_reason(bad_generators, error) ->
    <<"bad generators: ">>;
value_reason(restricted_shell_bad_return, exit) ->
    <<"restricted shell module returned bad value ">>;
value_reason(_, _) ->
    none.

printed(Reason, Class, #out{indent = Indent, print = Print, limit = Limit}) ->
    {Chars, _} = Print(Reason, iolist_size(Indent) + 1 + header_size(Class), Limit),
    Chars.

record_field({{M, N}, F}) -> io_lib:bformat(<<"~w in #~w:~w{}">>, [F, M, N]).

argument_count(A) when is_integer(A) -> A;
argument_count(As) when is_list(As) -> length(As).

arguments(0) -> <<"no arguments">>;
arguments(1) -> <<"one argument">>;
arguments(2) -> <<"two arguments">>;
arguments(N) -> io_lib:fwrite(<<"~w arguments">>, [N]).

%% The stack trace without the frames the trim fun drops from its end, one line or two per frame.
stack_lines(#out{indent = Indent} = Out, Stack, Trim, Extra) ->
    Kept = lists:reverse(
        lists:dropwhile(fun({M, F, A, _}) -> Trim(M, F, A) end, lists:reverse(Stack))
    ),
    frames(Out#out{indent = ["  " | Indent]}, Kept, 1, Extra).

frames(#out{limit = 0}, _Stack, _N, _Extra) ->
    [];
frames(Out, [Frame | Rest], N, Extra) ->
    Line = frame(Out, Frame, N, Extra),
    [Line | frames(Out#out{limit = take(Out#out.limit, Line, Out#out.encoding)}, Rest, N + 1, #{})];
frames(_Out, [], _N, _Extra) ->
    "".

frame(#out{indent = Indent, encoding = Encoding}, {M, F, A, Location}, N, Extra) when
    is_integer(A)
->
    io_lib:fwrite(<<"~s~s ~ts ~ts~ts">>, [
        separator(N, Indent),
        origin(N, M, F, A),
        mfa_text(M, F, A, Encoding),
        location(Location),
        argument_errors(Extra, A, separator(N, Indent))
    ]);
frame(#out{indent = Indent, encoding = Encoding} = Out, {M, F, As, _Info}, N, Extra) when
    is_list(As)
->
    CalledAs = [Indent, <<"   called as ">>],
    Call = call("", CalledAs, {M, F}, As, Out),
    io_lib:fwrite(<<"~s~s ~ts\n~s~ts~ts">>, [
        separator(N, Indent),
        origin(N, M, F, length(As)),
        mfa_text(M, F, length(As), Encoding),
        CalledAs,
        Call,
        argument_errors(Extra, As, separator(N, Indent))
    ]).

separator(1, Indent) -> Indent;
separator(_, Indent) -> [$\n | Indent].

origin(1, M, F, A) ->
    case operator({M, F}, argument_count(A)) of
        {yes, F} -> <<"in operator ">>;
        no -> <<"in function ">>
    end;
origin(_N, _M, _F, _A) ->
    <<"in call from">>.

location(Location) ->
    case {proplists:get_value(file, Location), proplists:get_value(line, Location)} of
        {File, Line} when File =/= undefined, Line =/= undefined ->
            io_lib:format("(~ts:~w)", [File, Line]);
        _ ->
            ""
    end.

%% The extra information the first frame's error_info module gives; errors in it are ignored.
extended_error(Reason, [{M, _F, _As, Info} | _] = Stack) ->
    case lists:keyfind(error_info, 1, Info) of
        {error_info, Map} when is_map(Map) ->
            Module = maps:get(module, Map, M),
            Function = maps:get(function, Map, format_error),
            try
                Module:Function(Reason, Stack)
            catch
                error:_ -> #{}
            end;
        _ ->
            #{}
    end;
extended_error(_Reason, []) ->
    #{}.

%% "*** argument N: ..." lines for the arguments the extra information names, then its general words.
argument_errors(Extra, As, Separator) ->
    case argument_lines(1, As, Extra) of
        [] -> [];
        Lines -> lists:join([$\n, Separator, <<"   ">>], ["" | Lines])
    end.

argument_lines(N, A, Extra) when is_integer(A) ->
    argument_lines(N, lists:duplicate(A, A), Extra);
argument_lines(N, [_ | As], Extra) ->
    case Extra of
        #{N := Text} ->
            [First | More] = string:lexemes(Text, "\r\n"),
            Prefix = io_lib:format(<<"*** argument ~w: ">>, [N]),
            Width = string:length(Prefix),
            [io_lib:format(<<"~ts~ts">>, [Prefix, First])] ++
                [io_lib:format(<<"~*ts~ts">>, [Width, "", Line]) || Line <- More] ++
                argument_lines(N + 1, As, Extra);
        #{} ->
            argument_lines(N + 1, As, Extra)
    end;
argument_lines(_N, _, Extra) ->
    case Extra of
        #{general := Text} -> [io_lib:format(<<"*** ~ts">>, [Text])];
        #{} -> []
    end.

%% Words then the value, the value on its own line when it wraps less there.
value(V, Words, Class, #out{indent = Indent, print = Print, limit = Limit}) ->
    HeaderSize = header_size(Class),
    {Inline, _} = Print(V, HeaderSize + iolist_size([Indent, Words]) + 1, Limit),
    [
        Words
        | case newlines(Inline) of
            N when N > 1 ->
                {Alone, _} = Print(V, iolist_size(Indent) + 1 + HeaderSize, Limit),
                case newlines(Alone) < N of
                    true -> [$\n, Indent, spaces(HeaderSize) | Alone];
                    false -> Inline
                end;
            _ ->
                Inline
        end
    ].

explained_call(Words, Class, Callee, As, #out{indent = Indent} = Out) ->
    call(Words, [Indent | spaces(header_size(Class))], Callee, As, Out).

%% A call as written: an operator call, or the callee with its arguments in parentheses, moved to a line of its own
%% when that wraps less or when even short arguments would not fit.
call(Words, Prefix, Callee, As, #out{encoding = Encoding} = Out) ->
    [
        Words
        | case operator(Callee, length(As)) of
            {yes, Op} ->
                operator_call(Words, Prefix, Op, As, Out);
            no ->
                Name = callee_text(Callee, length(As), Encoding),
                Inline = argument_list(As, string:length([Prefix, Words | Name]), Out),
                Alone = argument_list(As, string:length([Prefix | Name]), Out),
                Probe = argument_list([a2345, b2345], string:length([Prefix, Words | Name]), Out),
                case newlines(Probe) > 0 orelse newlines(Alone) < newlines(Inline) of
                    true -> [$\n, Prefix, Name, Alone];
                    false -> [Name, Inline]
                end
        end
    ].

operator_call(Words, Prefix, Op, [A], #out{print = Print, limit = Limit}) ->
    OpText = io_lib:fwrite(<<"~s ">>, [Op]),
    {Chars, _} = Print(A, iolist_size([Words, Prefix, OpText]) + 1, Limit),
    [OpText | Chars];
operator_call(Words, Prefix, Op, [A1, A2], #out{print = Print, limit = Limit, encoding = Encoding}) ->
    Column = iolist_size([Words, Prefix]),
    {Left, Limit1} = Print(A1, Column + 1, Limit),
    {Right, _} = Print(A2, Column + 1, Limit1),
    Broken = [$\n | spaces(Column)],
    Stacked = [Left, Broken, atom_to_list(Op), Broken | Right],
    case newlines(Left) > 0 of
        true ->
            Stacked;
        false ->
            OpText = io_lib:fwrite(<<" ~s ">>, [Op]),
            {Inline, _} = Print(
                A2, iolist_size([Words, Prefix | OpText]) + text_size(Encoding, Left) + 1, Limit1
            ),
            case newlines(Right) < newlines(Inline) of
                true -> Stacked;
                false -> [Left, OpText | Inline]
            end
    end.

%% The arguments as the format fun prints their list, the brackets turned to parentheses. A printable list of
%% arguments would print as a string, so its first argument is printed as a stand-in atom of the same width.
argument_list(As, Column, #out{print = Print, limit = Limit, encoding = Encoding}) ->
    case {As, printable(Encoding, As)} of
        {[First | Rest], true} ->
            Digits = integer_to_list(First),
            Width = length(Digits),
            {Chars, _} = Print(
                [list_to_atom(lists:duplicate(Width, $a)) | Rest], Column + 1, Limit
            ),
            Text = unicode:characters_to_list(Chars, Encoding),
            parenthesized([$[, Digits, string:slice(Text, 1 + Width)], Encoding);
        _ ->
            {Chars, _} = Print(As, Column + 1, Limit),
            parenthesized(Chars, Encoding)
    end.

parenthesized(Chars, Encoding) ->
    Bin = unicode:characters_to_binary(Chars, Encoding),
    Size = byte_size(Bin) - 2,
    <<$[, Inner:Size/binary, $]>> = Bin,
    [$(, Inner, $)].

printable(latin1, As) -> io_lib:printable_latin1_list(As);
printable(_, As) -> io_lib:printable_list(As).

mfa_text(M, F, A, Encoding) ->
    io_lib:fwrite(<<"~ts/~w">>, [callee_text({M, F}, A, Encoding), A]).

%% An auto-imported BIF or operator by its name, other functions as M:F, funs as format_fun/2 writes them.
callee_text({M, F}, A, Encoding) ->
    case erl_internal:bif(M, F, A) of
        true ->
            io_lib:fwrite(<<"~w">>, [F]);
        false ->
            case operator({M, F}, A) of
                {yes, '/'} -> io_lib:fwrite(<<"~w">>, [F]);
                {yes, F} -> atom_to_list(F);
                no -> io_lib:fwrite(<<"~w:~ts">>, [M, atom_text(F, Encoding)])
            end
    end;
callee_text(Fun, _A, Encoding) when is_function(Fun) ->
    format_fun(Fun, Encoding);
callee_text(F, _A, Encoding) ->
    io_lib:fwrite(<<"~ts">>, [atom_text(F, Encoding)]).

%% {yes, Op} for an erlang operator of that arity.
operator(Callee, A) ->
    try
        {erlang, F} = Callee,
        _ = erl_internal:op_type(F, A),
        {yes, F}
    catch
        error:_ -> no
    end.

%% Newlines in deep lists of characters and binaries.
newlines([H | T]) -> newlines(H) + newlines(T);
newlines($\n) -> 1;
newlines(Bin) when is_binary(Bin) -> newlines(binary_to_list(Bin));
newlines(_) -> 0.

spaces(N) -> lists:duplicate(N, $\s).

header(error) -> <<"exception error: ">>;
header(exit) -> <<"exception exit: ">>;
header(throw) -> <<"exception throw: ">>.

header_size(Class) -> iolist_size(header(Class)).

atom_text(A, latin1) -> io_lib:write_atom_as_latin1(A);
atom_text(A, _) -> io_lib:write_atom(A).

%% The characters limit after Chars; a negative limit stays (no limit), a limit never goes below zero.
take(Limit, _Chars, _Encoding) when Limit < 0 -> Limit;
take(Limit, Chars, Encoding) -> max(Limit - text_size(Encoding, Chars), 0).

text_size(latin1, Chars) -> iolist_size(Chars);
text_size(_, Chars) -> string:length(Chars).
