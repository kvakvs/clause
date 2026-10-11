-module(main).
-export([main/1]).

%% binary_to_term/1 on deterministically mutated and truncated encodings (plan step 41E): every input decodes or
%% raises badarg (notsup for compressed data); none crashes the program.
main(_) ->
    Seeds = [
        term_to_binary({atom, 1, -1, 1 bsl 80, 1.5, "text", [1 | 2], <<1, 2:3>>, #{a => [b]}}),
        term_to_binary([self(), make_ref(), fun lists:map/2, {}, #{}, []]),
        term_to_binary(lists:seq(1, 300))
    ],
    {Decoded, Rejected} = lists:foldl(
        fun(Seed, Counts) -> mutate(Seed, 2000, 12345, Counts) end, {0, 0}, Seeds
    ),
    true = Decoded > 0 andalso Rejected > 0,
    io:format("etf_fuzz: ok~n").

% Decode Count mutations of Seed, counting decoded and rejected inputs.
mutate(_, 0, _, Counts) ->
    Counts;
mutate(Seed, Count, State, {Decoded, Rejected}) ->
    Next = (State * 1103515245 + 12345) rem 2147483648,
    Input = variant(Seed, Next),
    Counts =
        try binary_to_term(Input) of
            _ -> {Decoded + 1, Rejected}
        catch
            error:badarg -> {Decoded, Rejected + 1};
            error:notsup -> {Decoded, Rejected + 1}
        end,
    mutate(Seed, Count - 1, Next, Counts).

% Truncate Seed or replace one of its bytes, as State selects.
variant(Seed, State) ->
    Size = byte_size(Seed),
    At = State rem Size,
    case State rem 3 of
        0 ->
            <<Prefix:At/binary, _/binary>> = Seed,
            Prefix;
        _ ->
            <<Prefix:At/binary, _, Rest/binary>> = Seed,
            <<Prefix/binary, (State div 7 rem 256), Rest/binary>>
    end.
