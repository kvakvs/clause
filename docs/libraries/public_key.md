# public_key

Clause's `public_key` application (plan 21, LIB80) consists of generated ASN.1 modules, the header
`library/public_key/include/public_key.hrl` and the `public_key` module. `clau` generates the ASN.1
modules from `library/public_key/asn1/` to encode and decode PKIX and PKCS types. The `public_key`
module exports the OTP 29 interface. Implemented functions cover DER and PEM encoding and decoding
(LIB80B), keys and signatures (LIB80C), certificate, name, CRL and hostname operations (LIB80D1),
path validation (LIB80D2), CRL validation (LIB80D3), OCSP validation (LIB80D4), test data
(LIB80D5) and trust stores (LIB80D6). See the [function inventory](../../library/public_key/public_key.terms).

## Specifications

`library/public_key/asn1/` contains the ASN.1 modules compiled by OTP's `public_key` application,
with matching module names:

| Source | Modules |
| --- | --- |
| RFC 5912 (PKIX, 2009 ASN.1) | `AlgorithmInformation-2009`, `AttributeCertificateVersion1-2009`, `EnrollmentMessageSyntax-2009`, `PKCS-10`, `PKIX-CommonTypes-2009`, `PKIX-X400Address-2009`, `PKIX1-PSS-OAEP-Algorithms-2009`, `PKIX1Explicit-2009`, `PKIX1Implicit-2009`, `PKIXAlgs-2009`, `PKIXAttributeCertificate-2009`, `PKIXCRMF-2009` |
| RFC 5911 (CMS and S/MIME) | `CryptographicMessageSyntax-2009`, `CryptographicMessageSyntaxAlgorithms-2009`, `CMSAesRsaesOaep-2009`, `SecureMimeMessageV3dot1-2009` |
| RFC 5915, 5958, 8017, 8018, 8410, 9629, 9654, 9810, 9814, 9881, 9935 | `ECPrivateKey`, `AsymmetricKeyPackageModuleV1`, `PKCS-1`, `PKCS5v2-0`, `Safecurves-pkix-18`, `KEMAlgorithmInformation-2023`, `OCSP-2024-08`, `PKIXCMP-2023`, `SLH-DSA-Module-2024`, `X509-ML-DSA-2025`, `X509-ML-KEM-2025` |
| Clause implementations | `OTP-PKIX` and `OTP-PKIX-Relaxed` (`OTPCertificate` representations), `DSS` (`DSAPrivateKey`), `PKCS-3` (`DHParameter`), `RFC5639` (Brainpool curve identifiers), `PKCS-FRAME.set.asn` (`AsymmetricKeyPackageModuleV1` with `PKCS5v2-0`) |

Each RFC module contains the RFC's code component and the IETF Trust BSD notice. Module text is
modified only to match OTP's specification differences. Modifications are documented at the start
of the affected files: `PKCS-1`, `PKCS5v2-0`, `ECPrivateKey`, `PKIX1Explicit-2009`, `PKIXAlgs-2009`
and `PKIXCMP-2023`. Generated Erlang modules and headers match those produced from OTP's
specifications.

## Generated modules

