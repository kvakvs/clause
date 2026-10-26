# public_key

Clause's `public_key` application (plan 21, LIB80) is built from two parts: the ASN.1 modules that
encode and decode the PKIX and PKCS types, which clau generates from the specifications in
`library/public_key/asn1/`, and the header `library/public_key/include/public_key.hrl`, and the
`public_key` module, which has OTP 29's exports: its DER and PEM codec (LIB80B), keys and signatures
(LIB80C), the certificate, name, CRL and hostname functions (LIB80D1), path validation (LIB80D2),
CRL validation (LIB80D3) and OCSP validation (LIB80D4) are implemented; test data and trust stores
(LIB80D5–D6) still raise `{not_implemented, Name, Arity}`
([inventory](../../library/public_key/public_key.terms)).

## Specifications

`library/public_key/asn1/` holds the modules OTP's `public_key` compiles, under the same names:

| Source | Modules |
| --- | --- |
| RFC 5912 (PKIX, 2009 ASN.1) | `AlgorithmInformation-2009`, `AttributeCertificateVersion1-2009`, `EnrollmentMessageSyntax-2009`, `PKCS-10`, `PKIX-CommonTypes-2009`, `PKIX-X400Address-2009`, `PKIX1-PSS-OAEP-Algorithms-2009`, `PKIX1Explicit-2009`, `PKIX1Implicit-2009`, `PKIXAlgs-2009`, `PKIXAttributeCertificate-2009`, `PKIXCRMF-2009` |
| RFC 5911 (CMS and S/MIME) | `CryptographicMessageSyntax-2009`, `CryptographicMessageSyntaxAlgorithms-2009`, `CMSAesRsaesOaep-2009`, `SecureMimeMessageV3dot1-2009` |
| RFC 5915, 5958, 8017, 8018, 8410, 9629, 9654, 9810, 9814, 9881, 9935 | `ECPrivateKey`, `AsymmetricKeyPackageModuleV1`, `PKCS-1`, `PKCS5v2-0`, `Safecurves-pkix-18`, `KEMAlgorithmInformation-2023`, `OCSP-2024-08`, `PKIXCMP-2023`, `SLH-DSA-Module-2024`, `X509-ML-DSA-2025`, `X509-ML-KEM-2025` |
| Written for Clause | `OTP-PKIX` and `OTP-PKIX-Relaxed` (the `OTPCertificate` forms), `DSS` (`DSAPrivateKey`), `PKCS-3` (`DHParameter`), `RFC5639` (Brainpool curve identifiers), `PKCS-FRAME.set.asn` (`AsymmetricKeyPackageModuleV1` with `PKCS5v2-0`) |

Each RFC module is the code component of its RFC with the IETF Trust's BSD notice, the module text
unchanged except where OTP's module differs; those edits are listed at the top of the file
(`PKCS-1`, `PKCS5v2-0`, `ECPrivateKey`, `PKIX1Explicit-2009`, `PKIXAlgs-2009`, `PKIXCMP-2023`). The
modules generate the same Erlang modules and headers as OTP's specifications do.

## Generated modules

