%% OTP 29 crypto types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([
    cmac_cipher_algorithm/0,
    crypto_state/0,
    dh_private/0,
    dh_public/0,
    dss_digest_type/0,
    ec_named_curve/0,
    ecdsa_digest_type/0,
    engine_ref/0,
    hash_state/0,
    hmac_hash_algorithm/0,
    kem/0,
    key_id/0,
    mac_state/0,
    mldsa/0,
    password/0,
    pk_encrypt_decrypt_opts/0,
    pk_sign_verify_opts/0,
    rsa_digest_type/0,
    sha1/0,
    sha2/0,
    sha3/0,
    slh_dsa/0
]).
-type key_integer() :: integer() | binary().
-type kem() :: mlkem512 | mlkem768 | mlkem1024.
-type dh_public() :: key_integer().
-type dh_private() :: key_integer().
-type ec_named_curve() ::
    brainpoolP160r1
    | brainpoolP160t1
    | brainpoolP192r1
    | brainpoolP192t1
    | brainpoolP224r1
    | brainpoolP224t1
    | brainpoolP256r1
    | brainpoolP256t1
    | brainpoolP320r1
    | brainpoolP320t1
    | brainpoolP384r1
    | brainpoolP384t1
    | brainpoolP512r1
    | brainpoolP512t1
    | c2pnb163v1
    | c2pnb163v2
    | c2pnb163v3
    | c2pnb176v1
    | c2pnb208w1
    | c2pnb272w1
    | c2pnb304w1
    | c2pnb368w1
    | c2tnb191v1
    | c2tnb191v2
    | c2tnb191v3
    | c2tnb239v1
    | c2tnb239v2
    | c2tnb239v3
    | c2tnb359v1
    | c2tnb431r1
    | ipsec3
    | ipsec4
    | prime192v1
    | prime192v2
    | prime192v3
    | prime239v1
    | prime239v2
    | prime239v3
    | prime256v1
    | secp112r1
    | secp112r2
    | secp128r1
    | secp128r2
    | secp160k1
    | secp160r1
    | secp160r2
    | secp192k1
    | secp192r1
    | secp224k1
    | secp224r1
    | secp256k1
    | secp256r1
    | secp384r1
    | secp521r1
    | sect113r1
    | sect113r2
    | sect131r1
    | sect131r2
    | sect163k1
    | sect163r1
    | sect163r2
    | sect193r1
    | sect193r2
    | sect233k1
    | sect233r1
    | sect239k1
    | sect283k1
    | sect283r1
    | sect409k1
    | sect409r1
    | sect571k1
    | sect571r1
    | wtls1
    | wtls10
    | wtls11
    | wtls12
    | wtls3
    | wtls4
    | wtls5
    | wtls6
    | wtls7
    | wtls8
    | wtls9.
-type rsa_digest_type() :: sha1() | sha2() | md5 | ripemd160.
-type dss_digest_type() :: sha1() | sha2().
-type ecdsa_digest_type() :: sha1() | sha2().
-type sha1() :: sha.
-type sha2() ::
    sha224 | sha256 | sha384 | sha512 | sha512_224 | sha512_256.
-type sha3() :: sha3_224 | sha3_256 | sha3_384 | sha3_512.
-type compatibility_only_hash() :: md5 | md4.
-opaque hash_state() :: reference().
-type hmac_hash_algorithm() ::
    sha1() | sha2() | sha3() | sm3 | compatibility_only_hash().
-type cmac_cipher_algorithm() ::
    aes_128_cbc
    | aes_192_cbc
    | aes_256_cbc
    | aes_cbc
    | blowfish_cbc
    | des_cbc
    | des_ede3_cbc
    | rc2_cbc.
-opaque mac_state() :: reference().
-opaque crypto_state() :: reference().
-type mldsa() :: mldsa44 | mldsa65 | mldsa87.
-type slh_dsa() ::
    slh_dsa_shake_128s
    | slh_dsa_shake_128f
    | slh_dsa_sha2_128s
    | slh_dsa_sha2_128f
    | slh_dsa_shake_192s
    | slh_dsa_shake_192f
    | slh_dsa_sha2_192s
    | slh_dsa_sha2_192f
    | slh_dsa_shake_256s
    | slh_dsa_shake_256f
    | slh_dsa_sha2_256s
    | slh_dsa_sha2_256f.
-type pk_sign_verify_opts() :: [rsa_sign_verify_opt()].
-type rsa_sign_verify_opt() ::
    {rsa_padding, rsa_sign_verify_padding()}
    | {rsa_pss_saltlen, integer()}
    | {rsa_mgf1_md, sha2()}.
-type rsa_sign_verify_padding() ::
    rsa_pkcs1_padding
    | rsa_pkcs1_pss_padding
    | rsa_x931_padding
    | rsa_no_padding.
-type pk_encrypt_decrypt_opts() :: [rsa_opt()] | rsa_compat_opts().
-type rsa_compat_opts() :: [{rsa_pad, rsa_padding()}] | rsa_padding().
-type rsa_padding() ::
    rsa_pkcs1_padding
    | rsa_pkcs1_oaep_padding
    | rsa_x931_padding
    | rsa_no_padding.
-type rsa_opt() ::
    {rsa_padding, rsa_padding()}
    | {signature_md, atom()}
    | {rsa_mgf1_md, sha}
    | {rsa_oaep_label, binary()}
    | {rsa_oaep_md, sha}.
-type key_id() :: string() | binary().
-type password() :: string() | binary().
-type engine_ref() :: term().
