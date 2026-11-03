%% mnesia's default hash module of fragmented tables (plan 21 step MIM1G, docs/libraries/mnesia.md#fragmented-tables):
%% linear hashing. Fragments split in turn, so the number of fragments grows one at a time, and a key's fragment is its
%% hash over twice the last power of two, folded back when that fragment does not exist yet. The state is OTP's record,
%% stored among the table's frag_properties.
-module(mnesia_frag_hash).
-export([init_state/2, add_frag/1, del_frag/1, key_to_frag_number/2, match_spec_to_frag_numbers/2]).

%% States from before phash2 still hash with phash.
-compile([{nowarn_deprecated_function, [{erlang, phash, 2}]}]).

%% The hash state: the fragments, the next fragment to split, the doublings so far and the hash function (phash2;
%% phash in states from before OTP R13).
-record(hash_state, {n_fragments, next_n_to_split, n_doubles, function}).

% A new table's state: one fragment.
init_state(_Tab, undefined) ->
    #hash_state{n_fragments = 1, next_n_to_split = 1, n_doubles = 0, function = phash2}.

% A new last fragment, filled from the next fragment to split: {State, [Split], [New]}.
add_frag(#hash_state{n_fragments = N, next_n_to_split = Split, n_doubles = Doubles} = State) ->
    New = N + 1,
    State2 =
        case Split + 1 =:= power2(Doubles) + 1 of
            true -> State#hash_state{n_fragments = New, next_n_to_split = 1, n_doubles = Doubles + 1};
            false -> State#hash_state{n_fragments = New, next_n_to_split = Split + 1}
        end,
    {State2, [Split], [New]};
add_frag(OldState) ->
    add_frag(converted(OldState)).

% The last fragment goes, its records into the fragment it split from: {State, [Last], [Merged]}.
del_frag(#hash_state{n_fragments = N, next_n_to_split = Split, n_doubles = Doubles} = State) ->
    case Split - 1 of
        Merged when Merged < 1 ->
            Halved = power2(Doubles - 1),
            {State#hash_state{n_fragments = N - 1, next_n_to_split = Halved, n_doubles = Doubles - 1}, [N], [Halved]};
        Merged ->
            {State#hash_state{n_fragments = N - 1, next_n_to_split = Merged}, [N], [Merged]}
    end;
del_frag(OldState) ->
    del_frag(converted(OldState)).

% A key's fragment number.
key_to_frag_number(#hash_state{function = Function, n_fragments = N, n_doubles = Doubles}, Key) ->
    case hash(Function, Key, power2(Doubles + 1)) of
        A when A > N -> A - power2(Doubles);
        A -> A
    end;
key_to_frag_number(OldState, Key) ->
    key_to_frag_number(converted(OldState), Key).

hash(phash2, Key, Range) -> erlang:phash2(Key, Range) + 1;
hash(phash, Key, Range) -> erlang:phash(Key, Range).

% The fragments a match specification can match: the key's when a one-clause head binds the key, else all.
match_spec_to_frag_numbers(#hash_state{n_fragments = N} = State, MatchSpec) ->
    case MatchSpec of
        [{Head, _, _}] when is_tuple(Head), tuple_size(Head) > 2 ->
            case mnesia:has_var(element(2, Head)) of
                false -> [key_to_frag_number(State, element(2, Head))];
                true -> lists:seq(1, N)
            end;
        _ ->
            lists:seq(1, N)
    end;
match_spec_to_frag_numbers(OldState, MatchSpec) ->
    match_spec_to_frag_numbers(converted(OldState), MatchSpec).

% A state from before phash2, {hash_state, N, Split, Doubles}.
converted({hash_state, N, Split, Doubles}) ->
    #hash_state{n_fragments = N, next_n_to_split = Split, n_doubles = Doubles, function = phash}.

power2(Y) -> 1 bsl Y.