The build runs `clau --generate-asn1` with OTP's options (`ber`, `der`, `noobj`) over the
specifications and writes the modules (`'OTP-PKIX'`, `'PKIX1Explicit-2009'`, ...) to
`library-asn1/public_key/` in the build tree, a second library root after `library/`
([library](library.md#how-programs-get-them)): a program that names one gets it compiled into its
batch like any library module. As in OTP, the modules `PKCS-FRAME` merges
(`AsymmetricKeyPackageModuleV1`, `PKCS5v2-0`) are not modules of their own. Their encodings,
decodings and records are asn1ct's ([ASN.1](../asn1.md)); OTP's `asn1config` exclusive decode
functions (`decode_TBSCert_exclusive/1`) are not generated.

## Header

`-include_lib("public_key/include/public_key.hrl")` resolves to Clause's header unless a project
names its own `public_key` application. It defines the records and macros of OTP's: most are the
records and values the specifications generate, the others the records `public_key` builds itself
(`'Certificate'` in its pre-2009 form, `'ECPoint'`, the ML-DSA, ML-KEM and SLH-DSA keys, `#cert{}`)
and the object identifiers it names. The `public_key_records` golden checks every record's fields
and defaults and every macro's value against OTP's header; `public_key_asn1` decodes and encodes
certificates, keys and parameters through the generated modules.

## Functions

`public_key` (with [`public_key.app`](../../library/public_key/public_key.app), and an `asn1.app`
for the application it depends on) forwards its codec functions to `clause_public_key_codec`, which
includes `public_key.hrl`; the record walks and object identifier tables of OTP's
`pubkey_translation` and `pubkey_cert_records` are runtime builtins
([public key](../builtins.md#public-key)).

- `der_decode/2`, `der_encode/2`: the type's generated module, as OTP chooses it; PKCS #8
  `PrivateKeyInfo` and `OneAsymmetricKey` unwrapped into RSA (also RSASSA-PSS with its parameters),
  DSA, EC, Ed25519/Ed448, ML-DSA, ML-KEM and SLH-DSA keys and wrapped again; `EcpkParameters`,
  `Dss-Sig-Value`, `AttributeTypeAndValue`, `ExtensionRequest` and extension types
  (`'SubjectAltName'`, `'CRLReason'`, ...) as OTP codes them; errors as OTP's
  (`{badmatch, {error, {asn1, Reason}}}`, `{badarg, {unknown_type, Type}}`).
- `pem_decode/1`, `pem_encode/1`: OTP's labels and lines (64 characters, `CRLF` and trailing white
  space accepted, other lines skipped); an `ENCRYPTED PRIVATE KEY` entry decodes to
  `{'PrivateKeyInfo', Encrypted, CipherInfo}`.
- `pem_entry_decode/1,2`, `pem_entry_encode/2,3`: `SubjectPublicKeyInfo` entries of every key type;
  password encryption by OpenSSL's legacy `Proc-Type`/`DEK-Info` scheme (MD5 key derivation), PKCS
  #5 PBES1 (PBKDF1) and PBES2 (PBKDF2 with HMAC-SHA1/224/256/384/512) over `crypto`, with the
  ciphers `DES-CBC`, `DES-EDE3-CBC`, `RC2-CBC` and `AES-128/192/256-CBC` (those the linked OpenSSL
  provides).
- `pkix_decode_cert/2` in the `plain`, `otp` and `relaxed` forms (the otp form with its public key
  and extensions decoded, CRL distribution points left encoded, long user notices cut to 350
  characters as OTP does), `pkix_encode/3` in the `plain` and `otp` forms.

The `public_key_codec` golden compares all of these with OTP, including the PEM text of encrypted
entries.

## Keys and signatures

`clause_public_key_keys` turns key records into [`crypto`](crypto.md)'s key lists as OTP does and
calls it:

| Key | crypto algorithm |
| --- | --- |
| `#'RSAPrivateKey'{}` (all CRT components, or only `[E, N, D]`), `{RSAKey, PssParams}`; `#'RSAPublicKey'{}` | `rsa` |
| `#'DSAPrivateKey'{}`; `{Y, #'Dss-Parms'{}}` | `dss` |
| `#'ECPrivateKey'{}` with named or explicit parameters; `{#'ECPoint'{}, Params}` | `ecdsa` |
| Ed25519/Ed448 `#'ECPrivateKey'{}`, `{ed_pri, Curve, Pub, Priv}`; `{#'ECPoint'{}, {namedCurve, Oid}}`, `{ed_pub, Curve, Key}` | `eddsa` |
| `#'ML-DSAPrivateKey'{}` (seed, else expanded key), `#'SLH-DSAPrivateKey'{}`; their public keys | the parameter set |
| A map with `sign_fun` or `encrypt_fun` | the fun is called |

- `sign/3,4`, `verify/4,5` (a DSA key with digest type `none` signs a SHA-1 digest; a signature that
  is not a binary verifies `false`; a key of no kind raises `badarg`), `encrypt_private/2,3`,
  `decrypt_public/2,3`, `encrypt_public/2,3`, `decrypt_private/2,3` (padding from `rsa_pad`,
  `rsa_padding` or the first atom option, PKCS #1 by default).
- `generate_key/1`: DH, EC on named and explicit curves (parameters normalised to the curve's OID),
  X25519/X448, Ed25519/Ed448, RSA, ML-DSA and SLH-DSA; `compute_key/2,3`: ECDH, X25519/X448 and DH.
- `pkix_sign/2` and `pkix_verify/2` for every signature algorithm OTP knows, RSASSA-PSS included;
  the signed bytes are the DER certificate's first element, as OTP's exclusive decode gives them.
  `pkix_sign_types/1`, `pkix_hash_type/1`.
- `short_name_hash/1`: OpenSSL's `c_rehash` hash of a name.
- `dh_gex_group/4`, `dh_gex_group_sizes/0`: OTP's selection among groups; the default groups are
  eight per size from OpenSSH's moduli file (`clause_public_key_moduli`), the sizes OTP has
  ([differences](../differences.md#asn1)). `ssh_curvename2oid/1`, `oid2ssh_curvename/1`,
  `handle_trace/3`.

OTP's quirks are kept: an ML-DSA key `generate_key/1` makes has an empty seed, which `crypto`
refuses to sign with; a DSA private key cannot verify; explicit prime-field curves fail decoding
their prime. The `public_key_keys` golden compares deterministic signatures (RSA PKCS #1, EdDSA,
certificates), encryption, agreement, types, hashes and group selection with OTP.

## Certificates and names

`clause_public_key_cert` holds what OTP's `pubkey_cert`, `pubkey_crl` and `public_key` do for
certificates:

- `pkix_normalize_name/1` (printable strings trimmed, inner white space runs made single, case
  folded; a DER name is decoded first), `pkix_is_issuer/2` (a certificate's or CRL's issuer against
  a candidate's subject, attribute values compared as terms or, for printable and UTF-8 strings,
  without case and extra white space), `pkix_is_self_signed/1`, `pkix_issuer_id/2` (`self`: the
  certificate's serial number and normalised issuer; `other`: its authority key identifier's,
  `{error, issuer_not_found}` without one), `pkix_subject_id/1`, `pkix_is_fixed_dh_cert/1`.
- `pkix_crl_issuer/1`, `pkix_crl_verify/2` (the CRL's signature over its first element, with the
  certificate's key), `pkix_dist_point/1`, `pkix_dist_points/1` (in reverse order, as OTP),
  `pkix_match_dist_point/2` (the CRL's issuing distribution point names against the point's, by
  OTP's name matching rules).
- `pkix_verify_hostname/2,3` with the `match_fun`, `fail_callback` and `fqdn_fun` options: DNS, URI,
  IP (tuples or strict strings) and SRV ids against the subject alternative names, then the host
  names taken from the reference ids; `pkix_verify_hostname_match_fun(https)` adds wildcard matching
  of the first label.

As in OTP, a certificate of an X9.42 DH key does not decode in the otp form
(`{badarg, {unknown_type, 'DHPublicKey'}}`), so `pkix_is_fixed_dh_cert/1` answers only for decoded
records. The `public_key_certs` golden compares these functions with OTP on certificates and a CRL
made with OpenSSL.

## Path validation

`pkix_path_validation/3` (`clause_public_key_path`) validates a chain from a trusted certificate
(DER, otp or plain form) as OTP does, by RFC 5280 6.1: validity dates (UTC notBefore years 50–99 are
19xx, notAfter by the sliding window), issuer names, the signature with the working key, basic
constraints and path lengths, key usage and extended key usage, name constraints over the subject,
its e-mail addresses and its alternative names, and the valid policy tree with policy mappings,
constraints and `inhibitAnyPolicy` (at most 1000 nodes ever added). The options are OTP's:
`verify_fun` (arity 3 or 4; `{bad_cert, Reason}`, `{extension, Ext}`, `valid` and `valid_peer`
events), `max_path_length`, `explicit_policy`, `inhibit_any_policy`, `inhibit_policy_mapping` and
`policy_set`. The result is `{ok, {{Algorithm, PublicKey, Params}, PolicyNodes}}` with the valid
policies' qualifiers, or `{error, {bad_cert, Reason}}`; a path error atom as the first argument is
given to the verify fun first. The `public_key_path` golden compares valid chains, each failure,
policies, mappings, options and verify fun events with OTP on chains made with OpenSSL.

## CRL validation

`pkix_crls_validate/3` (`clause_public_key_crl`) checks a certificate against CRLs as OTP does, by
RFC 5280 6.3: the CRLs of each distribution point are combined with the delta CRL that extends them
(CRL numbers, issuer, issuing distribution point and authority key identifier), a CRL past its next
update is replaced through `update_crl` while it gives a new one, the CRL's issuer and scope are
checked (indirect CRLs included), `issuer_fun` gives the CRL signer's trusted certificate and path,
which is validated with the cRLSign bit (and the signer's own revocation status when it cannot sign
certificates), the reason masks accumulate, and the certificate is looked up in the delta CRL and
then the CRL. The result is `valid`, `{bad_cert, {revoked, Reason}}` or
`{bad_cert, revocation_status_undetermined}`, with the rejected CRLs and their reasons when
`undetermined_details` is set. The `public_key_crls` golden compares revoked and unrevoked
certificates, delta CRLs, stale CRLs, unknown critical extensions, bad signatures, missing issuer
chains and a separate CRL signer with OTP on CRLs made with OpenSSL.

## OCSP validation

`pkix_ocsp_validate/5` (`clause_public_key_ocsp`) checks a DER OCSP response for a certificate as
OTP does, by RFC 6960: the response must be at most 100 KB, `successful` and of the basic type; it
must have been produced in the past and be signed by a responder that its responder ID names (by
name or by key hash), whose certificate is within its validity dates and is the issuer itself, a
designated responder the issuer signed with the `id-kp-OCSPSigning` extended key usage, or one that
`is_trusted_responder_fun` accepts; the nonce sent must come back (a missing one gives
`{missing, ocsp_nonce}` in the details); and a single response must match the certificate's serial
number and issuer name and key hashes, with this update in the past and next update, if any, in the
future. The result is `{ok, Details}`, `{error, {bad_cert, {revoked, RevokedInfo}}}` or
`{error, {bad_cert, {revocation_status_undetermined, Reason}}}`. `ocsp_extensions/1` gives the
nonce extension of a request. The `public_key_ocsp` golden compares good, revoked and unknown
statuses, designated, unauthorised and expired responders, nonces, tampered and re-signed responses
and stale times with OTP on responses made with OpenSSL.
