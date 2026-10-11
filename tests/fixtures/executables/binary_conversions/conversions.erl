-module(conversions).
-export([main/1]).

%% Binary, atom and number conversions (plan step 41D) with OTP's results and badarg/system_limit edges. Calls go
%% through apply/3 so the OTP compiler cannot fold them; direct/0 makes a few direct calls.
main(_) ->
    Cases = [
        {atom_to_binary, [caf]},
        {atom_to_binary, [list_to_atom([104, 233, 108]), latin1]},
        {atom_to_binary, [list_to_atom([104, 233, 108]), utf8]},
        {atom_to_binary, [list_to_atom([300]), latin1]},
        {atom_to_binary, [a, bogus]},
        {binary_to_atom, [<<"abc">>]},
        {binary_to_atom, [<<255>>, latin1]},
        {binary_to_atom, [<<255>>, utf8]},
        {binary_to_atom, [<<16#c3, 16#a9>>, latin1]},
        {binary_to_atom, [<<16#c3, 16#a9>>, unicode]},
        {binary_to_atom, [list_to_binary([$a || _ <- lists:seq(1, 256)])]},
        {binary_to_existing_atom, [<<"never_ever_exists_zzz">>]},
        {binary_to_existing_atom, [<<"ok">>, utf8]},
        {list_to_existing_atom, ["never_ever_exists_zzz"]},
        {list_to_existing_atom, ["error"]},
        {integer_to_binary, [-12345]},
        {integer_to_binary, [255, 16]},
        {integer_to_binary, [-255, 2]},
        {integer_to_binary, [1 bsl 100, 36]},
        {integer_to_binary, [1, 37]},
        {binary_to_integer, [<<"+0012">>]},
        {binary_to_integer, [<<"-ff">>, 16]},
        {binary_to_integer, [<<"">>]},
        {binary_to_integer, [<<" 1">>]},
        {binary_to_integer, [<<"123456789012345678901234567890">>]},
        {float_to_binary, [1.5]},
        {float_to_binary, [1.5, [short]]},
        {float_to_binary, [0.1, [{decimals, 3}, compact]]},
        {float_to_binary, [1.0, [{scientific, 2}]]},
        {float_to_binary, [1.0, [bogus]]},
        {binary_to_float, [<<"-0.0">>]},
        {binary_to_float, [<<"2.5E-3">>]},
        {binary_to_float, [<<"1.5 ">>]},
        {list_to_float, ["1.0e10"]},
        {list_to_float, ["1e10"]},
        {list_to_float, ["1."]},
        {list_to_float, [".5"]},
        {list_to_float, ["+1.5"]},
        {list_to_float, ["01.50"]},
        {list_to_float, ["1.0E+2"]},
        {list_to_float, ["1.0e400"]},
        {list_to_float, ["1.0e-400"]},
        {list_to_float, ["1.5e+"]},
        {binary_to_list, [<<1, 2, 3>>, 2, 3]},
        {binary_to_list, [<<1, 2, 3>>, 3, 3]},
        {binary_to_list, [<<1, 2, 3>>, 2, 1]},
        {binary_to_list, [<<1, 2, 3>>, 0, 1]},
        {binary_to_list, [<<1, 2, 3>>, 1, 4]},
        {binary_to_list, [<<1, 2, 3:4>>, 1, 1]},
        {bitstring_to_list, [<<1, 2:3>>]},
        {bitstring_to_list, [<<1, 2>>]},
        {bitstring_to_list, [<<>>]},
        {list_to_bitstring, [[1, <<2:3>>, [<<3:5>>] | <<1:1>>]]},
        {list_to_bitstring, [[[] | <<>>]]},
        {list_to_bitstring, [[256]]},
        {list_to_bitstring, [<<1:1>>]},
        {split_binary, [<<1, 2, 3:4>>, 1]},
        {split_binary, [<<1, 2>>, 0]},
        {split_binary, [<<1, 2>>, 2]},
        {split_binary, [<<1, 2, 3>>, 4]},
        {iolist_size, [[1, <<2, 3>>, [4] | <<5>>]]},
        {iolist_size, [<<1, 2, 3>>]},
        {iolist_size, [[<<1:1>>]]},
        {iolist_size, [abc]},
        {iolist_size, [[1 | 2]]},
        {list_to_pid, ["<1.5.0>"]},
        {list_to_pid, ["<0.5>"]},
        {list_to_ref, ["#Ref<0.1.2.3>"]},
        {list_to_ref, ["#Ref<0.0.0.0>"]},
        {list_to_ref, ["#Ref<0.1.2>"]}
    ],
    [io:format("~p~n", [run(Name, Arguments)]) || {Name, Arguments} <- Cases],
    io:format("~p~n", [direct()]),
    io:format("conversions: ok~n").

% The value of erlang:Name(Arguments...), or its error.
run(Name, Arguments) ->
    try
        apply(erlang, Name, Arguments)
    catch
        Class:Reason -> {Class, Reason}
    end.

% Direct calls of the builtins, and pid and reference text round trips.
direct() ->
    Ref = make_ref(),
    {
        binary_to_atom(atom_to_binary(id(ok))),
        binary_to_integer(integer_to_binary(id(-7))),
        binary_to_float(float_to_binary(id(2.5), [short])),
        list_to_pid(pid_to_list(self())) =:= self(),
        list_to_ref(ref_to_list(Ref)) =:= Ref,
        iolist_size([id(1), <<2>>]),
        split_binary(id(<<1, 2, 3>>), 1),
        bitstring_to_list(id(<<7:3>>))
    }.

% Hide a value from constant folding.
id(Value) -> Value.
