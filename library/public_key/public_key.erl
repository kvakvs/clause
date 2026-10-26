%% Clause's replacement for OTP's public_key (docs/libraries/public_key.md, plan 21 step LIB80), written from OTP's
%% documentation: DER and PEM coding and certificate decoding (LIB80B) are clause_public_key_codec's, over the ASN.1
%% modules clau generates from library/public_key/asn1, keys and signatures (LIB80C) clause_public_key_keys', over
%% crypto; validation (LIB80D) raises {not_implemented, Name, Arity}.
-module(public_key).
-export([
    cacerts_clear/0,
    cacerts_get/0,
    cacerts_load/0,
    cacerts_load/1,
    compute_key/2,
    compute_key/3,
    decrypt_private/2,
    decrypt_private/3,
    decrypt_public/2,
    decrypt_public/3,
    der_decode/2,
    der_encode/2,
    dh_gex_group/4,
    dh_gex_group_sizes/0,
    encrypt_private/2,
    encrypt_private/3,
    encrypt_public/2,
    encrypt_public/3,
    generate_key/1,
    handle_trace/3,
    ocsp_extensions/1,
    oid2ssh_curvename/1,
    pem_decode/1,
    pem_encode/1,
    pem_entry_decode/1,
    pem_entry_decode/2,
    pem_entry_encode/2,
    pem_entry_encode/3,
    pkix_crl_issuer/1,
    pkix_crl_verify/2,
    pkix_crls_validate/3,
    pkix_decode_cert/2,
    pkix_dist_point/1,
    pkix_dist_points/1,
    pkix_encode/3,
    pkix_hash_type/1,
    pkix_is_fixed_dh_cert/1,
    pkix_is_issuer/2,
    pkix_is_self_signed/1,
    pkix_issuer_id/2,
    pkix_match_dist_point/2,
    pkix_normalize_name/1,
    pkix_ocsp_validate/5,
    pkix_path_validation/3,
    pkix_sign/2,
    pkix_sign_types/1,
    pkix_subject_id/1,
    pkix_test_data/1,
    pkix_test_root_cert/2,
    pkix_verify/2,
    pkix_verify_hostname/2,
    pkix_verify_hostname/3,
    pkix_verify_hostname_match_fun/1,
    short_name_hash/1,
    sign/3,
    sign/4,
    ssh_curvename2oid/1,
    verify/4,
    verify/5
]).

-include("public_key.types.hrl").

%% The DER and PEM codec.
der_decode(Asn1Type, Der) -> clause_public_key_codec:der_decode(Asn1Type, Der).
der_encode(Asn1Type, Entity) -> clause_public_key_codec:der_encode(Asn1Type, Entity).
pem_decode(PemBin) -> clause_public_key_codec:pem_decode(PemBin).
pem_encode(PemEntries) -> clause_public_key_codec:pem_encode(PemEntries).
pem_entry_decode(PemEntry) -> clause_public_key_codec:pem_entry_decode(PemEntry).
pem_entry_decode(PemEntry, Password) -> clause_public_key_codec:pem_entry_decode(PemEntry, Password).
pem_entry_encode(Asn1Type, Entity) -> clause_public_key_codec:pem_entry_encode(Asn1Type, Entity).
pem_entry_encode(Asn1Type, Entity, InfoPwd) -> clause_public_key_codec:pem_entry_encode(Asn1Type, Entity, InfoPwd).
pkix_decode_cert(DerCert, Type) -> clause_public_key_codec:pkix_decode_cert(DerCert, Type).
pkix_encode(Asn1Type, Term, Type) -> clause_public_key_codec:pkix_encode(Asn1Type, Term, Type).

