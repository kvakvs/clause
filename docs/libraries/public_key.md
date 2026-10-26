# public_key

Clause's `public_key` application (plan 21, LIB80) is built from two parts: the ASN.1 modules that encode and decode
the PKIX and PKCS types, which clau generates from the specifications in `library/public_key/asn1/`, and the header
`library/public_key/include/public_key.hrl`. The functions of the `public_key` module follow in later steps (LIB80B:
DER and PEM codec; LIB80C: keys and signatures; LIB80D: validation).

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
