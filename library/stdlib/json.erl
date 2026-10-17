%% Clause's json (plan 21 step LIB5, docs/builtins.md#encodings): decode/1 and the binary escapers are runtime
%% builtins with clause_builtin_placeholder bodies. The encoders, the formatter and the callback decoders are Erlang:
%% they take funs and build the same iodata as OTP's. The callback decoders walk OTP's states over the clause_json
%% string and number scanners, so callbacks run in OTP's order and errors and continuations agree.
-module(json).
-export([
    decode/1,
    decode/3,
    decode_continue/2,
    decode_start/3,
    encode/1,
    encode/2,
    encode_atom/2,
    encode_binary/1,
    encode_binary_escape_all/1,
    encode_float/1,
    encode_integer/1,
    encode_key_value_list/2,
    encode_key_value_list_checked/2,
    encode_list/2,
    encode_map/2,
    encode_map_checked/2,
    encode_value/2,
    format/1,
    format/2,
    format/3,
    format_key_value_list/3,
    format_key_value_list_checked/3,
    format_value/3
]).

-include("json.types.hrl").

-define(IS_SPACE(B), (B =:= $\s orelse B =:= $\t orelse B =:= $\r orelse B =:= $\n)).

%% The callbacks of decode/3: undefined where the default applies.
-record(decode, {
    array_start,
    array_push,
    array_finish,
    object_start,
    object_push,
    object_finish,
    float = fun erlang:binary_to_float/1,
    integer = fun erlang:binary_to_integer/1,
    string,
    null = null
}).

%% Encoding

%% The default encoder in C++; json.erl's own for deeply nested terms.
encode(Term) ->
    case clause_json:encode(Term) of
        {ok, Iodata} -> Iodata;
        fallback -> encode(Term, fun encode_value/2)
    end.

encode(Term, Encoder) when is_function(Encoder, 2) -> Encoder(Term, Encoder).

%% The default encoder: Encode is called back for the elements, members and atoms' names.
encode_value(Value, Encode) when is_atom(Value) -> encode_atom(Value, Encode);
encode_value(Value, _Encode) when is_binary(Value) -> json:encode_binary(Value);
encode_value(Value, _Encode) when is_integer(Value) -> encode_integer(Value);
encode_value(Value, _Encode) when is_float(Value) -> encode_float(Value);
encode_value(Value, Encode) when is_list(Value) -> array(Value, Encode);
encode_value(Value, Encode) when is_map(Value) -> object_of_map(Value, Encode);
encode_value(Value, _Encode) -> error({unsupported_type, Value}).

encode_atom(null, _Encode) -> <<"null">>;
encode_atom(true, _Encode) -> <<"true">>;
encode_atom(false, _Encode) -> <<"false">>;
encode_atom(Atom, Encode) -> Encode(atom_to_binary(Atom, utf8), Encode).

encode_integer(Integer) -> integer_to_binary(Integer).

encode_float(Float) -> float_to_binary(Float, [short]).

encode_list(List, Encode) when is_list(List) -> array(List, Encode).

array([], _Encode) ->
    <<"[]">>;
