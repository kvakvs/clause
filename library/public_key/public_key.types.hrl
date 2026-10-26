%% OTP 29 public_key types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([asn1_type/0,
              bad_cert_reason/0,
              cert/0,
              cert_id/0,
              cert_opt/0,
              chain_opts/0,
              combined_cert/0,
              conf_opt/0,
              custom_key_opts/0,
              der_encoded/0,
              digest_type/0,
              dsa_private_key/0,
              dsa_public_key/0,
              ecdsa_private_key/0,
              ecdsa_public_key/0,
              eddsa_private_key/0,
              eddsa_public_key/0,
              issuer_name/0,
              key_params/0,
              mldsa_private_key/0,
              mldsa_public_key/0,
              oid/0,
              pem_entry/0,
              pki_asn1_type/0,
              policy_node/0,
              private_key/0,
              public_key/0,
              public_key_info/0,
              rsa_private_key/0,
              rsa_pss_private_key/0,
              rsa_pss_public_key/0,
              rsa_public_key/0,
              slh_dsa_private_key/0,
              slh_dsa_public_key/0,
              test_config/0,
              test_root_cert/0]).
-record('DSAPrivateKey',{version, p, q, g, y, x}).
-record('ECPrivateKey',{version, privateKey,
                        parameters = asn1_NOVALUE,
                        publicKey = asn1_NOVALUE,
                        attributes = asn1_NOVALUE}).
-record('OTPCertificate',{tbsCertificate, signatureAlgorithm, signature}).
-record('RSAPrivateKey',{version, modulus, publicExponent,
                         privateExponent, prime1, prime2, exponent1,
                         exponent2, coefficient,
                         otherPrimeInfos = asn1_NOVALUE}).
-record('RSASSA-PSS-params',{hashAlgorithm = asn1_DEFAULT,
                             maskGenAlgorithm = asn1_DEFAULT,
                             saltLength = asn1_DEFAULT,
                             trailerField = asn1_DEFAULT}).
-record('Extension',{extnID, critical = asn1_DEFAULT, extnValue}).
-record('UserNotice',{noticeRef = asn1_NOVALUE,
                      explicitText = asn1_NOVALUE}).
