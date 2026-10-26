# public_key

Clause's `public_key` application (plan 21, LIB80) is built from two parts: the ASN.1 modules that encode and decode
the PKIX and PKCS types, which clau generates from the specifications in `library/public_key/asn1/`, and the header
`library/public_key/include/public_key.hrl`, and the `public_key` module, which has OTP 29's exports: its DER and PEM
codec (LIB80B) and keys and signatures (LIB80C) are implemented; validation (LIB80D) still raises
`{not_implemented, Name, Arity}` ([inventory](../../library/public_key/public_key.terms)).

## Specifications

`library/public_key/asn1/` holds the modules OTP's `public_key` compiles, under the same names:

| Source | Modules |
| --- | --- |
| RFC 5912 (PKIX, 2009 ASN.1) | `AlgorithmInformation-2009`, `AttributeCertificateVersion1-2009`, `EnrollmentMessageSyntax-2009`, `PKCS-10`, `PKIX-CommonTypes-2009`, `PKIX-X400Address-2009`, `PKIX1-PSS-OAEP-Algorithms-2009`, `PKIX1Explicit-2009`, `PKIX1Implicit-2009`, `PKIXAlgs-2009`, `PKIXAttributeCertificate-2009`, `PKIXCRMF-2009` |
| RFC 5911 (CMS and S/MIME) | `CryptographicMessageSyntax-2009`, `CryptographicMessageSyntaxAlgorithms-2009`, `CMSAesRsaesOaep-2009`, `SecureMimeMessageV3dot1-2009` |
| RFC 5915, 5958, 8017, 8018, 8410, 9629, 9654, 9810, 9814, 9881, 9935 | `ECPrivateKey`, `AsymmetricKeyPackageModuleV1`, `PKCS-1`, `PKCS5v2-0`, `Safecurves-pkix-18`, `KEMAlgorithmInformation-2023`, `OCSP-2024-08`, `PKIXCMP-2023`, `SLH-DSA-Module-2024`, `X509-ML-DSA-2025`, `X509-ML-KEM-2025` |
| Written for Clause | `OTP-PKIX` and `OTP-PKIX-Relaxed` (the `OTPCertificate` forms), `DSS` (`DSAPrivateKey`), `PKCS-3` (`DHParameter`), `RFC5639` (Brainpool curve identifiers), `PKCS-FRAME.set.asn` (`AsymmetricKeyPackageModuleV1` with `PKCS5v2-0`) |

Each RFC module is the code component of its RFC with the IETF Trust's BSD notice, the module text unchanged except
where OTP's module differs; those edits are listed at the top of the file (`PKCS-1`, `PKCS5v2-0`, `ECPrivateKey`,
`PKIX1Explicit-2009`, `PKIXAlgs-2009`, `PKIXCMP-2023`). The modules generate the same Erlang modules and headers as
OTP's specifications do.

## Generated modules

