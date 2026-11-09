%% OTP's pubkey_pbe as libraries call it (plan 21 step MIM7, docs/libraries/library.md#undocumented-modules): PBKDF2
%% over a given pseudo-random function. PEM encryption is public_key's; the rest is declined.
-module(pubkey_pbe).
-export([pbdkdf2/7]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    decode/4,
    decrypt_parameters/1,
    encode/4,
    encrypt_parameters/1,
    pbdkdf1/4
]).

% PBKDF2 (RFC 8018): Length bytes from blocks of Count rounds, Prf(PrfHash, Password, Data, PrfLength)
% called as OTP's calls it, block by block.
pbdkdf2(Password, Salt, Count, Length, Prf, PrfHash, PrfLength) ->
    Blocks = (Length + PrfLength - 1) div PrfLength,
    Key = <<<<(block(Password, Salt, Count, I, Prf, PrfHash, PrfLength))/binary>> || I <- lists:seq(1, Blocks)>>,
    binary:part(Key, 0, Length).

block(Password, Salt, Count, I, Prf, PrfHash, PrfLength) ->
    First = Prf(PrfHash, Password, [Salt, <<I:32>>], PrfLength),
    rounds(Count - 1, First, First, Password, Prf, PrfHash, PrfLength).

rounds(0, _, Acc, _, _, _, _) ->
    Acc;
rounds(N, Previous, Acc, Password, Prf, PrfHash, PrfLength) ->
    Next = Prf(PrfHash, Password, Previous, PrfLength),
    rounds(N - 1, Next, crypto:exor(Acc, Next), Password, Prf, PrfHash, PrfLength).

decode(_, _, _, _) -> erlang:error({not_implemented, decode, 4}).
decrypt_parameters(_) -> erlang:error({not_implemented, decrypt_parameters, 1}).
encode(_, _, _, _) -> erlang:error({not_implemented, encode, 4}).
encrypt_parameters(_) -> erlang:error({not_implemented, encrypt_parameters, 1}).
pbdkdf1(_, _, _, _) -> erlang:error({not_implemented, pbdkdf1, 4}).
