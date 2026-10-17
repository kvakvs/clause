-module(base64_codec).
-export([main/1]).

%% base64 (plan 21 step LIB5): encodings with both alphabets and paddings, strict and lenient decoding, and the
%% error reasons of OTP's state machines for truncated, invalid and odd inputs.
main(_) ->
    Bytes = [
        <<>>, <<0>>, <<255>>, <<"ab">>, <<"abc">>, <<"Hello World">>, <<251, 255, 191, 62, 63>>
    ],
    show(encode, [
        [base64:encode(B) || B <- Bytes],
        [base64:encode(B, #{mode => urlsafe}) || B <- Bytes],
        [base64:encode(B, #{padding => false}) || B <- Bytes],
        [base64:encode_to_string(B) || B <- Bytes],
        base64:encode("Hello World"),
        base64:encode_to_string("ab", #{mode => urlsafe, padding => false}),
        base64:encode([256, 300, 7]),
        base64:encode(<<"x">>, #{mode => other, padding => maybe_not, other => 1})
    ]),
    Encoded = [base64:encode(B) || B <- Bytes],
    show(decode, [
        [base64:decode(E) || E <- Encoded],
        [base64:decode_to_string(E) || E <- Encoded],
        [base64:mime_decode(E) || E <- Encoded],
        reason(fun() -> base64:decode("SGVs bG8g\r\nV29y\tbGQ=") end),
        reason(fun() -> base64:decode(<<"SGVsbG8gV29ybGQ= \n">>) end),
        reason(fun() -> base64:decode(<<"_w==">>, #{mode => urlsafe}) end),
        reason(fun() -> base64:decode(<<"SGVsbG8">>, #{padding => false}) end),
        reason(fun() -> base64:decode(<<"SGVsbA">>, #{padding => false}) end),
        reason(fun() -> base64:decode("YQ", #{padding => false}) end),
        reason(fun() -> base64:decode_to_string(<<"YWJj">>) end),
        reason(fun() -> base64:mime_decode(<<"SGVs!bG8*gV29ybGQ=">>) end),
        reason(fun() -> base64:mime_decode(<<"YQ==YWJj">>) end),
        reason(fun() -> base64:mime_decode(<<"YW=Jj">>) end),
        reason(fun() -> base64:mime_decode(<<"YQ">>, #{padding => false}) end),
        reason(fun() -> base64:mime_decode_to_string("SGVs#bG8=garbage") end),
        reason(fun() -> base64:mime_decode("YWJj=,=YQ==") end)
    ]),
    show(errors, [
        reason(fun() -> base64:decode(<<"Y">>) end),
        reason(fun() -> base64:decode(<<"YQ">>) end),
        reason(fun() -> base64:decode(<<"YWI">>) end),
        reason(fun() -> base64:decode(<<"YQ==x">>) end),
        reason(fun() -> base64:decode(<<"YQ==Y">>) end),
        reason(fun() -> base64:decode(<<"Y!Jj">>) end),
        reason(fun() -> base64:decode(<<"=WJj">>) end),
        reason(fun() -> base64:decode(<<"YW=j">>) end),
        reason(fun() -> base64:decode(<<"Y!==">>) end),
        reason(fun() -> base64:decode(<<"Y=!=">>) end),
        reason(fun() -> base64:decode_to_string("Y!Jj") end),
        reason(fun() -> base64:decode_to_string("Y!==") end),
        reason(fun() -> base64:decode(id([$Y, $Q | x])) end),
        reason(fun() -> base64:decode(id([$Y, a])) end),
        reason(fun() -> base64:decode(id([-1])) end),
        reason(fun() -> base64:decode(id([600])) end),
        reason(fun() -> base64:decode(id(x)) end),
        reason(fun() -> base64:decode(<<"YQ==">>, id(x)) end),
        reason(fun() -> base64:mime_decode(<<"Y">>) end),
        reason(fun() -> base64:mime_decode(<<"YWJ">>) end),
        reason(fun() -> base64:mime_decode(id([a])) end),
        reason(fun() -> base64:encode(id([a])) end),
        reason(fun() -> base64:encode(id([1, 2 | x])) end),
        reason(fun() -> base64:encode(id([a | x])) end),
        reason(fun() -> base64:encode(id([1, 2, 3 | x])) end),
        reason(fun() -> base64:encode(id([-1])) end),
        reason(fun() -> base64:encode(id(<<1:3>>)) end),
        reason(fun() -> base64:encode(<<"x">>, id(x)) end),
        reason(fun() -> base64:encode("x", id(x)) end),
        reason(fun() -> base64:encode_to_string(id(x)) end)
    ]),
    io:format("base64_codec: ok~n").

id(Value) -> Value.

show(Name, Values) -> io:format("~w: ~w~n", [Name, Values]).

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.