-record('RSAPublicKey',{modulus, publicExponent}).
-record('ECPoint',{point}).
-record(cert,{der :: public_key:der_encoded(),
              otp :: #'OTPCertificate'{}}).
-record('ML-DSAPrivateKey',{algorithm :: crypto:mldsa(),
                            seed = <<>> :: binary(),
                            expandedkey = <<>> :: binary()}).
-record('ML-DSAPublicKey',{algorithm :: crypto:mldsa(), key :: binary()}).
-record('ML-KEMPrivateKey',{algorithm :: crypto:kem(),
                            seed = <<>> :: binary(),
                            expandedkey = <<>> :: binary()}).
-record('ML-KEMPublicKey',{algorithm :: crypto:kem(), key :: binary()}).
-record('SLH-DSAPrivateKey',{algorithm :: crypto:slh_dsa(),
                             key :: binary()}).
-record('SLH-DSAPublicKey',{algorithm :: crypto:slh_dsa(),
                            key :: binary()}).
-record('ECParameters',{version, fieldID, curve, base, order,
                        cofactor = asn1_NOVALUE}).
-record('Dss-Parms',{p, q, g}).
-record('AttributeTypeAndValue',{type, value}).
-record('PBEParameter',{salt, iterationCount}).
-record('PBES2-params',{keyDerivationFunc, encryptionScheme}).
-type public_key() ::
          rsa_public_key() |
          rsa_pss_public_key() |
          dsa_public_key() |
          ecdsa_public_key() |
          eddsa_public_key() |
          mldsa_public_key() |
          ml_kem_public_key() |
          slh_dsa_public_key().
-type private_key() ::
          rsa_private_key() |
          rsa_pss_private_key() |
          dsa_private_key() |
          ecdsa_private_key() |
          eddsa_private_key() |
          mldsa_private_key() |
          ml_kem_private_key() |
          slh_dsa_private_key() |
          #{algorithm :=
                slh_dsa | mldsa | eddsa | rsa_pss_pss | ecdsa | rsa |
                dsa,
            sign_fun => fun()}.
-type custom_key_opts() :: [term()].
-type rsa_public_key() :: #'RSAPublicKey'{}.
-type rsa_private_key() ::
          #'RSAPrivateKey'{} | #{algorithm := rsa, encrypt_fun => fun()}.
-type rsa_pss_public_key() :: {rsa_public_key(), #'RSASSA-PSS-params'{}}.
-type rsa_pss_private_key() ::
          {#'RSAPrivateKey'{}, #'RSASSA-PSS-params'{}}.
-type dsa_private_key() :: #'DSAPrivateKey'{}.
-type dsa_public_key() :: {dss_public_key(), #'Dss-Parms'{}}.
-type dss_public_key() :: pos_integer().
-type ecdsa_public_key() ::
          {#'ECPoint'{}, {namedCurve, oid()} | #'ECParameters'{}}.
-type mldsa_public_key() :: #'ML-DSAPublicKey'{}.
-type ml_kem_public_key() :: #'ML-KEMPublicKey'{}.
-type slh_dsa_public_key() :: #'SLH-DSAPublicKey'{}.
-type ecdsa_private_key() :: #'ECPrivateKey'{}.
-type eddsa_public_key() :: {#'ECPoint'{}, {namedCurve, oid()}}.
-type eddsa_private_key() ::
          #'ECPrivateKey'{parameters :: {namedCurve, oid()}}.
-type mldsa_private_key() :: #'ML-DSAPrivateKey'{}.
-type ml_kem_private_key() :: #'ML-KEMPrivateKey'{}.
-type slh_dsa_private_key() :: #'SLH-DSAPrivateKey'{}.
-type key_params() ::
          'NULL' |
          #'RSASSA-PSS-params'{} |
          {namedCurve, oid()} |
          #'ECParameters'{} |
          #'Dss-Parms'{}.
-type der_encoded() :: binary().
-type pki_asn1_type() ::
          'Certificate' | 'RSAPrivateKey' | 'RSAPublicKey' |
          'SubjectPublicKeyInfo' | 'DSAPrivateKey' | 'DHParameter' |
          'PrivateKeyInfo' | 'CertificationRequest' | 'ContentInfo' |
          'CertificateList' | 'ECPrivateKey' | 'OneAsymmetricKey' |
          'EcpkParameters'.
-type pem_entry() ::
          {pki_asn1_type(),
           DerOrDerEncrypted :: binary(),
           not_encrypted |
           {Cipher :: iodata(),
            Salt ::
                binary() |
                {#'PBEParameter'{}, digest_type()} |
                #'PBES2-params'{}}}.
-type asn1_type() :: atom().
-type digest_type() ::
          crypto:sha2() | crypto:sha1() | legacy_digest_type() | none.
-type legacy_digest_type() :: md5 | md2.
-type crl_reason() ::
          unspecified | keyCompromise | cACompromise |
          affiliationChanged | superseded | cessationOfOperation |
          certificateHold | privilegeWithdrawn | aACompromise.
-type oid() :: tuple().
-type cert_id() :: {SerialNr :: integer(), issuer_name()}.
-type issuer_name() :: {rdnSequence, [[#'AttributeTypeAndValue'{}]]}.
-type bad_cert_reason() ::
          cert_expired | invalid_issuer | invalid_signature |
          distinguished_name_not_permitted | name_not_permitted |
          missing_basic_constraint | invalid_key_usage |
          duplicate_cert_in_path |
          {key_usage_mismatch, term()} |
          {policy_requirement_not_met, term()} |
          {invalid_policy_mapping, term()} |
          {revoked, crl_reason()} |
          invalid_validity_dates | policy_tree_exceeded |
          {revocation_status_undetermined, term()} |
          atom().
-type combined_cert() :: #cert{}.
-type cert() :: der_encoded() | #'OTPCertificate'{}.
-type policy_node() ::
          #{valid_policy := oid(),
            qualifier_set := [#'UserNotice'{} | {uri, string()}],
            expected_policy_set := [oid()]}.
-type public_key_info() ::
          {oid(),
           rsa_public_key() | #'ECPoint'{} | dss_public_key(),
           key_params()}.
-type cert_opt() ::
          {digest, digest_type()} |
          {key,
           {namedCurve, oid()} |
           #'ECParameters'{} |
           {rsa, Size :: pos_integer(), Prime :: pos_integer()} |
           private_key()} |
          {validity,
           {From :: erlang:timestamp(), To :: erlang:timestamp()}} |
          {extensions, [#'Extension'{}]}.
-type chain_opts() ::
          #{root := [cert_opt()],
            intermediates => [[cert_opt()]],
            peer := [cert_opt()]}.
-type conf_opt() ::
          {cert, der_encoded()} |
          {key, private_key() | {both, public_key(), private_key()}} |
          {cacerts, [der_encoded()]}.
-type test_config() ::
          #{server_config := [conf_opt()],
            client_config := [conf_opt()]}.
-type test_root_cert() :: #{cert := der_encoded(), key := private_key()}.