%% Keys and signatures.
compute_key(OthersKey, MyKey) -> clause_public_key_keys:compute_key(OthersKey, MyKey).
compute_key(OthersKey, MyKey, Params) -> clause_public_key_keys:compute_key(OthersKey, MyKey, Params).
decrypt_private(CipherText, Key) -> clause_public_key_keys:decrypt_private(CipherText, Key, []).
decrypt_private(CipherText, Key, Options) -> clause_public_key_keys:decrypt_private(CipherText, Key, Options).
decrypt_public(CipherText, Key) -> clause_public_key_keys:decrypt_public(CipherText, Key, []).
decrypt_public(CipherText, Key, Options) -> clause_public_key_keys:decrypt_public(CipherText, Key, Options).
dh_gex_group(Min, N, Max, Groups) -> clause_public_key_keys:dh_gex_group(Min, N, Max, Groups).
dh_gex_group_sizes() -> clause_public_key_keys:dh_gex_group_sizes().
encrypt_private(PlainText, Key) -> clause_public_key_keys:encrypt_private(PlainText, Key, []).
encrypt_private(PlainText, Key, Options) -> clause_public_key_keys:encrypt_private(PlainText, Key, Options).
encrypt_public(PlainText, Key) -> clause_public_key_keys:encrypt_public(PlainText, Key, []).
encrypt_public(PlainText, Key, Options) -> clause_public_key_keys:encrypt_public(PlainText, Key, Options).
generate_key(Params) -> clause_public_key_keys:generate_key(Params).
handle_trace(Kind, Event, Stack) -> clause_public_key_keys:handle_trace(Kind, Event, Stack).
oid2ssh_curvename(Oid) -> clause_public_key_keys:oid2ssh_curvename(Oid).
pkix_hash_type(Oid) -> clause_public_key_keys:pkix_hash_type(Oid).
pkix_sign(TBSCert, Key) -> clause_public_key_keys:pkix_sign(TBSCert, Key).
pkix_sign_types(Oid) -> clause_public_key_keys:pkix_sign_types(Oid).
pkix_verify(DerCert, Key) -> clause_public_key_keys:pkix_verify(DerCert, Key).
short_name_hash(Name) -> clause_public_key_keys:short_name_hash(Name).
sign(DigestOrPlainText, DigestType, Key) -> clause_public_key_keys:sign(DigestOrPlainText, DigestType, Key, []).
sign(DigestOrPlainText, DigestType, Key, Options) ->
    clause_public_key_keys:sign(DigestOrPlainText, DigestType, Key, Options).
ssh_curvename2oid(Name) -> clause_public_key_keys:ssh_curvename2oid(Name).
verify(DigestOrPlainText, DigestType, Signature, Key) ->
    clause_public_key_keys:verify(DigestOrPlainText, DigestType, Signature, Key, []).
verify(DigestOrPlainText, DigestType, Signature, Key, Options) ->
    clause_public_key_keys:verify(DigestOrPlainText, DigestType, Signature, Key, Options).

%%--------------------------------------------------------------------------------------------------------------------
%% Not implemented yet (plan 21 step LIB80D)
%%--------------------------------------------------------------------------------------------------------------------

cacerts_clear() -> erlang:error({not_implemented, cacerts_clear, 0}).
cacerts_get() -> erlang:error({not_implemented, cacerts_get, 0}).
cacerts_load() -> erlang:error({not_implemented, cacerts_load, 0}).
cacerts_load(_) -> erlang:error({not_implemented, cacerts_load, 1}).
ocsp_extensions(_) -> erlang:error({not_implemented, ocsp_extensions, 1}).
pkix_crl_issuer(_) -> erlang:error({not_implemented, pkix_crl_issuer, 1}).
pkix_crl_verify(_, _) -> erlang:error({not_implemented, pkix_crl_verify, 2}).
pkix_crls_validate(_, _, _) -> erlang:error({not_implemented, pkix_crls_validate, 3}).
pkix_dist_point(_) -> erlang:error({not_implemented, pkix_dist_point, 1}).
pkix_dist_points(_) -> erlang:error({not_implemented, pkix_dist_points, 1}).
pkix_is_fixed_dh_cert(_) -> erlang:error({not_implemented, pkix_is_fixed_dh_cert, 1}).
pkix_is_issuer(_, _) -> erlang:error({not_implemented, pkix_is_issuer, 2}).
pkix_is_self_signed(_) -> erlang:error({not_implemented, pkix_is_self_signed, 1}).
pkix_issuer_id(_, _) -> erlang:error({not_implemented, pkix_issuer_id, 2}).
pkix_match_dist_point(_, _) -> erlang:error({not_implemented, pkix_match_dist_point, 2}).
pkix_normalize_name(_) -> erlang:error({not_implemented, pkix_normalize_name, 1}).
pkix_ocsp_validate(_, _, _, _, _) -> erlang:error({not_implemented, pkix_ocsp_validate, 5}).
pkix_path_validation(_, _, _) -> erlang:error({not_implemented, pkix_path_validation, 3}).
pkix_subject_id(_) -> erlang:error({not_implemented, pkix_subject_id, 1}).
pkix_test_data(_) -> erlang:error({not_implemented, pkix_test_data, 1}).
pkix_test_root_cert(_, _) -> erlang:error({not_implemented, pkix_test_root_cert, 2}).
pkix_verify_hostname(_, _) -> erlang:error({not_implemented, pkix_verify_hostname, 2}).
pkix_verify_hostname(_, _, _) -> erlang:error({not_implemented, pkix_verify_hostname, 3}).
pkix_verify_hostname_match_fun(_) -> erlang:error({not_implemented, pkix_verify_hostname_match_fun, 1}).
