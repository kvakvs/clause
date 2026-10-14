-module(binary).
-export([bin_to_list/2,bin_to_list/3,compile_pattern/1,decode_hex/1,decode_unsigned/1,decode_unsigned/2,encode_hex/1,encode_hex/2,encode_unsigned/1,encode_unsigned/2,join/2,list_to_bin/1,longest_common_suffix/1,matches/2,matches/3,referenced_byte_size/1]).
%% Tracing stand-in: simple byte-pattern versions of OTP's binary NIFs.
-export([part/2, part/3, match/2, match/3, split/2, split/3, replace/3, replace/4, at/2, first/1, last/1,
         bin_to_list/1, copy/1, copy/2, longest_common_prefix/1]).
part(Bin, {Pos, Len}) -> part(Bin, Pos, Len).
part(Bin, Pos, Len) when Len < 0 -> part(Bin, Pos + Len, -Len);
part(Bin, Pos, Len) -> <<_:Pos/binary, P:Len/binary, _/binary>> = Bin, P.
match(Bin, Pat) -> match(Bin, Pat, []).
match(Bin, Pats, _) when is_list(Pats) -> first_match(Bin, Pats, 0);
match(Bin, Pat, Opts) -> match(Bin, [Pat], Opts).
first_match(Bin, _, At) when At >= byte_size(Bin) -> nomatch;
first_match(Bin, Pats, At) ->
    case [P || P <- Pats, prefix_at(Bin, At, P)] of
        [P | _] -> {At, byte_size(P)};
        [] -> first_match(Bin, Pats, At + 1)
    end.
prefix_at(Bin, At, P) -> byte_size(Bin) - At >= byte_size(P) andalso binary:part(Bin, At, byte_size(P)) =:= P.
split(Bin, Pat) -> split(Bin, Pat, []).
split(Bin, Pat, Opts) ->
    Pats = if is_list(Pat) -> Pat; true -> [Pat] end,
    case first_match(Bin, Pats, 0) of
        nomatch -> [Bin];
        {At, L} ->
            Rest = binary:part(Bin, At + L, byte_size(Bin) - At - L),
            Head = binary:part(Bin, 0, At),
            case lists:member(global, Opts) of
                true -> [Head | split(Rest, Pat, Opts)];
                false -> [Head, Rest]
            end
    end.
replace(Bin, Pat, Rep) -> replace(Bin, Pat, Rep, []).
replace(Bin, Pat, Rep, Opts) ->
    Parts = split(Bin, Pat, Opts),
    iolist_to_binary(lists:join(Rep, Parts)).
at(Bin, Pos) -> <<_:Pos/binary, B, _/binary>> = Bin, B.
first(<<B, _/binary>>) -> B.
last(Bin) -> binary:at(Bin, byte_size(Bin) - 1).
bin_to_list(Bin) -> binary_to_list(Bin).
copy(Bin) -> Bin.
copy(Bin, N) -> iolist_to_binary(lists:duplicate(N, Bin)).
longest_common_prefix(_) -> erlang:error({tracing_stand_in, longest_common_prefix}).
bin_to_list(_,_) -> erlang:error({tracing_stand_in, bin_to_list, 2}).
bin_to_list(_,_,_) -> erlang:error({tracing_stand_in, bin_to_list, 3}).
compile_pattern(_) -> erlang:error({tracing_stand_in, compile_pattern, 1}).
decode_hex(_) -> erlang:error({tracing_stand_in, decode_hex, 1}).
decode_unsigned(_) -> erlang:error({tracing_stand_in, decode_unsigned, 1}).
decode_unsigned(_,_) -> erlang:error({tracing_stand_in, decode_unsigned, 2}).
encode_hex(_) -> erlang:error({tracing_stand_in, encode_hex, 1}).
encode_hex(_,_) -> erlang:error({tracing_stand_in, encode_hex, 2}).
encode_unsigned(_) -> erlang:error({tracing_stand_in, encode_unsigned, 1}).
encode_unsigned(_,_) -> erlang:error({tracing_stand_in, encode_unsigned, 2}).
join(_,_) -> erlang:error({tracing_stand_in, join, 2}).
list_to_bin(_) -> erlang:error({tracing_stand_in, list_to_bin, 1}).
longest_common_suffix(_) -> erlang:error({tracing_stand_in, longest_common_suffix, 1}).
matches(_,_) -> erlang:error({tracing_stand_in, matches, 2}).
matches(_,_,_) -> erlang:error({tracing_stand_in, matches, 3}).
referenced_byte_size(_) -> erlang:error({tracing_stand_in, referenced_byte_size, 1}).