The build invokes `clau --generate-asn1` with OTP's `ber`, `der` and `noobj` options. Generated
modules, including `'OTP-PKIX'` and `'PKIX1Explicit-2009'`, are written to
`library-asn1/public_key/` in the build tree. This directory is under the second library root,
searched after `library/`. A program referencing a generated module causes it to be compiled into
the batch according to the [library discovery rules](library.md#how-programs-get-them).

As in OTP, `AsymmetricKeyPackageModuleV1` and `PKCS5v2-0` are merged into `PKCS-FRAME` and are
not available as separate modules. Encodings, decoded values and record definitions match
`asn1ct`; see [ASN.1](../asn1.md). OTP's `asn1config` exclusive decode functions, including
`decode_TBSCert_exclusive/1`, are not generated.

## Header

`-include_lib("public_key/include/public_key.hrl")` resolves to Clause's header unless the project
defines its own `public_key` application. The header defines OTP-compatible records and macros.
Most records and values are generated from the specifications. Additional definitions include
the pre-2009 `'Certificate'` representation, `'ECPoint'`, ML-DSA, ML-KEM and SLH-DSA key records,
`#cert{}` and the object identifiers used by `public_key`.

The `public_key_records` golden test compares every record field, default value and macro value
against OTP's header. The `public_key_asn1` golden test decodes and encodes certificates, keys and
parameters through the generated modules.

## Functions

The application includes [`public_key.app`](../../library/public_key/public_key.app) and an
`asn1.app` dependency resource. `public_key` delegates encoding and decoding functions to
`clause_public_key_codec`, which includes `public_key.hrl`. Runtime builtins implement the record
traversals and object identifier tables corresponding to OTP's `pubkey_translation` and
`pubkey_cert_records`; see [public key builtins](../builtins.md#public-key).

- `der_decode/2` and `der_encode/2` select the generated module for each type according to OTP's
  rules. PKCS #8 `PrivateKeyInfo` and `OneAsymmetricKey` values are unwrapped into RSA, DSA, EC,
  Ed25519/Ed448, ML-DSA, ML-KEM and SLH-DSA keys and wrapped during encoding. RSA keys include
  RSASSA-PSS parameters where applicable. `EcpkParameters`, `Dss-Sig-Value`, `AttributeTypeAndValue`,
  `ExtensionRequest` and extension types such as `'SubjectAltName'` and `'CRLReason'` use
  OTP-compatible representations. Errors include `{badmatch, {error, {asn1, Reason}}}` and
  `{badarg, {unknown_type, Type}}`, matching OTP.
- `pem_decode/1` and `pem_encode/1` use OTP's labels and 64-character line format. Decoding
  accepts `CRLF` and trailing whitespace and skips unrelated lines. An `ENCRYPTED PRIVATE KEY`
  entry decodes to `{'PrivateKeyInfo', Encrypted, CipherInfo}`.
- `pem_entry_decode/1,2` and `pem_entry_encode/2,3` support `SubjectPublicKeyInfo` entries for all
  key types. Password encryption uses `crypto` with OpenSSL's legacy `Proc-Type`/`DEK-Info`
  scheme and MD5 key derivation, PKCS #5 PBES1 with PBKDF1, or PBES2 with PBKDF2 and
  HMAC-SHA1/224/256/384/512. Supported ciphers are `DES-CBC`, `DES-EDE3-CBC`, `RC2-CBC` and
  `AES-128/192/256-CBC`, subject to availability in the linked OpenSSL.
- `pkix_decode_cert/2` supports `plain`, `otp` and `relaxed` representations. The `otp`
  representation decodes the public key and extensions, retains encoded CRL distribution points,
  and truncates user notices to 350 characters, matching OTP. `pkix_encode/3` supports `plain`
  and `otp` representations.

The `public_key_codec` golden test compares these functions against OTP, including the PEM text
of encrypted entries.

## Keys and signatures

`clause_public_key_keys` converts key records to OTP-compatible [`crypto`](crypto.md) key lists
and delegates cryptographic operations to that module:

| Key | `crypto` algorithm or dispatch |
| --- | --- |
| `#'RSAPrivateKey'{}` (all CRT components, or only `[E, N, D]`), `{RSAKey, PssParams}`; `#'RSAPublicKey'{}` | `rsa` |
| `#'DSAPrivateKey'{}`; `{Y, #'Dss-Parms'{}}` | `dss` |
| `#'ECPrivateKey'{}` with named or explicit parameters; `{#'ECPoint'{}, Params}` | `ecdsa` |
| Ed25519/Ed448 `#'ECPrivateKey'{}`, `{ed_pri, Curve, Pub, Priv}`; `{#'ECPoint'{}, {namedCurve, Oid}}`, `{ed_pub, Curve, Key}` | `eddsa` |
| `#'ML-DSAPrivateKey'{}` (seed, otherwise expanded key), `#'SLH-DSAPrivateKey'{}`; corresponding public keys | Selected parameter set |
| Map containing `sign_fun` or `encrypt_fun` | Invocation of the supplied function |

- `sign/3,4` and `verify/4,5` support the mapped key types. With a DSA key, digest type `none`
  denotes a SHA-1 digest. A non-binary signature causes verification to return `false`; an
  unsupported key representation raises `badarg`.
- `encrypt_private/2,3`, `decrypt_public/2,3`, `encrypt_public/2,3` and `decrypt_private/2,3`
  select RSA padding from `rsa_pad`, `rsa_padding` or the first atom option. The default is PKCS #1.
- `generate_key/1` supports DH, EC with named or explicit parameters, X25519/X448, Ed25519/Ed448,
  RSA, ML-DSA and SLH-DSA. EC parameters are normalized to the curve's OID. `compute_key/2,3`
  supports ECDH, X25519/X448 and DH.
- `pkix_sign/2` and `pkix_verify/2` support all signature algorithms recognized by OTP, including
  RSASSA-PSS. The signed byte sequence is the DER certificate's first element, corresponding to
  OTP's exclusive decode result. `pkix_sign_types/1` and `pkix_hash_type/1` provide algorithm
  and digest mappings.
- `short_name_hash/1` computes OpenSSL's `c_rehash` name hash.
- `dh_gex_group/4` and `dh_gex_group_sizes/0` implement OTP's group selection rules. Default
  groups are stored in `clause_public_key_moduli`: eight groups per OTP-supported size, derived
  from OpenSSH's moduli file. See [differences](../differences.md#asn1).
  `ssh_curvename2oid/1`, `oid2ssh_curvename/1` and `handle_trace/3` are also implemented.

The implementation preserves the following OTP behavior: `generate_key/1` produces an ML-DSA key
with an empty seed, which `crypto` rejects for signing; DSA private keys are rejected for
verification; and decoding explicit prime-field curves fails when decoding the prime.
The `public_key_keys` golden test compares deterministic signatures (RSA PKCS #1, EdDSA and
certificates), encryption, key agreement, types, hashes and group selection against OTP.

## Certificates and names

`clause_public_key_cert` implements certificate operations corresponding to OTP's `pubkey_cert`,
`pubkey_crl` and `public_key`:

- `pkix_normalize_name/1` trims printable strings, collapses internal whitespace and applies case
  folding. DER-encoded names are decoded first.
- `pkix_is_issuer/2` compares a certificate's or CRL's issuer with a candidate certificate's
  subject. Attribute values use term comparison, except that printable and UTF-8 strings are
  compared without case or redundant whitespace. `pkix_is_self_signed/1`, `pkix_subject_id/1`
  and `pkix_is_fixed_dh_cert/1` are also implemented.
- `pkix_issuer_id/2` with `self` returns the certificate's serial number and normalized issuer.
  With `other`, it uses the authority key identifier and returns `{error, issuer_not_found}`
  when that identifier is absent.
- `pkix_crl_issuer/1` extracts the CRL issuer. `pkix_crl_verify/2` verifies the signature over the
  CRL's first element using the certificate's key. `pkix_dist_point/1` and `pkix_dist_points/1`
  extract distribution points; the latter returns them in reverse order, matching OTP.
  `pkix_match_dist_point/2` compares the CRL's issuing distribution point names with the supplied
  point using OTP's name matching rules.
- `pkix_verify_hostname/2,3` supports `match_fun`, `fail_callback` and `fqdn_fun`. It matches DNS,
  URI, IP (tuples or strict strings) and SRV identifiers against subject alternative names, then
  evaluates hostnames derived from the reference identifiers. `pkix_verify_hostname_match_fun(https)`
  enables wildcard matching in the first label.

As in OTP, decoding an X9.42 DH certificate into the `otp` representation fails with
`{badarg, {unknown_type, 'DHPublicKey'}}`. Consequently, `pkix_is_fixed_dh_cert/1` operates only
on decoded records. The `public_key_certs` golden test compares these functions against OTP using
certificates and a CRL generated with OpenSSL.

## Path validation

`pkix_path_validation/3`, implemented by `clause_public_key_path`, validates a certificate chain
from a trusted certificate in DER, `otp` or `plain` representation. It follows OTP's implementation
of RFC 5280 section 6.1 and checks:

- Validity periods, interpreting UTCTime `notBefore` years 50–99 as 19xx and `notAfter` years
  through the sliding window.
- Issuer names and signatures using the working public key.
- Basic constraints, path length constraints, key usage and extended key usage.
- Name constraints on the subject, its e-mail addresses and its alternative names.
- The valid policy tree, policy mappings, policy constraints and `inhibitAnyPolicy`, with a
  cumulative limit of 1000 added policy nodes.

Supported OTP options are `verify_fun`, `max_path_length`, `explicit_policy`, `inhibit_any_policy`,
`inhibit_policy_mapping` and `policy_set`. `verify_fun` accepts arity 3 or 4 and receives
`{bad_cert, Reason}`, `{extension, Ext}`, `valid` and `valid_peer` events. A path error atom supplied
as the first argument is passed to the verification function before further processing.

The result is `{ok, {{Algorithm, PublicKey, Params}, PolicyNodes}}`, including the valid policies'
qualifiers, or `{error, {bad_cert, Reason}}`. The `public_key_path` golden test compares valid
chains, failure cases, policies, mappings, options and verification callback events against OTP
using chains generated with OpenSSL.

## CRL validation

`pkix_crls_validate/3`, implemented by `clause_public_key_crl`, checks a certificate against CRLs
according to OTP's implementation of RFC 5280 section 6.3:

- Each distribution point's CRLs are combined with applicable delta CRLs using CRL numbers,
  issuer, issuing distribution point and authority key identifier.
- A CRL past its next update time is replaced through `update_crl`, repeatedly while the callback
  supplies a new CRL.
- CRL issuer and scope are checked, including indirect CRLs.
- `issuer_fun` supplies the CRL signer's trusted certificate and path. Validation checks the
  cRLSign bit and, when the signer cannot sign certificates, its own revocation status.
- Reason masks are accumulated. The certificate is located first in the delta CRL, then in the
  base CRL.

The result is `valid`, `{bad_cert, {revoked, Reason}}` or
`{bad_cert, revocation_status_undetermined}`. When `undetermined_details` is enabled, the result
includes rejected CRLs and their rejection reasons. The `public_key_crls` golden test compares
revoked and unrevoked certificates, delta CRLs, stale CRLs, unknown critical extensions, invalid
signatures, missing issuer chains and a separate CRL signer against OTP using CRLs generated
with OpenSSL.

## OCSP validation

`pkix_ocsp_validate/5`, implemented by `clause_public_key_ocsp`, validates a DER-encoded OCSP
response according to OTP's implementation of RFC 6960:

- The response size must not exceed 100 KB. Its status must be `successful`, and its response
  type must be basic.
- The production time must be in the past.
- The signature must be valid for the responder identified by name or key hash. The responder
  certificate must be within its validity period and identify the issuer, a designated responder
  signed by the issuer with extended key usage `id-kp-OCSPSigning`, or a responder accepted by
  `is_trusted_responder_fun`.
- A supplied request nonce must be returned. A missing nonce produces `{missing, ocsp_nonce}` in
  the details.
- A single response must match the certificate's serial number and issuer name and key hashes.
  Its this-update time must be in the past; its next-update time, if present, must be in the future.

The result is `{ok, Details}`, `{error, {bad_cert, {revoked, RevokedInfo}}}` or
`{error, {bad_cert, {revocation_status_undetermined, Reason}}}`. `ocsp_extensions/1` constructs
the request nonce extension. The `public_key_ocsp` golden test compares good, revoked and unknown
statuses, designated, unauthorized and expired responders, nonce handling, modified and re-signed
responses, and stale times against OTP using responses generated with OpenSSL.

## Test data

`pkix_test_data/1` and `pkix_test_root_cert/2`, implemented by `clause_public_key_test_data`,
generate test certificates with OTP-compatible structure and defaults. A self-signed root can
be supplied as `#{cert, key}` or generated from options. Generated intermediate CAs and peers
form either one chain, returned as `[{cert, _}, {key, _}, {cacerts, _}]`, or server and client
chains, returned as `#{server_config, client_config}` with each chain trusting the other's root.

- **Keys:** the `key` option selects key generation parameters or supplies a key, including the
  `{both, Public, Private}` form. The default is generation on the first curve returned by
  `crypto:ec_curves/0`.
- **Signatures:** SHA-1 is the default unless overridden by `digest`. With `rsa_padding` set to
  `rsa_pss_rsae`, the default is SHA-256. Keys containing RSASSA-PSS parameters use RSASSA-PSS.
- **Identity:** subjects use OTP's values (`erlang`, `automated testing`, Stockholm), and serial
  numbers are obtained from `erlang:unique_integer/1`.
- **Validity:** the default period starts yesterday and ends one week ahead, at 13:00. The
  `validity` option overrides the dates. GeneralizedTime is used from 2049.
- **Extensions:** the `extensions` option replaces the defaults for the named extensions. Defaults
  include CA key usage and basic constraints, server peer key usage and a subject alternative
  name from `net_adm:localhost/0`, and client peer key usage.

The `public_key_test_data` golden test compares the non-random components of generated
certificates and their path validation results against OTP.

## Trust stores

`cacerts_load/0,1`, `cacerts_get/0` and `cacerts_clear/0`, implemented by
`clause_public_key_cacerts`, load trusted CA certificates as OTP does and cache them as
`#cert{}` records in a persistent term:

- **Linux, BSD and Solaris:** the first of OTP's bundle files (or `.pem` directories) per system
  that contains certificates, for example `/etc/ssl/certs/ca-certificates.crt` on Debian.
- **macOS:** the system root and system keychains, exported by `/usr/bin/security`.
- **Windows:** the current user's `ROOT` store, read through crypt32 by the `os_cacerts/0`
  builtin ([public key](../builtins.md#public-key)).
- **Files:** `cacerts_load/1` reads one PEM file or directory of `.pem` files; entries that are
  not certificates are ignored with a warning. A missing file, or one without certificates, gives
  `{error, enoent}`.

`cacerts_get/0` loads on first use, from the `public_key` application's `cacerts_path` setting if
present, and fails with `{failed_load_cacerts, Reason}` when nothing loads. `cacerts_clear/0`
returns whether certificates were loaded. The `public_key_cacerts` golden test compares bundles,
directories, `cacerts_path`, errors and whether the system store has certificates against OTP.
