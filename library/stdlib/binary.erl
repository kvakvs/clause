%% Clause's replacement for OTP's binary (docs/library.md#runtime-module-replacements), generated from
%% binary.terms by tests/compiler/library/library_modules.py; edited by hand from here on. A stub raises
%% error({not_implemented, Name, Arity}). The other functions are runtime builtins that replace their stubs
%% (plan step 41W, docs/builtins.md#binary-module); replace/3,4 and list_to_bin/1 are Erlang over them.
-module(binary).
-export([
    at/2,
    bin_to_list/1,
    bin_to_list/2,
    bin_to_list/3,
    compile_pattern/1,
    copy/1,
    copy/2,
    decode_hex/1,
    decode_unsigned/1,
    decode_unsigned/2,
    encode_hex/1,
    encode_hex/2,
    encode_unsigned/1,
    encode_unsigned/2,
    first/1,
    join/2,
    last/1,
    list_to_bin/1,
    longest_common_prefix/1,
    longest_common_suffix/1,
    match/2,
    match/3,
    matches/2,
    matches/3,
    part/2,
    part/3,
    referenced_byte_size/1,
    replace/3,
    replace/4,
    split/2,
    split/3
]).
-include("binary.types.hrl").

at(_, _) -> erlang:error({not_implemented, at, 2}).
bin_to_list(_) -> erlang:error({not_implemented, bin_to_list, 1}).
bin_to_list(_, _) -> erlang:error({not_implemented, bin_to_list, 2}).
bin_to_list(_, _, _) -> erlang:error({not_implemented, bin_to_list, 3}).
compile_pattern(_) -> erlang:error({not_implemented, compile_pattern, 1}).
copy(_) -> erlang:error({not_implemented, copy, 1}).
copy(_, _) -> erlang:error({not_implemented, copy, 2}).
decode_hex(_) -> erlang:error({not_implemented, decode_hex, 1}).
decode_unsigned(_) -> erlang:error({not_implemented, decode_unsigned, 1}).
decode_unsigned(_, _) -> erlang:error({not_implemented, decode_unsigned, 2}).
encode_hex(_) -> erlang:error({not_implemented, encode_hex, 1}).
encode_hex(_, _) -> erlang:error({not_implemented, encode_hex, 2}).
encode_unsigned(_) -> erlang:error({not_implemented, encode_unsigned, 1}).
encode_unsigned(_, _) -> erlang:error({not_implemented, encode_unsigned, 2}).
first(_) -> erlang:error({not_implemented, first, 1}).
join(_, _) -> erlang:error({not_implemented, join, 2}).
last(_) -> erlang:error({not_implemented, last, 1}).
longest_common_prefix(_) -> erlang:error({not_implemented, longest_common_prefix, 1}).
longest_common_suffix(_) -> erlang:error({not_implemented, longest_common_suffix, 1}).
match(_, _) -> erlang:error({not_implemented, match, 2}).
match(_, _, _) -> erlang:error({not_implemented, match, 3}).
matches(_, _) -> erlang:error({not_implemented, matches, 2}).
matches(_, _, _) -> erlang:error({not_implemented, matches, 3}).
part(_, _) -> erlang:error({not_implemented, part, 2}).
part(_, _, _) -> erlang:error({not_implemented, part, 3}).
referenced_byte_size(_) -> erlang:error({not_implemented, referenced_byte_size, 1}).
split(_, _) -> erlang:error({not_implemented, split, 2}).
split(_, _, _) -> erlang:error({not_implemented, split, 3}).

%% Subject with the first match of Pattern replaced (plan step 41W).
replace(Subject, Pattern, Replacement) -> replace(Subject, Pattern, Replacement, []).

%% Subject with the first match, or every match with global, replaced by Replacement: a binary, where
%% {insert_replaced, Positions} inserts the match, or a fun of the match returning a binary.
replace(Subject, Pattern, Replacement, Options) when
    is_binary(Subject), is_binary(Replacement) orelse is_function(Replacement, 1), is_list(Options)
->
    {Global, Insert, Search} = replace_options(Options, false, [], []),
    check_insert(Replacement, Insert),
    Matches =
        case Global of
            true ->
                binary:matches(Subject, Pattern, Search);
            false ->
                case binary:match(Subject, Pattern, Search) of
                    nomatch -> [];
                    Match -> [Match]
                end
        end,
    iolist_to_binary(replaced(Subject, 0, Matches, Replacement, Insert));
replace(_, _, _, _) ->
    erlang:error(badarg).

%% The global flag, the sorted insert positions and the search options of replace/4.
replace_options([global | Rest], _, Insert, Search) ->
    replace_options(Rest, true, Insert, Search);
replace_options([{insert_replaced, Position} | Rest], Global, _, Search) when
    is_integer(Position)
->
    replace_options(Rest, Global, [Position], Search);
replace_options([{insert_replaced, Positions} | Rest], Global, _, Search) when is_list(Positions) ->
    replace_options(Rest, Global, lists:sort(Positions), Search);
replace_options([{scope, _} = Scope | Rest], Global, Insert, Search) ->
    replace_options(Rest, Global, Insert, [Scope | Search]);
replace_options([], Global, Insert, Search) ->
    {Global, Insert, Search};
replace_options(_, _, _, _) ->
    erlang:error(badarg).

%% Insert positions need a binary replacement and must lie within it.
check_insert(_, []) ->
    ok;
check_insert(Replacement, Insert) when is_binary(Replacement) ->
    [check_position(Position, byte_size(Replacement)) || Position <- Insert],
    ok;
check_insert(_, _) ->
    erlang:error(badarg).

check_position(Position, Size) when is_integer(Position), Position >= 0, Position =< Size -> ok;
check_position(_, _) -> erlang:error(badarg).

%% The parts of Subject from Offset on, with each match replaced.
replaced(Subject, Offset, [{Start, Length} | Rest], Replacement, Insert) ->
    Match = binary:part(Subject, Start, Length),
    [
        binary:part(Subject, Offset, Start - Offset),
        replacement(Match, Replacement, Insert, 0)
        | replaced(Subject, Start + Length, Rest, Replacement, Insert)
    ];
replaced(Subject, Offset, [], _, _) ->
    [binary:part(Subject, Offset, byte_size(Subject) - Offset)].

%% What one match becomes: the fun's binary result, or Replacement with Match inserted at each position.
replacement(Match, Fun, _, _) when is_function(Fun, 1) ->
    case Fun(Match) of
        Result when is_binary(Result) -> Result;
        _ -> erlang:error(badarg)
    end;
replacement(Match, Replacement, [Position | Rest], Done) ->
    [
        binary:part(Replacement, Done, Position - Done),
        Match
        | replacement(Match, Replacement, Rest, Position)
    ];
replacement(_, Replacement, [], Done) ->
    binary:part(Replacement, Done, byte_size(Replacement) - Done).

%% The binary of an iolist, as erlang:list_to_binary/1 (plan step 41W).
list_to_bin(List) -> erlang:list_to_binary(List).
