-module(service_answer).
-export([
    head/1,
    body/1,
    head_mismatch/1,
    skipped/1,
    reached/1,
    strict/1,
    fallback/1,
    match_once/1,
    match_stop/1,
    construct/1,
    inspect/1,
    heap_guard/1,
    extracted/1,
    heap_error/1,
    id/1,
    first/2,
    integer_guard/1,
    integer_body/1,
    integer_budget/1,
    float_guard/1,
    float_body/1,
    float_literal/1,
    map_guard/1,
    map_body/1,
    map_pattern/1,
    bits_guard/1,
    bits_body/1,
    bits_extract/1,
    record_guard/1,
    record_body/1,
    record_inspect/1,
    record_error/1,
    range_guard/1,
    range_body/1
]).
-record(fault_record, {a}).
head(X) when is_integer(abs(X)); true -> ok.
body(X) -> is_integer(abs(X)).
head_mismatch(0) when is_integer(abs(0)); true -> ok.
skipped(X) -> true orelse is_integer(abs(X)).
reached(X) when (is_integer(abs(X)) orelse true); true -> ok.
strict(X) -> true or is_integer(abs(X)).
fallback(X) when is_integer(abs(X)) -> ok;
fallback(_) -> recovered.
match_once(X) ->
    Y = is_integer(abs(X)),
    Y.
match_stop(X) ->
    false = is_integer(abs(X)),
    hd([]).
construct(X) -> {X, [X], {X}}.
inspect({X}) -> X;
inspect(X) -> X.
heap_guard(X) when element(1, {X}) =:= X -> X;
heap_guard(_) -> recovered.
extracted({_, [X | T]}) -> first(id({X, T}), construct(X)).
heap_error(X) -> impossible = {X, [X]}.
id(I) -> I.
first(Fst, _Snd) -> Fst.
integer_guard(X) when is_integer(X bsl 100); true -> ok;
integer_guard(_) -> recovered.
integer_body(X) -> X bsl 100.
integer_budget(X) when is_integer(1 bsl X) -> fits;
integer_budget(_) -> recovered.

float_guard(X) when is_float(float(X)); true -> ok;
float_guard(_) -> recovered.
float_body(X) -> float(X).
float_literal(_) -> 1.5.

map_guard(X) when is_map(#{a => X}); true -> ok;
map_guard(_) -> recovered.
map_body(X) -> #{a => X}.
map_pattern(X) ->
    #{(tuple_size({X}) + 1) := V} = #{2 => X},
    V.

bits_guard(X) when bit_size(<<X:16>>) > 0; true -> ok;
bits_guard(_) -> recovered.
bits_body(X) -> <<X:16>>.
bits_extract(B) ->
    <<X:8, T/bitstring>> = B,
    {X, T}.
record_guard(X) when is_record(#fault_record{a = X}, fault_record); true -> ok;
record_guard(_) -> recovered.
record_body(X) -> #fault_record{a = X}.
record_inspect(X) -> X#fault_record.a.
record_error(X) -> ({other, X})#fault_record.a.
range_guard(X) when is_integer(abs(X), 0, 100); true -> ok;
range_guard(_) -> recovered.
range_body(X) -> is_integer(abs(X), 0, 100).
