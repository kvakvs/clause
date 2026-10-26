%% clause_crypto: the builtins of crypto over OpenSSL (plan 21 step LIB79, runtime/src/builtins/crypto/), in the crypto
%% runtime archive a program links when it calls crypto (docs/builtins.md#crypto-module). Each catalog builtin replaces
%% its clause_builtin_placeholder body; library/crypto/crypto.erl answers OTP's crypto functions over them.
-module(clause_crypto).
-export([
    hash_algorithms/0,
    cipher_algorithms/0,
    mac_algorithms/0,
    pubkey_algorithms/0,
    kem_algorithms/0,
    curve_algorithms/0,
    rsa_opts_algorithms/0,
    info_lib/0,
    info/0,
    hash_info/1,
    hash/2,
    hash_init/1,
    hash_update/2,
    hash_final/1,
    hash_final_xof/2,
    mac/4,
    mac_init/3,
    mac_update/2,
    mac_final/1,
    pbkdf2_hmac/5,
    hash_equals/2,
    strong_rand_bytes/1,
    strong_rand_range/1,
    rand_uniform/2,
    rand_seed/1,
    mod_exp/4,
    exor/2,
    cipher_info/1,
    crypto_init/4,
    crypto_update/2,
    crypto_final/1,
    crypto_get_data/1,
    crypto_one_time/5,
    aead_cipher/7,
    aead_cipher_init/4,
    aead_cipher/4,
    info_fips/0,
    enable_fips_mode/1
]).

hash_algorithms() -> clause_builtin_placeholder.
cipher_algorithms() -> clause_builtin_placeholder.
mac_algorithms() -> clause_builtin_placeholder.
pubkey_algorithms() -> clause_builtin_placeholder.
kem_algorithms() -> clause_builtin_placeholder.
curve_algorithms() -> clause_builtin_placeholder.
rsa_opts_algorithms() -> clause_builtin_placeholder.
info_lib() -> clause_builtin_placeholder.
info() -> clause_builtin_placeholder.
hash_info(_) -> clause_builtin_placeholder.
hash(_, _) -> clause_builtin_placeholder.
hash_init(_) -> clause_builtin_placeholder.
hash_update(_, _) -> clause_builtin_placeholder.
hash_final(_) -> clause_builtin_placeholder.
hash_final_xof(_, _) -> clause_builtin_placeholder.
mac(_, _, _, _) -> clause_builtin_placeholder.
mac_init(_, _, _) -> clause_builtin_placeholder.
mac_update(_, _) -> clause_builtin_placeholder.
mac_final(_) -> clause_builtin_placeholder.
pbkdf2_hmac(_, _, _, _, _) -> clause_builtin_placeholder.
hash_equals(_, _) -> clause_builtin_placeholder.
strong_rand_bytes(_) -> clause_builtin_placeholder.
strong_rand_range(_) -> clause_builtin_placeholder.
rand_uniform(_, _) -> clause_builtin_placeholder.
rand_seed(_) -> clause_builtin_placeholder.
mod_exp(_, _, _, _) -> clause_builtin_placeholder.
exor(_, _) -> clause_builtin_placeholder.
cipher_info(_) -> clause_builtin_placeholder.
crypto_init(_, _, _, _) -> clause_builtin_placeholder.
crypto_update(_, _) -> clause_builtin_placeholder.
crypto_final(_) -> clause_builtin_placeholder.
crypto_get_data(_) -> clause_builtin_placeholder.
crypto_one_time(_, _, _, _, _) -> clause_builtin_placeholder.
aead_cipher(_, _, _, _, _, _, _) -> clause_builtin_placeholder.
aead_cipher_init(_, _, _, _) -> clause_builtin_placeholder.
aead_cipher(_, _, _, _) -> clause_builtin_placeholder.
info_fips() -> clause_builtin_placeholder.
enable_fips_mode(_) -> clause_builtin_placeholder.