array([First | Rest], Encode) when is_function(Encode, 2) ->
    Head = Encode(First, Encode),
    [$[, Head | array_rest(Rest, Encode)].

array_rest([], _Encode) ->
    "]";
array_rest([Element | Rest], Encode) ->
    Head = Encode(Element, Encode),
    [$,, Head | array_rest(Rest, Encode)].

encode_map(Map, Encode) when is_map(Map) -> object_of_map(Map, Encode).

object_of_map(Map, Encode) when is_function(Encode, 2) ->
    object([member(Key, Value, Encode) || {Key, Value} <- maps:to_list(Map)]).

encode_map_checked(Map, Encode) -> checked_object(maps:to_list(Map), Encode).

%% Members of a list of pairs; elements that are not pairs are skipped.
encode_key_value_list(List, Encode) when is_function(Encode, 2) ->
    object([member(Key, Value, Encode) || {Key, Value} <- List]).

encode_key_value_list_checked(List, Encode) -> checked_object(List, Encode).

%% One member, comma first: [$,, Key, $: | Value].
member(Key, Value, Encode) ->
    EncodedKey = key(Key, Encode),
    [$,, EncodedKey, $: | Encode(Value, Encode)].

checked_object(List, Encode) when is_function(Encode, 2) ->
    object(checked_members(List, Encode, #{})).

%% Members whose encoded keys must differ: error({duplicate_key, Key}) at the second.
checked_members([{Key, Value} | Rest], Encode, Seen) ->
    EncodedKey = iolist_to_binary(key(Key, Encode)),
    case is_map_key(EncodedKey, Seen) of
        true ->
            error({duplicate_key, Key});
        false ->
            Member = [$,, EncodedKey, $: | Encode(Value, Encode)],
            [Member | checked_members(Rest, Encode, Seen#{EncodedKey => true})]
    end;
checked_members([], _Encode, _Seen) ->
    [].

%% Keys are strings: binaries and atoms through Encode, numbers quoted as they are.
key(Key, Encode) when is_binary(Key) -> Encode(Key, Encode);
key(Key, Encode) when is_atom(Key) -> Encode(atom_to_binary(Key, utf8), Encode);
key(Key, _Encode) when is_integer(Key) -> [$", encode_integer(Key), $"];
key(Key, _Encode) when is_float(Key) -> [$", encode_float(Key), $"].

object([]) -> <<"{}">>;
object([[_Comma | First] | Rest]) -> [${, First, Rest, $}].

encode_binary(_) -> clause_builtin_placeholder.
encode_binary_escape_all(_) -> clause_builtin_placeholder.

%% Formatting

format(Term) -> format(Term, #{}).

%% The default formatter in C++; json.erl's own for deeply nested terms or options it does not take.
format(Term, Options) when is_map(Options) ->
    State = maps:merge(#{level => 0, col => 0, indent => 2, max => 100}, Options),
    case clause_json:format(Term, State) of
        {ok, Iodata} -> [Iodata, $\n];
        fallback -> [format_value(Term, fun format_value/3, State), $\n]
    end;
format(Term, Encoder) when is_function(Encoder, 3) ->
    format(Term, Encoder, #{}).

format(Term, Encoder, Options) when is_function(Encoder, 3) ->
    State = maps:merge(#{level => 0, col => 0, indent => 2, max => 100}, Options),
    [Encoder(Term, Encoder, State), $\n].

%% The default formatter: arrays and objects over several lines, map members in key order.
format_value(Atom, Encode, State) when is_atom(Atom) ->
    encode_atom(Atom, fun(Value, Inner) -> Encode(Value, Inner, State) end);
format_value(Binary, _Encode, _State) when is_binary(Binary) ->
    json:encode_binary(Binary);
format_value(Integer, _Encode, _State) when is_integer(Integer) ->
    encode_integer(Integer);
format_value(Float, _Encode, _State) when is_float(Float) ->
    encode_float(Float);
format_value(List, Encode, State) when is_list(List) ->
    format_array(List, Encode, State);
format_value(Map, Encode, State) when is_map(Map) ->
    format_key_value_list(maps:to_list(maps:iterator(Map, ordered)), Encode, State);
format_value(Other, _Encode, _State) ->
    error({unsupported_type, Other}).

%% An array: one element per line when its first is an array, object or string or the line is already past `max`,
%% else elements run along the line and wrap after `max` columns.
format_array([], _Encode, _State) ->
    <<"[]">>;
format_array([First | Rest], Encode, #{level := Level, col := Column, max := Max} = State) ->
    Inner = State#{level := Level + 1},
    {Width, Indent} = indent(Inner),
    case is_list(First) orelse is_map(First) orelse is_binary(First) orelse Column > Max of
        true ->
            Lined = Inner#{col := Width},
            Head = Encode(First, Encode, Lined),
            {_, Closing} = indent(State),
            [$[, Indent, Head, lined_elements(Rest, Encode, Lined, Indent), Closing, $]];
        false ->
            Head = Encode(First, Encode, Inner),
            Next = Column + 1 + erlang:iolist_size(Head),
            [$[, Head, running_elements(Rest, Encode, Inner#{col := Next}, Indent), $]]
    end.

lined_elements([], _Encode, _State, _Indent) ->
    [];
lined_elements([Element | Rest], Encode, State, Indent) ->
    Head = Encode(Element, Encode, State),
    [[[$, | Indent] | Head] | lined_elements(Rest, Encode, State, Indent)].

%% Elements along a line, a new line once the column reaches `max`.
running_elements([], _Encode, _State, _Indent) ->
    [];
running_elements([Element | Rest], Encode, #{col := Column, max := Max} = State, Indent) when
    Column < Max
->
    Head = Encode(Element, Encode, State),
    Next = Column + 1 + erlang:iolist_size(Head),
    [[$, | Head] | running_elements(Rest, Encode, State#{col := Next}, Indent)];
running_elements([Element | Rest], Encode, State, Indent) ->
    Head = Encode(Element, Encode, State),
    Wrapped = [[$, | Indent] | Head],
    Next = erlang:iolist_size(Wrapped) - 2,
    [Wrapped | running_elements(Rest, Encode, State#{col := Next}, Indent)].

format_key_value_list(List, Encode, #{level := Level} = State) ->
    {_, Closing} = indent(State),
    Inner = State#{level := Level + 1},
    Members = lists:map(fun({Key, Value}) -> formatted_member(Key, Value, Encode, Inner) end, List),
    format_object(Members, Closing).

format_key_value_list_checked(List, Encode, State) when is_function(Encode, 3) ->
    {_, Closing} = indent(State),
    format_object(checked_formatted(List, Encode, State), Closing).

checked_formatted([], _Encode, _State) ->
    [];
checked_formatted(List, Encode, #{level := Level} = State) ->
    Inner = State#{level := Level + 1},
    Check = fun({Key, Value}, {Members, Seen}) ->
        EncodedKey = iolist_to_binary(formatted_key(Key, Encode, Inner)),
        case is_map_key(EncodedKey, Seen) of
            true ->
                error({duplicate_key, Key});
            false ->
                Member = member_line(EncodedKey, Value, Encode, Inner),
                {[Member | Members], Seen#{EncodedKey => true}}
        end
    end,
    {Members, _} = lists:foldl(Check, {[], #{}}, List),
    lists:reverse(Members).

formatted_member(Key, Value, Encode, State) ->
    member_line(formatted_key(Key, Encode, State), Value, Encode, State).

formatted_key(Key, Encode, State) -> key(Key, fun(Name, _) -> Encode(Name, Encode, State) end).

%% [$,, Indent, Key, ": " | Value], the value starting after the key.
member_line(EncodedKey, Value, Encode, State) ->
    {Width, Indent} = indent(State),
    Inner = State#{col := Width + 2 + erlang:iolist_size(EncodedKey)},
    [$,, Indent, EncodedKey, ": " | Encode(Value, Encode, Inner)].

%% An object: one member whose value is not an array or object stays on one line.
format_object([], _Closing) ->
    <<"{}">>;
format_object([[_Comma, Indent | Member]], Closing) ->
    [_Key, _Colon | Value] = Member,
    case lists:member(first_character(Value), "{[") of
        true -> [${, Indent, Member, Closing, $}];
        false -> ["{ ", Member, " }"]
    end;
format_object([[_Comma, Indent | First] | Rest], Closing) ->
    [${, Indent, First, Rest, Closing, $}].

%% The first character of a value that is not a space or a newline.
first_character(Value) ->
    case first_in(Value) of
        none -> error({badmatch, []});
        Character -> Character
    end.

%% The first character of iodata that is not a space or a newline, walked from the front; none if there is none.
first_in(C) when C =:= $\s; C =:= $\n -> none;
first_in(C) when is_integer(C) -> C;
first_in(<<C, Rest/binary>>) when C =:= $\s; C =:= $\n -> first_in(Rest);
first_in(<<C, _/binary>>) ->
    C;
first_in(<<>>) ->
    none;
first_in([]) ->
    none;
first_in([Head | Tail]) ->
    case first_in(Head) of
        none -> first_in(Tail);
        Character -> Character
    end.

%% The column and the line break of a nesting level.
indent(#{level := Level, indent := Indent}) ->
    Width = Level * Indent,
    {Width, line_break(Width)}.

line_break(Width) when is_integer(Width), Width >= 0, Width =< 20, Width rem 2 =:= 0 ->
    <<$\n, (binary:copy(<<$\s>>, Width))/binary>>;
line_break(Width) ->
    ["\n", lists:duplicate(Width, " ")].

%% Decoding

decode(_) -> clause_builtin_placeholder.

decode(Binary, Acc, Decoders) when is_binary(Binary) ->
    case value(Binary, 0, Acc, [], decoders(Decoders)) of
        {continue, State} -> finish(State);
        Result -> Result
    end.

decode_start(Binary, Acc, Decoders) when is_binary(Binary) ->
    value(Binary, 0, Acc, [], decoders(Decoders)).

decode_continue(end_of_input, State) ->
    finish(State);
decode_continue(More, {Rest, Acc, Stack, #decode{} = Decoders, Waiting}) when is_binary(More) ->
    resume(<<Rest/binary, More/binary>>, Acc, Stack, Decoders, Waiting).

%% The end of the input in a continuation: only a top-level number is complete.
finish({_, Acc, [], _, {number, Value}}) -> {Value, Acc, <<>>};
finish({_, _, _, _, {float_error, Token, _}}) -> error({unexpected_sequence, Token});
finish(_) -> error(unexpected_end).

resume(Binary, Acc, Stack, Decoders, {array_push, Value}) ->
    array_push(Binary, 0, Acc, Stack, Decoders, Value);
resume(Binary, Acc, Stack, Decoders, {object_value, Key}) ->
    object_value(Binary, 0, Acc, Stack, Decoders, Key);
resume(Binary, Acc, Stack, Decoders, {object_push, Value, Key}) ->
    object_push(Binary, 0, Acc, Stack, Decoders, Value, Key);
resume(Binary, Acc, Stack, Decoders, object_key) ->
    object_key(Binary, 0, Acc, Stack, Decoders);
resume(Binary, Acc, Stack, Decoders, _Value) ->
    value(Binary, 0, Acc, Stack, Decoders).

decoders(Decoders) -> maps:fold(fun decoder/3, #decode{}, Decoders).

decoder(array_start, Fun, D) when is_function(Fun, 1) -> D#decode{array_start = Fun};
decoder(array_push, Fun, D) when is_function(Fun, 2) -> D#decode{array_push = Fun};
decoder(array_finish, Fun, D) when is_function(Fun, 2) -> D#decode{array_finish = Fun};
decoder(object_start, Fun, D) when is_function(Fun, 1) -> D#decode{object_start = Fun};
decoder(object_push, Fun, D) when is_function(Fun, 3) -> D#decode{object_push = Fun};
decoder(object_finish, Fun, D) when is_function(Fun, 2) -> D#decode{object_finish = Fun};
decoder(float, Fun, D) when is_function(Fun, 1) -> D#decode{float = Fun};
decoder(integer, Fun, D) when is_function(Fun, 1) -> D#decode{integer = Fun};
decoder(string, Fun, D) when is_function(Fun, 1) -> D#decode{string = Fun};
decoder(null, Null, D) -> D#decode{null = Null}.

%% The byte at Skip, or eof.
byte(Binary, Skip) when Skip < byte_size(Binary) -> binary:at(Binary, Skip);
byte(_Binary, _Skip) -> eof.

%% A value from Skip, spaces first. The stack holds `array` or `object` and the accumulator outside it for each open
%% container, and an object's key while its value is read.
value(Binary, Skip, Acc, Stack, D) ->
    case byte(Binary, Skip) of
        B when B =:= $-; B >= $0, B =< $9 -> number(Binary, Skip, Acc, Stack, D);
        $" -> string(Binary, Skip + 1, Acc, Stack, D);
        $[ -> array_start(Binary, Skip, 1, Acc, Stack, D);
        ${ -> object_start(Binary, Skip, 1, Acc, Stack, D);
        $t -> literal(Binary, Skip, <<"rue">>, true, Acc, Stack, D);
        $f -> literal(Binary, Skip, <<"alse">>, false, Acc, Stack, D);
        $n -> literal(Binary, Skip, <<"ull">>, D#decode.null, Acc, Stack, D);
        B when ?IS_SPACE(B) -> value(Binary, Skip + 1, Acc, Stack, D);
        eof -> pause(Binary, Skip, Acc, Stack, D, value);
        _ -> invalid_byte(Binary, Skip)
    end.

%% The walk needs more input: resume from Skip, where it waits for what Waiting names.
pause(Binary, Skip, Acc, Stack, D, Waiting) ->
    {continue, {binary_part(Binary, Skip, byte_size(Binary) - Skip), Acc, Stack, D, Waiting}}.

%% OTP's unexpected: Pos bytes past Skip with Len more needed; more input may complete it, else the byte at
%% Skip + Pos is invalid.
unexpected(Binary, Skip, Pos, Len, Acc, Stack, D, Waiting) when
    byte_size(Binary) =< Skip + Pos + Len
->
    pause(Binary, Skip, Acc, Stack, D, Waiting);
unexpected(Binary, Skip, Pos, _Len, _Acc, _Stack, _D, _Waiting) ->
    invalid_byte(Binary, Skip + Pos).

invalid_byte(Binary, Skip) -> error({invalid_byte, binary:at(Binary, Skip)}).

literal(Binary, Skip, Rest, Value, Acc, Stack, D) ->
    Size = byte_size(Rest),
    case Binary of
        <<_:Skip/binary, _, Rest:Size/binary, _/binary>> ->
            continue(Binary, Skip + 1 + Size, Acc, Stack, D, Value);
        _ ->
            unexpected(Binary, Skip, 1, Size, Acc, Stack, D, value)
    end.

string(Binary, Skip, Acc, Stack, D) ->
    case clause_json:string(Binary, Skip) of
        more ->
            pause(Binary, Skip - 1, Acc, Stack, D, value);
        {Text, Next} when D#decode.string =:= undefined ->
            continue(Binary, Next, Acc, Stack, D, Text);
        {Text, Next} ->
            continue(Binary, Next, Acc, Stack, D, (D#decode.string)(Text))
    end.

%% A number: the integer or float decoder sees its text; at the end of the input more digits may follow.
number(Binary, Skip, Acc, Stack, D) ->
    case clause_json:number(Binary, Skip) of
        more ->
            pause(Binary, Skip, Acc, Stack, D, value);
        {integer, Text, Next} ->
            Value = (D#decode.integer)(Text),
            number_end(Binary, Skip, Next, Acc, Stack, D, Value);
        {float, Token, Next} when Next =:= byte_size(Binary) ->
            try (D#decode.float)(Token) of
                Value -> pause(Binary, Skip, Acc, Stack, D, {number, Value})
            catch
                _:_ -> pause(Binary, Skip, Acc, Stack, D, {float_error, Token, Skip})
            end;
        {float, Token, Next} ->
            try (D#decode.float)(Token) of
                Value -> continue(Binary, Next, Acc, Stack, D, Value)
            catch
                _:_ -> error({unexpected_sequence, Token})
            end
    end.

number_end(Binary, Skip, Next, Acc, Stack, D, Value) when Next =:= byte_size(Binary) ->
    pause(Binary, Skip, Acc, Stack, D, {number, Value});
number_end(Binary, _Skip, Next, Acc, Stack, D, Value) ->
    continue(Binary, Next, Acc, Stack, D, Value).

array_start(Binary, Skip, Len, Acc, Stack, D) ->
    case byte(Binary, Skip + Len) of
        B when ?IS_SPACE(B) ->
            array_start(Binary, Skip, Len + 1, Acc, Stack, D);
        $] ->
            {Value, NewAcc} = empty_array(Acc, D),
            continue(Binary, Skip + Len + 1, NewAcc, Stack, D, Value);
        eof ->
            pause(Binary, Skip, Acc, Stack, D, value);
        _ ->
            value(Binary, Skip + Len, start(D#decode.array_start, Acc), [array, Acc | Stack], D)
    end.

empty_array(Acc, #decode{array_start = undefined, array_finish = undefined}) ->
    {[], Acc};
empty_array(Acc, #decode{array_start = Start, array_finish = undefined}) ->
    {lists:reverse(Start(Acc)), Acc};
empty_array(Acc, #decode{array_start = undefined, array_finish = Finish}) ->
    Finish([], Acc);
empty_array(Acc, #decode{array_start = Start, array_finish = Finish}) ->
    Finish(Start(Acc), Acc).

start(undefined, _Acc) -> [];
start(Start, Acc) -> Start(Acc).

array_push(Binary, Skip, Acc, Stack, D, Value) ->
    case byte(Binary, Skip) of
        B when ?IS_SPACE(B) ->
            array_push(Binary, Skip + 1, Acc, Stack, D, Value);
        $] ->
            Elements = push_element(D#decode.array_push, Value, Acc),
            [_, OldAcc | Outer] = Stack,
            {Array, NewAcc} = finish_array(D#decode.array_finish, Elements, OldAcc),
            continue(Binary, Skip + 1, NewAcc, Outer, D, Array);
        $, ->
            value(Binary, Skip + 1, push_element(D#decode.array_push, Value, Acc), Stack, D);
        _ ->
            unexpected(Binary, Skip, 0, 0, Acc, Stack, D, {array_push, Value})
    end.

push_element(undefined, Value, Acc) -> [Value | Acc];
push_element(Push, Value, Acc) -> Push(Value, Acc).

finish_array(undefined, Elements, OldAcc) -> {lists:reverse(Elements), OldAcc};
finish_array(Finish, Elements, OldAcc) -> Finish(Elements, OldAcc).

object_start(Binary, Skip, Len, Acc, Stack, D) ->
    case byte(Binary, Skip + Len) of
        B when ?IS_SPACE(B) ->
            object_start(Binary, Skip, Len + 1, Acc, Stack, D);
        $} ->
            {Value, NewAcc} = empty_object(Acc, D),
            continue(Binary, Skip + Len + 1, NewAcc, Stack, D, Value);
        $" ->
            Members = start(D#decode.object_start, Acc),
            string(Binary, Skip + Len + 1, Members, [object, Acc | Stack], D);
        _ ->
            unexpected(Binary, Skip, Len, 0, Acc, Stack, D, value)
    end.

empty_object(Acc, #decode{object_start = undefined, object_finish = undefined}) ->
    {#{}, Acc};
empty_object(Acc, #decode{object_start = Start, object_finish = undefined}) ->
    {maps:from_list(Start(Acc)), Acc};
empty_object(Acc, #decode{object_start = undefined, object_finish = Finish}) ->
    Finish([], Acc);
empty_object(Acc, #decode{object_start = Start, object_finish = Finish}) ->
    Finish(Start(Acc), Acc).

object_value(Binary, Skip, Acc, Stack, D, Key) ->
    case byte(Binary, Skip) of
        B when ?IS_SPACE(B) -> object_value(Binary, Skip + 1, Acc, Stack, D, Key);
        $: -> value(Binary, Skip + 1, Acc, [Key | Stack], D);
        _ -> unexpected(Binary, Skip, 0, 0, Acc, Stack, D, {object_value, Key})
    end.

object_push(Binary, Skip, Acc, Stack, D, Value, Key) ->
    case byte(Binary, Skip) of
        B when ?IS_SPACE(B) ->
            object_push(Binary, Skip + 1, Acc, Stack, D, Value, Key);
        $} ->
            Members = push_member(D#decode.object_push, Key, Value, Acc),
            [_, OldAcc | Outer] = Stack,
            {Object, NewAcc} = finish_object(D#decode.object_finish, Members, OldAcc),
            continue(Binary, Skip + 1, NewAcc, Outer, D, Object);
        $, ->
            object_key(
                Binary, Skip + 1, push_member(D#decode.object_push, Key, Value, Acc), Stack, D
            );
        _ ->
            unexpected(Binary, Skip, 0, 0, Acc, Stack, D, {object_push, Value, Key})
    end.

push_member(undefined, Key, Value, Acc) -> [{Key, Value} | Acc];
push_member(Push, Key, Value, Acc) -> Push(Key, Value, Acc).

finish_object(undefined, Members, OldAcc) -> {maps:from_list(Members), OldAcc};
finish_object(Finish, Members, OldAcc) -> Finish(Members, OldAcc).

object_key(Binary, Skip, Acc, Stack, D) ->
    case byte(Binary, Skip) of
        B when ?IS_SPACE(B) -> object_key(Binary, Skip + 1, Acc, Stack, D);
        $" -> string(Binary, Skip + 1, Acc, Stack, D);
        _ -> unexpected(Binary, Skip, 0, 0, Acc, Stack, D, object_key)
    end.

%% A value read: it goes to the innermost open container, or ends the walk.
continue(Binary, Skip, Acc, [], _D, Value) ->
    terminate(Binary, Skip, Skip, Acc, Value);
continue(Binary, Skip, Acc, [array | _] = Stack, D, Value) ->
    array_push(Binary, Skip, Acc, Stack, D, Value);
continue(Binary, Skip, Acc, [object | _] = Stack, D, Key) ->
    object_value(Binary, Skip, Acc, Stack, D, Key);
continue(Binary, Skip, Acc, [Key | Stack], D, Value) ->
    object_push(Binary, Skip, Acc, Stack, D, Value, Key).

%% After the value only spaces may follow to the end; otherwise the rest from the value's end is returned.
terminate(Binary, Skip, At, Acc, Value) ->
    case byte(Binary, At) of
        eof -> {Value, Acc, <<>>};
        B when ?IS_SPACE(B) -> terminate(Binary, Skip, At + 1, Acc, Value);
        _ -> {Value, Acc, binary_part(Binary, Skip, byte_size(Binary) - Skip)}
    end.