The build runs `clau --generate-asn1` with OTP's options (`ber`, `der`, `noobj`) over the specifications and writes
the modules (`'OTP-PKIX'`, `'PKIX1Explicit-2009'`, ...) to `library-asn1/public_key/` in the build tree, a second
library root after `library/` ([library](library.md#how-programs-get-them)): a program that names one gets it compiled
into its batch like any library module. As in OTP, the modules `PKCS-FRAME` merges (`AsymmetricKeyPackageModuleV1`,
`PKCS5v2-0`) are not modules of their own. Their encodings, decodings and records are asn1ct's
([ASN.1](../asn1.md)); OTP's `asn1config` exclusive decode functions (`decode_TBSCert_exclusive/1`) are not
generated.

## Header

`-include_lib("public_key/include/public_key.hrl")` resolves to Clause's header unless a project names its own
`public_key` application. It defines the records and macros of OTP's: most are the records and values the
specifications generate, the others the records `public_key` builds itself (`'Certificate'` in its pre-2009 form,
`'ECPoint'`, the ML-DSA, ML-KEM and SLH-DSA keys, `#cert{}`) and the object identifiers it names. The
`public_key_records` golden checks every record's fields and defaults and every macro's value against OTP's header;
`public_key_asn1` decodes and encodes certificates, keys and parameters through the generated modules.

## Functions

`public_key` (with [`public_key.app`](../../library/public_key/public_key.app), and an `asn1.app` for the application
it depends on) forwards its codec functions to `clause_public_key_codec`, which includes `public_key.hrl`; the record
walks and object identifier tables of OTP's `pubkey_translation` and `pubkey_cert_records` are runtime builtins
([public key](../builtins.md#public-key)).

- `der_decode/2`, `der_encode/2`: the type's generated module, as OTP chooses it; PKCS #8 `PrivateKeyInfo` and
  `OneAsymmetricKey` unwrapped into RSA (also RSASSA-PSS with its parameters), DSA, EC, Ed25519/Ed448, ML-DSA,
  ML-KEM and SLH-DSA keys and wrapped again; `EcpkParameters`, `Dss-Sig-Value`, `AttributeTypeAndValue`,
  `ExtensionRequest` and extension types (`'SubjectAltName'`, `'CRLReason'`, ...) as OTP codes them; errors as OTP's
  (`{badmatch, {error, {asn1, Reason}}}`, `{badarg, {unknown_type, Type}}`).
- `pem_decode/1`, `pem_encode/1`: OTP's labels and lines (64 characters, `CRLF` and trailing white space accepted,
  other lines skipped); an `ENCRYPTED PRIVATE KEY` entry decodes to `{'PrivateKeyInfo', Encrypted, CipherInfo}`.
- `pem_entry_decode/1,2`, `pem_entry_encode/2,3`: `SubjectPublicKeyInfo` entries of every key type; password
  encryption by OpenSSL's legacy `Proc-Type`/`DEK-Info` scheme (MD5 key derivation), PKCS #5 PBES1 (PBKDF1) and PBES2
  (PBKDF2 with HMAC-SHA1/224/256/384/512) over `crypto`, with the ciphers `DES-CBC`, `DES-EDE3-CBC`, `RC2-CBC` and
  `AES-128/192/256-CBC` (those the linked OpenSSL provides).
- `pkix_decode_cert/2` in the `plain`, `otp` and `relaxed` forms (the otp form with its public key and extensions
  decoded, CRL distribution points left encoded, long user notices cut to 350 characters as OTP does),
  `pkix_encode/3` in the `plain` and `otp` forms.

The `public_key_codec` golden compares all of these with OTP, including the PEM text of encrypted entries.

## Keys and signatures

`clause_public_key_keys` turns key records into [`crypto`](crypto.md)'s key lists as OTP does and calls it:

| Key | crypto algorithm |
| --- | --- |
| `#'RSAPrivateKey'{}` (all CRT components, or only `[E, N, D]`), `{RSAKey, PssParams}`; `#'RSAPublicKey'{}` | `rsa` |
| `#'DSAPrivateKey'{}`; `{Y, #'Dss-Parms'{}}` | `dss` |
| `#'ECPrivateKey'{}` with named or explicit parameters; `{#'ECPoint'{}, Params}` | `ecdsa` |
| Ed25519/Ed448 `#'ECPrivateKey'{}`, `{ed_pri, Curve, Pub, Priv}`; `{#'ECPoint'{}, {namedCurve, Oid}}`, `{ed_pub, Curve, Key}` | `eddsa` |
| `#'ML-DSAPrivateKey'{}` (seed, else expanded key), `#'SLH-DSAPrivateKey'{}`; their public keys | the parameter set |
| A map with `sign_fun` or `encrypt_fun` | the fun is called |

- `sign/3,4`, `verify/4,5` (a DSA key with digest type `none` signs a SHA-1 digest; a signature that is not a binary
  verifies `false`; a key of no kind raises `badarg`), `encrypt_private/2,3`, `decrypt_public/2,3`,
  `encrypt_public/2,3`, `decrypt_private/2,3` (padding from `rsa_pad`, `rsa_padding` or the first atom option, PKCS #1
  by default).
- `generate_key/1`: DH, EC on named and explicit curves (parameters normalised to the curve's OID), X25519/X448,
  Ed25519/Ed448, RSA, ML-DSA and SLH-DSA; `compute_key/2,3`: ECDH, X25519/X448 and DH.
- `pkix_sign/2` and `pkix_verify/2` for every signature algorithm OTP knows, RSASSA-PSS included; the signed bytes are
  the DER certificate's first element, as OTP's exclusive decode gives them. `pkix_sign_types/1`, `pkix_hash_type/1`.
- `short_name_hash/1`: OpenSSL's `c_rehash` hash of a name.
- `dh_gex_group/4`, `dh_gex_group_sizes/0`: OTP's selection among groups; the default groups are eight per size from
  OpenSSH's moduli file (`clause_public_key_moduli`), the sizes OTP has
  ([differences](../differences.md#asn1)). `ssh_curvename2oid/1`, `oid2ssh_curvename/1`, `handle_trace/3`.

OTP's quirks are kept: an ML-DSA key `generate_key/1` makes has an empty seed, which `crypto` refuses to sign with;
a DSA private key cannot verify; explicit prime-field curves fail decoding their prime. The `public_key_keys`
golden compares deterministic signatures (RSA PKCS #1, EdDSA, certificates), encryption, agreement, types, hashes and
group selection with OTP.
