%% The records and macros of public_key.hrl: each record's fields and default values, each macro's value
%% (plan 21 step LIB80B).
-module(public_key_records).
-export([main/1]).
-include_lib("public_key/include/public_key.hrl").

main(_) ->
    [io:format("~p ~w~n", [R, record(R)]) || R <- records()],
    [io:format("~p ~w~n", [M, macro(M)]) || M <- macros()],
    io:format("public_key_records: ok~n").

records() ->
    [
        'RSAPublicKey',
        'RSAPrivateKey',
        'OtherPrimeInfo',
        'RSASSA-PSS-params',
        'RSASSA-AlgorithmIdentifier',
        'RSAES-OAEP-params',
        'RSAES-AlgorithmIdentifier',
        'HashAlgorithm',
        'MaskGenAlgorithm',
        'PSourceAlgorithm',
        'ML-DSAPrivateKey',
        'ML-DSAPublicKey',
        'ML-KEMPrivateKey',
        'ML-KEMPublicKey',
        'SLH-DSAPrivateKey',
        'SLH-DSAPublicKey',
        'DSAPrivateKey',
        'Dss-Parms',
        'DomainParameters',
        'ValidationParams',
        'Dss-Sig-Value',
        'ECPrivateKey',
        'ECParameters',
        'Curve',
        'FieldID',
        'ECPoint',
        'ECDSA-Sig-Value',
        'Certificate',
        'TBSCertificate',
        'TBSCertificate_signature',
        'Validity',
        'SubjectPublicKeyInfo',
        'AlgorithmIdentifier',
        'Certificate_algorithmIdentifier',
        'AttributeTypeAndValue',
        'Extension',
        'AttributeSet',
        'SingleAttribute',
        'SecurityCategory',
        'AuthorityKeyIdentifier',
        'PrivateKeyUsagePeriod',
        'PolicyInformation',
        'PolicyQualifierInfo',
        'UserNotice',
        'NoticeReference',
        'PolicyMappings_SEQOF',
        'EDIPartyName',
        'SubjectDirectoryAttributes_SEQOF',
        'BasicConstraints',
        'NameConstraints',
        'GeneralSubtree',
        'PolicyConstraints',
        'DistributionPoint',
        'AccessDescription',
        'OTPCertificate',
        'OTPTBSCertificate',
        'OTPSubjectPublicKeyInfo',
        'SignatureAlgorithm',
        'PublicKeyAlgorithm',
        cert,
        'DigestAlgorithm',
        'DHParameter',
        'CertificateList',
        'TBSCertList',
        'TBSCertList_signature',
        'CertificateList_algorithmIdentifier',
        'TBSCertList_revokedCertificates_SEQOF',
        'IssuingDistributionPoint',
        'CertificationRequest',
        'CertificationRequestInfo',
        'CertificationRequestInfo_subjectPKInfo',
        'CertificationRequestInfo_subjectPKInfo_algorithm',
        'CertificationRequestInfo_attributes_SETOF',
        'CertificationRequest_signatureAlgorithm',
        'OCSPRequest',
        'TBSRequest',
        'TBSRequest_requestExtensions_SEQOF',
        'Signature',
        'Signature_signatureAlgorithm',
        'Request',
        'Request_singleRequestExtensions_SEQOF',
        'CertID',
        'CertID_hashAlgorithm',
        'OCSPResponse',
        'ResponseBytes',
        'BasicOCSPResponse',
        'ResponseData',
        'BasicOCSPResponse_signatureAlgorithm',
        'ResponseData_responseExtensions_SEQOF',
        'SingleResponse',
        'SingleResponse_singleExtensions_SEQOF',
        'RevokedInfo',
        'ServiceLocator',
        'CrlID',
        'PreferredSignatureAlgorithm',
        'PreferredSignatureAlgorithm_sigIdentifier',
        'PreferredSignatureAlgorithm_certIdentifier',
        'PrivateKeyInfo',
        'PrivateKeyInfo_privateKeyAlgorithm',
        'EncryptedPrivateKeyInfo',
        'EncryptedPrivateKeyInfo_encryptionAlgorithm',
        'OneAsymmetricKey',
        'RC2-CBC-Parameter',
        'PBES2-params',
        'PBES2-params_keyDerivationFunc',
        'PBES2-params_encryptionScheme',
        'PBEParameter',
        'PBKDF2-params',
        'PBKDF2-params_prf',
        'EncryptionAlgorithmIdentifier',
        'Attribute',
        'ContentInfo',
        'SignedData',
        'EncapsulatedContentInfo',
        'SignerInfo',
        'SignerInfo_unsignedAttrs_SETOF',
        'SignedAttributes_SETOF',
        'EnvelopedData',
        'EnvelopedData_unprotectedAttrs_SETOF',
        'OriginatorInfo',
        'EncryptedContentInfo',
        'KeyTransRecipientInfo',
        'KeyTransRecipientInfo_keyEncryptionAlgorithm',
        'KeyAgreeRecipientInfo',
        'KeyAgreeRecipientInfo_keyEncryptionAlgorithm',
        'OriginatorPublicKey',
        'OriginatorPublicKey_algorithm',
        'RecipientEncryptedKey',
        'RecipientKeyIdentifier',
        'KEKRecipientInfo',
        'KEKIdentifier',
        'PasswordRecipientInfo',
        'OtherRecipientInfo',
        'DigestedData',
        'EncryptedData',
        'EncryptedData_unprotectedAttrs_SETOF',
        'AuthenticatedData',
        'AuthAttributes_SETOF',
        'UnauthAttributes_SETOF',
        'DigestAlgorithmIdentifier',
        'SignatureAlgorithmIdentifier',
        'KeyEncryptionAlgorithmIdentifier',
        'ContentEncryptionAlgorithmIdentifier',
        'MessageAuthenticationCodeAlgorithm',
        'KeyDerivationAlgorithmIdentifier',
        'OtherRevocationInfoFormat',
        'OtherCertificateFormat',
        'IssuerAndSerialNumber',
        'OtherKeyAttribute',
        'ExtendedCertificate',
        'ExtendedCertificateInfo',
        'ORAddress',
        'BuiltInStandardAttributes',
        'PersonalName',
        'BuiltInDomainDefinedAttribute',
        'ExtensionAttribute',
        'PDSParameter',
        'PresentationAddress',
        'TeletexDomainDefinedAttribute'
    ].

macros() ->
    [
        'pkcs-1',
        'id-RSAES-OAEP',
        'id-pSpecified',
        'pSpecifiedEmpty',
        'emptyString',
        'nullOctetString',
        'nullParameters',
        'id-ml-dsa-44',
        'id-ml-dsa-65',
        'id-ml-dsa-87',
        'id-alg-ml-kem-512',
        'id-alg-ml-kem-768',
        'id-alg-ml-kem-1024',
        'id-slh-dsa-sha2-128s',
        'id-slh-dsa-sha2-128f',
        'id-slh-dsa-sha2-192s',
        'id-slh-dsa-sha2-192f',
        'id-slh-dsa-sha2-256s',
        'id-slh-dsa-sha2-256f',
        'id-slh-dsa-shake-128s',
        'id-slh-dsa-shake-128f',
        'id-slh-dsa-shake-192s',
        'id-slh-dsa-shake-192f',
        'id-slh-dsa-shake-256s',
        'id-slh-dsa-shake-256f',
        'id-edwards-curve-algs',
        'id-Ed25519',
        'id-Ed448',
        'id-X25519',
        'id-X448',
        'secp192r1',
        'sect163k1',
        'sect163r2',
        'secp224r1',
        'sect233k1',
        'sect233r1',
        'secp256r1',
        'sect283k1',
        'sect283r1',
        'secp384r1',
        'sect409k1',
        'sect409r1',
        'secp521r1',
        'sect571k1',
        'sect571r1',
        'secp224k1',
        'secp192k1',
        'secp160r2',
        'secp128r2',
        'secp128r1',
        'sect193r2',
        'sect193r1',
        'sect131r2',
        'sect131r1',
        'secp256k1',
        'secp160k1',
        'secp160r1',
        'secp112r2',
        'secp112r1',
        'sect113r2',
        'sect113r1',
        'sect239k1',
        'sect163r1',
        'ellipticCurveRFC5639',
        'versionOne',
        'ecStdCurvesAndGeneration',
        'brainpoolP160r1',
        'brainpoolP160t1',
        'brainpoolP192r1',
        'brainpoolP192t1',
        'brainpoolP224r1',
        'brainpoolP224t1',
        'brainpoolP256r1',
        'brainpoolP256t1',
        'brainpoolP320r1',
        'brainpoolP320t1',
        'brainpoolP384r1',
        'brainpoolP384t1',
        'brainpoolP512r1',
        'brainpoolP512t1',
        'id-at',
        'id-at-name',
        'id-at-surname',
        'id-at-givenName',
        'id-at-initials',
        'id-at-generationQualifier',
        'id-at-commonName',
        'id-at-localityName',
        'id-at-stateOrProvinceName',
        'id-at-organizationName',
        'id-at-title',
        'id-at-dnQualifier',
        'id-at-countryName',
        'id-at-serialNumber',
        'id-at-pseudonym',
        'id-domainComponent',
        'id-emailAddress',
        'id-at-organizationalUnitName',
        'id-ce',
        'id-ce-targetInformation',
        'id-ce-invalidityDate',
        'id-ce-holdInstructionCode',
        'id-ce-certificateIssuer',
        'id-ce-cRLReasons',
        'id-ce-deltaCRLIndicator',
        'id-ce-issuingDistributionPoint',
        'id-ce-cRLNumber',
        'id-ce-freshestCRL',
        'id-ce-inhibitAnyPolicy',
        'id-ce-extKeyUsage',
        'id-ce-cRLDistributionPoints',
        'id-ce-policyConstraints',
        'id-ce-nameConstraints',
        'id-ce-basicConstraints',
        'id-ce-subjectDirectoryAttributes',
        'id-ce-issuerAltName',
        'id-ce-subjectAltName',
        'id-ce-policyMappings',
        'id-ce-certificatePolicies',
        'id-ce-privateKeyUsagePeriod',
        'id-ce-keyUsage',
        'id-ce-subjectKeyIdentifier',
        'id-ce-authorityKeyIdentifier',
        'id-ad-caIssuers',
        'id-ad-timeStamping',
        'id-ad-caRepository',
        'id-pe-authorityInfoAccess',
        'id-pe-subjectInfoAccess',
        'anyExtendedKeyUsage',
        'anyPolicy',
        'id-pkix',
        'id-kp',
        'id-kp-timeStamping',
        'id-kp-emailProtection',
        'id-kp-codeSigning',
        'id-kp-clientAuth',
        'id-kp-serverAuth',
        'id-qt',
        'id-qt-cps',
        'id-qt-unotice',
        'holdInstruction',
        'id-holdinstruction-none',
        'id-holdinstruction-callissuer',
        'id-holdinstruction-reject',
        'id-sha224',
        'id-sha256',
        'id-sha384',
        'id-sha512',
        'id-sha1',
        'id-md2',
        'id-md5',
        'id-RSASSA-PSS',
        'rSASSA-PSS-Default-Identifier',
        'rSAES-OAEP-Default-Identifier',
        'id-mgf1',
        'sha1Identifier',
        'sha1',
        'mgf1SHA1',
        'id-ecPublicKey',
        'ecdsa-with-SHA224',
        'ecdsa-with-SHA256',
        'ecdsa-with-SHA384',
        'ecdsa-with-SHA512',
        'ecdsa-with-SHA2',
        'rsaEncryption',
        'md2WithRSAEncryption',
        'md5WithRSAEncryption',
        'sha1WithRSAEncryption',
        'sha224WithRSAEncryption',
        'sha256WithRSAEncryption',
        'sha384WithRSAEncryption',
        'sha512WithRSAEncryption',
        'sha512-224WithRSAEncryption',
        'sha512-256WithRSAEncryption',
        'sha-1WithRSAEncryption',
        'id-hmacWithSHA1',
        'ecdsa-with-SHA1',
        'id-dsa',
        'id-dsaWithSHA1',
        'id-dsa-with-sha1',
        'id-dsa-with-sha224',
        'id-dsa-with-sha256',
        'dhpublicnumber',
        'id-keyExchangeAlgorithm',
        'pkcs-3',
        'dhKeyAgreement',
        'pkcs-9-at-extensionRequest',
        unspecified,
        keyCompromise,
        cACompromise,
        affiliationChanged,
        superseded,
        cessationOfOperation,
        certificateHold,
        removeFromCRL,
        privilegeWithdrawn,
        aACompromise,
        'id-kp-OCSPSigning',
        'id-pkix-ocsp',
        'id-pkix-ocsp-basic',
        'id-pkix-ocsp-nonce',
        'id-pkix-ocsp-crl',
        'id-pkix-ocsp-response',
        'id-pkix-ocsp-nocheck',
        'id-pkix-ocsp-archive-cutoff',
        'id-pkix-ocsp-service-locator',
        'id-pkix-ocsp-pref-sig-algs',
        'id-pkix-ocsp-extended-revoke',
        'id-ad-ocsp',
        'pkcs-5',
        'pbeWithSHA1AndRC2-CBC',
        'pbeWithSHA1AndDES-CBC',
        'pbeWithMD5AndRC2-CBC',
        'pbeWithMD5AndDES-CBC',
        'pbeWithMD2AndRC2-CBC',
        'pbeWithMD2AndDES-CBC',
        'id-PBES2',
        'id-PBKDF2',
        'defaultPBKDF2',
        'id-hmacWithSHA224',
        'id-hmacWithSHA256',
        'id-hmacWithSHA384',
        'id-hmacWithSHA512',
        'aes',
        'id-aes256-wrap',
        'id-aes192-wrap',
        'id-aes128-wrap',
        'id-aes128-CBC',
        'id-aes192-CBC',
        'id-aes256-CBC',
        'rc2CBC',
        'des-EDE3-CBC',
        'desCBC',
        'id-ct-contentInfo',
        'id-data',
        'id-signedData',
        'id-envelopedData',
        'id-digestedData',
        'id-encryptedData',
        'id-ct-authData',
        'id-contentType',
        'id-messageDigest',
        'id-signingTime',
        'id-countersignature',
        'des-ede3-cbc',
        'rc2-cbc',
        'encryptedData',
        'digestedData',
        'envelopedData',
        'signedData',
        'data',
        'ubMax',
        'ub-match',
        'ub-common-name-length',
        'ub-country-name-alpha-length',
        'ub-country-name-numeric-length',
        'ub-domain-defined-attributes',
        'ub-domain-defined-attribute-type-length',
        'ub-domain-defined-attribute-value-length',
        'ub-domain-name-length',
        'ub-extension-attributes',
        'ub-e163-4-number-length',
        'ub-e163-4-sub-address-length',
        'ub-generation-qualifier-length',
        'ub-given-name-length',
        'ub-initials-length',
        'ub-integer-options',
        'ub-numeric-user-id-length',
        'ub-organization-name-length',
        'ub-organizational-unit-name-length',
        'ub-organizational-units',
        'ub-pds-name-length',
        'ub-pds-parameter-length',
        'ub-pds-physical-address-lines',
        'ub-postal-code-length',
        'ub-surname-length',
        'ub-terminal-id-length',
        'ub-unformatted-address-length',
        'ub-x121-address-length',
        'ub-state-name',
        'ub-organization-name',
        'ub-organizational-unit-name',
        'ub-title',
        'ub-serial-number',
        'ub-pseudonym',
        'ub-emailaddress-length',
        'ub-locality-name',
        'ub-common-name',
        'ub-name'
    ].

record('RSAPublicKey') ->
    {record_info(fields, 'RSAPublicKey'), #'RSAPublicKey'{}};
record('RSAPrivateKey') ->
    {record_info(fields, 'RSAPrivateKey'), #'RSAPrivateKey'{}};
record('OtherPrimeInfo') ->
    {record_info(fields, 'OtherPrimeInfo'), #'OtherPrimeInfo'{}};
record('RSASSA-PSS-params') ->
    {record_info(fields, 'RSASSA-PSS-params'), #'RSASSA-PSS-params'{}};
record('RSASSA-AlgorithmIdentifier') ->
    {record_info(fields, 'RSASSA-AlgorithmIdentifier'), #'RSASSA-AlgorithmIdentifier'{}};
record('RSAES-OAEP-params') ->
    {record_info(fields, 'RSAES-OAEP-params'), #'RSAES-OAEP-params'{}};
record('RSAES-AlgorithmIdentifier') ->
    {record_info(fields, 'RSAES-AlgorithmIdentifier'), #'RSAES-AlgorithmIdentifier'{}};
record('HashAlgorithm') ->
    {record_info(fields, 'HashAlgorithm'), #'HashAlgorithm'{}};
record('MaskGenAlgorithm') ->
    {record_info(fields, 'MaskGenAlgorithm'), #'MaskGenAlgorithm'{}};
record('PSourceAlgorithm') ->
    {record_info(fields, 'PSourceAlgorithm'), #'PSourceAlgorithm'{}};
record('ML-DSAPrivateKey') ->
    {record_info(fields, 'ML-DSAPrivateKey'), #'ML-DSAPrivateKey'{}};
record('ML-DSAPublicKey') ->
    {record_info(fields, 'ML-DSAPublicKey'), #'ML-DSAPublicKey'{}};
record('ML-KEMPrivateKey') ->
    {record_info(fields, 'ML-KEMPrivateKey'), #'ML-KEMPrivateKey'{}};
record('ML-KEMPublicKey') ->
    {record_info(fields, 'ML-KEMPublicKey'), #'ML-KEMPublicKey'{}};
record('SLH-DSAPrivateKey') ->
    {record_info(fields, 'SLH-DSAPrivateKey'), #'SLH-DSAPrivateKey'{}};
record('SLH-DSAPublicKey') ->
    {record_info(fields, 'SLH-DSAPublicKey'), #'SLH-DSAPublicKey'{}};
record('DSAPrivateKey') ->
    {record_info(fields, 'DSAPrivateKey'), #'DSAPrivateKey'{}};
record('Dss-Parms') ->
    {record_info(fields, 'Dss-Parms'), #'Dss-Parms'{}};
record('DomainParameters') ->
    {record_info(fields, 'DomainParameters'), #'DomainParameters'{}};
record('ValidationParams') ->
    {record_info(fields, 'ValidationParams'), #'ValidationParams'{}};
record('Dss-Sig-Value') ->
    {record_info(fields, 'Dss-Sig-Value'), #'Dss-Sig-Value'{}};
record('ECPrivateKey') ->
    {record_info(fields, 'ECPrivateKey'), #'ECPrivateKey'{}};
record('ECParameters') ->
    {record_info(fields, 'ECParameters'), #'ECParameters'{}};
record('Curve') ->
    {record_info(fields, 'Curve'), #'Curve'{}};
record('FieldID') ->
    {record_info(fields, 'FieldID'), #'FieldID'{}};
record('ECPoint') ->
    {record_info(fields, 'ECPoint'), #'ECPoint'{}};
record('ECDSA-Sig-Value') ->
    {record_info(fields, 'ECDSA-Sig-Value'), #'ECDSA-Sig-Value'{}};
record('Certificate') ->
    {record_info(fields, 'Certificate'), #'Certificate'{}};
record('TBSCertificate') ->
    {record_info(fields, 'TBSCertificate'), #'TBSCertificate'{}};
record('TBSCertificate_signature') ->
    {record_info(fields, 'TBSCertificate_signature'), #'TBSCertificate_signature'{}};
record('Validity') ->
    {record_info(fields, 'Validity'), #'Validity'{}};
record('SubjectPublicKeyInfo') ->
    {record_info(fields, 'SubjectPublicKeyInfo'), #'SubjectPublicKeyInfo'{}};
record('AlgorithmIdentifier') ->
    {record_info(fields, 'AlgorithmIdentifier'), #'AlgorithmIdentifier'{}};
record('Certificate_algorithmIdentifier') ->
    {record_info(fields, 'Certificate_algorithmIdentifier'), #'Certificate_algorithmIdentifier'{}};
record('AttributeTypeAndValue') ->
    {record_info(fields, 'AttributeTypeAndValue'), #'AttributeTypeAndValue'{}};
record('Extension') ->
    {record_info(fields, 'Extension'), #'Extension'{}};
record('AttributeSet') ->
    {record_info(fields, 'AttributeSet'), #'AttributeSet'{}};
record('SingleAttribute') ->
    {record_info(fields, 'SingleAttribute'), #'SingleAttribute'{}};
record('SecurityCategory') ->
    {record_info(fields, 'SecurityCategory'), #'SecurityCategory'{}};
record('AuthorityKeyIdentifier') ->
    {record_info(fields, 'AuthorityKeyIdentifier'), #'AuthorityKeyIdentifier'{}};
record('PrivateKeyUsagePeriod') ->
    {record_info(fields, 'PrivateKeyUsagePeriod'), #'PrivateKeyUsagePeriod'{}};
record('PolicyInformation') ->
    {record_info(fields, 'PolicyInformation'), #'PolicyInformation'{}};
record('PolicyQualifierInfo') ->
    {record_info(fields, 'PolicyQualifierInfo'), #'PolicyQualifierInfo'{}};
record('UserNotice') ->
    {record_info(fields, 'UserNotice'), #'UserNotice'{}};
record('NoticeReference') ->
    {record_info(fields, 'NoticeReference'), #'NoticeReference'{}};
record('PolicyMappings_SEQOF') ->
    {record_info(fields, 'PolicyMappings_SEQOF'), #'PolicyMappings_SEQOF'{}};
record('EDIPartyName') ->
    {record_info(fields, 'EDIPartyName'), #'EDIPartyName'{}};
record('SubjectDirectoryAttributes_SEQOF') ->
    {record_info(fields, 'SubjectDirectoryAttributes_SEQOF'), #'SubjectDirectoryAttributes_SEQOF'{}};
record('BasicConstraints') ->
    {record_info(fields, 'BasicConstraints'), #'BasicConstraints'{}};
record('NameConstraints') ->
    {record_info(fields, 'NameConstraints'), #'NameConstraints'{}};
record('GeneralSubtree') ->
    {record_info(fields, 'GeneralSubtree'), #'GeneralSubtree'{}};
record('PolicyConstraints') ->
    {record_info(fields, 'PolicyConstraints'), #'PolicyConstraints'{}};
record('DistributionPoint') ->
    {record_info(fields, 'DistributionPoint'), #'DistributionPoint'{}};
record('AccessDescription') ->
    {record_info(fields, 'AccessDescription'), #'AccessDescription'{}};
record('OTPCertificate') ->
    {record_info(fields, 'OTPCertificate'), #'OTPCertificate'{}};
record('OTPTBSCertificate') ->
    {record_info(fields, 'OTPTBSCertificate'), #'OTPTBSCertificate'{}};
record('OTPSubjectPublicKeyInfo') ->
    {record_info(fields, 'OTPSubjectPublicKeyInfo'), #'OTPSubjectPublicKeyInfo'{}};
record('SignatureAlgorithm') ->
    {record_info(fields, 'SignatureAlgorithm'), #'SignatureAlgorithm'{}};
record('PublicKeyAlgorithm') ->
    {record_info(fields, 'PublicKeyAlgorithm'), #'PublicKeyAlgorithm'{}};
record(cert) ->
    {record_info(fields, cert), #cert{}};
record('DigestAlgorithm') ->
    {record_info(fields, 'DigestAlgorithm'), #'DigestAlgorithm'{}};
record('DHParameter') ->
    {record_info(fields, 'DHParameter'), #'DHParameter'{}};
record('CertificateList') ->
    {record_info(fields, 'CertificateList'), #'CertificateList'{}};
record('TBSCertList') ->
    {record_info(fields, 'TBSCertList'), #'TBSCertList'{}};
record('TBSCertList_signature') ->
    {record_info(fields, 'TBSCertList_signature'), #'TBSCertList_signature'{}};
record('CertificateList_algorithmIdentifier') ->
    {record_info(fields, 'CertificateList_algorithmIdentifier'), #'CertificateList_algorithmIdentifier'{}};
record('TBSCertList_revokedCertificates_SEQOF') ->
    {record_info(fields, 'TBSCertList_revokedCertificates_SEQOF'), #'TBSCertList_revokedCertificates_SEQOF'{}};
record('IssuingDistributionPoint') ->
    {record_info(fields, 'IssuingDistributionPoint'), #'IssuingDistributionPoint'{}};
record('CertificationRequest') ->
    {record_info(fields, 'CertificationRequest'), #'CertificationRequest'{}};
record('CertificationRequestInfo') ->
    {record_info(fields, 'CertificationRequestInfo'), #'CertificationRequestInfo'{}};
record('CertificationRequestInfo_subjectPKInfo') ->
    {record_info(fields, 'CertificationRequestInfo_subjectPKInfo'), #'CertificationRequestInfo_subjectPKInfo'{}};
record('CertificationRequestInfo_subjectPKInfo_algorithm') ->
    {
        record_info(fields, 'CertificationRequestInfo_subjectPKInfo_algorithm'),
        #'CertificationRequestInfo_subjectPKInfo_algorithm'{}
    };
record('CertificationRequestInfo_attributes_SETOF') ->
    {record_info(fields, 'CertificationRequestInfo_attributes_SETOF'), #'CertificationRequestInfo_attributes_SETOF'{}};
record('CertificationRequest_signatureAlgorithm') ->
    {record_info(fields, 'CertificationRequest_signatureAlgorithm'), #'CertificationRequest_signatureAlgorithm'{}};
record('OCSPRequest') ->
    {record_info(fields, 'OCSPRequest'), #'OCSPRequest'{}};
record('TBSRequest') ->
    {record_info(fields, 'TBSRequest'), #'TBSRequest'{}};
record('TBSRequest_requestExtensions_SEQOF') ->
    {record_info(fields, 'TBSRequest_requestExtensions_SEQOF'), #'TBSRequest_requestExtensions_SEQOF'{}};
record('Signature') ->
    {record_info(fields, 'Signature'), #'Signature'{}};
record('Signature_signatureAlgorithm') ->
    {record_info(fields, 'Signature_signatureAlgorithm'), #'Signature_signatureAlgorithm'{}};
record('Request') ->
    {record_info(fields, 'Request'), #'Request'{}};
record('Request_singleRequestExtensions_SEQOF') ->
    {record_info(fields, 'Request_singleRequestExtensions_SEQOF'), #'Request_singleRequestExtensions_SEQOF'{}};
record('CertID') ->
    {record_info(fields, 'CertID'), #'CertID'{}};
record('CertID_hashAlgorithm') ->
    {record_info(fields, 'CertID_hashAlgorithm'), #'CertID_hashAlgorithm'{}};
record('OCSPResponse') ->
    {record_info(fields, 'OCSPResponse'), #'OCSPResponse'{}};
record('ResponseBytes') ->
    {record_info(fields, 'ResponseBytes'), #'ResponseBytes'{}};
record('BasicOCSPResponse') ->
    {record_info(fields, 'BasicOCSPResponse'), #'BasicOCSPResponse'{}};
record('ResponseData') ->
    {record_info(fields, 'ResponseData'), #'ResponseData'{}};
record('BasicOCSPResponse_signatureAlgorithm') ->
    {record_info(fields, 'BasicOCSPResponse_signatureAlgorithm'), #'BasicOCSPResponse_signatureAlgorithm'{}};
record('ResponseData_responseExtensions_SEQOF') ->
    {record_info(fields, 'ResponseData_responseExtensions_SEQOF'), #'ResponseData_responseExtensions_SEQOF'{}};
record('SingleResponse') ->
    {record_info(fields, 'SingleResponse'), #'SingleResponse'{}};
record('SingleResponse_singleExtensions_SEQOF') ->
    {record_info(fields, 'SingleResponse_singleExtensions_SEQOF'), #'SingleResponse_singleExtensions_SEQOF'{}};
record('RevokedInfo') ->
    {record_info(fields, 'RevokedInfo'), #'RevokedInfo'{}};
record('ServiceLocator') ->
    {record_info(fields, 'ServiceLocator'), #'ServiceLocator'{}};
record('CrlID') ->
    {record_info(fields, 'CrlID'), #'CrlID'{}};
record('PreferredSignatureAlgorithm') ->
    {record_info(fields, 'PreferredSignatureAlgorithm'), #'PreferredSignatureAlgorithm'{}};
record('PreferredSignatureAlgorithm_sigIdentifier') ->
    {record_info(fields, 'PreferredSignatureAlgorithm_sigIdentifier'), #'PreferredSignatureAlgorithm_sigIdentifier'{}};
record('PreferredSignatureAlgorithm_certIdentifier') ->
    {
        record_info(fields, 'PreferredSignatureAlgorithm_certIdentifier'),
        #'PreferredSignatureAlgorithm_certIdentifier'{}
    };
record('PrivateKeyInfo') ->
    {record_info(fields, 'PrivateKeyInfo'), #'PrivateKeyInfo'{}};
record('PrivateKeyInfo_privateKeyAlgorithm') ->
    {record_info(fields, 'PrivateKeyInfo_privateKeyAlgorithm'), #'PrivateKeyInfo_privateKeyAlgorithm'{}};
record('EncryptedPrivateKeyInfo') ->
    {record_info(fields, 'EncryptedPrivateKeyInfo'), #'EncryptedPrivateKeyInfo'{}};
record('EncryptedPrivateKeyInfo_encryptionAlgorithm') ->
    {
        record_info(fields, 'EncryptedPrivateKeyInfo_encryptionAlgorithm'),
        #'EncryptedPrivateKeyInfo_encryptionAlgorithm'{}
    };
record('OneAsymmetricKey') ->
    {record_info(fields, 'OneAsymmetricKey'), #'OneAsymmetricKey'{}};
record('RC2-CBC-Parameter') ->
    {record_info(fields, 'RC2-CBC-Parameter'), #'RC2-CBC-Parameter'{}};
record('PBES2-params') ->
    {record_info(fields, 'PBES2-params'), #'PBES2-params'{}};
record('PBES2-params_keyDerivationFunc') ->
    {record_info(fields, 'PBES2-params_keyDerivationFunc'), #'PBES2-params_keyDerivationFunc'{}};
record('PBES2-params_encryptionScheme') ->
    {record_info(fields, 'PBES2-params_encryptionScheme'), #'PBES2-params_encryptionScheme'{}};
record('PBEParameter') ->
    {record_info(fields, 'PBEParameter'), #'PBEParameter'{}};
record('PBKDF2-params') ->
    {record_info(fields, 'PBKDF2-params'), #'PBKDF2-params'{}};
record('PBKDF2-params_prf') ->
    {record_info(fields, 'PBKDF2-params_prf'), #'PBKDF2-params_prf'{}};
record('EncryptionAlgorithmIdentifier') ->
    {record_info(fields, 'EncryptionAlgorithmIdentifier'), #'EncryptionAlgorithmIdentifier'{}};
record('Attribute') ->
    {record_info(fields, 'Attribute'), #'Attribute'{}};
record('ContentInfo') ->
    {record_info(fields, 'ContentInfo'), #'ContentInfo'{}};
record('SignedData') ->
    {record_info(fields, 'SignedData'), #'SignedData'{}};
record('EncapsulatedContentInfo') ->
    {record_info(fields, 'EncapsulatedContentInfo'), #'EncapsulatedContentInfo'{}};
record('SignerInfo') ->
    {record_info(fields, 'SignerInfo'), #'SignerInfo'{}};
record('SignerInfo_unsignedAttrs_SETOF') ->
    {record_info(fields, 'SignerInfo_unsignedAttrs_SETOF'), #'SignerInfo_unsignedAttrs_SETOF'{}};
record('SignedAttributes_SETOF') ->
    {record_info(fields, 'SignedAttributes_SETOF'), #'SignedAttributes_SETOF'{}};
record('EnvelopedData') ->
    {record_info(fields, 'EnvelopedData'), #'EnvelopedData'{}};
record('EnvelopedData_unprotectedAttrs_SETOF') ->
    {record_info(fields, 'EnvelopedData_unprotectedAttrs_SETOF'), #'EnvelopedData_unprotectedAttrs_SETOF'{}};
record('OriginatorInfo') ->
    {record_info(fields, 'OriginatorInfo'), #'OriginatorInfo'{}};
record('EncryptedContentInfo') ->
    {record_info(fields, 'EncryptedContentInfo'), #'EncryptedContentInfo'{}};
record('KeyTransRecipientInfo') ->
    {record_info(fields, 'KeyTransRecipientInfo'), #'KeyTransRecipientInfo'{}};
record('KeyTransRecipientInfo_keyEncryptionAlgorithm') ->
    {
        record_info(fields, 'KeyTransRecipientInfo_keyEncryptionAlgorithm'),
        #'KeyTransRecipientInfo_keyEncryptionAlgorithm'{}
    };
record('KeyAgreeRecipientInfo') ->
    {record_info(fields, 'KeyAgreeRecipientInfo'), #'KeyAgreeRecipientInfo'{}};
record('KeyAgreeRecipientInfo_keyEncryptionAlgorithm') ->
    {
        record_info(fields, 'KeyAgreeRecipientInfo_keyEncryptionAlgorithm'),
        #'KeyAgreeRecipientInfo_keyEncryptionAlgorithm'{}
    };
record('OriginatorPublicKey') ->
    {record_info(fields, 'OriginatorPublicKey'), #'OriginatorPublicKey'{}};
record('OriginatorPublicKey_algorithm') ->
    {record_info(fields, 'OriginatorPublicKey_algorithm'), #'OriginatorPublicKey_algorithm'{}};
record('RecipientEncryptedKey') ->
    {record_info(fields, 'RecipientEncryptedKey'), #'RecipientEncryptedKey'{}};
record('RecipientKeyIdentifier') ->
    {record_info(fields, 'RecipientKeyIdentifier'), #'RecipientKeyIdentifier'{}};
record('KEKRecipientInfo') ->
    {record_info(fields, 'KEKRecipientInfo'), #'KEKRecipientInfo'{}};
record('KEKIdentifier') ->
    {record_info(fields, 'KEKIdentifier'), #'KEKIdentifier'{}};
record('PasswordRecipientInfo') ->
    {record_info(fields, 'PasswordRecipientInfo'), #'PasswordRecipientInfo'{}};
record('OtherRecipientInfo') ->
    {record_info(fields, 'OtherRecipientInfo'), #'OtherRecipientInfo'{}};
record('DigestedData') ->
    {record_info(fields, 'DigestedData'), #'DigestedData'{}};
record('EncryptedData') ->
    {record_info(fields, 'EncryptedData'), #'EncryptedData'{}};
record('EncryptedData_unprotectedAttrs_SETOF') ->
    {record_info(fields, 'EncryptedData_unprotectedAttrs_SETOF'), #'EncryptedData_unprotectedAttrs_SETOF'{}};
record('AuthenticatedData') ->
    {record_info(fields, 'AuthenticatedData'), #'AuthenticatedData'{}};
record('AuthAttributes_SETOF') ->
    {record_info(fields, 'AuthAttributes_SETOF'), #'AuthAttributes_SETOF'{}};
record('UnauthAttributes_SETOF') ->
    {record_info(fields, 'UnauthAttributes_SETOF'), #'UnauthAttributes_SETOF'{}};
record('DigestAlgorithmIdentifier') ->
    {record_info(fields, 'DigestAlgorithmIdentifier'), #'DigestAlgorithmIdentifier'{}};
record('SignatureAlgorithmIdentifier') ->
    {record_info(fields, 'SignatureAlgorithmIdentifier'), #'SignatureAlgorithmIdentifier'{}};
record('KeyEncryptionAlgorithmIdentifier') ->
    {record_info(fields, 'KeyEncryptionAlgorithmIdentifier'), #'KeyEncryptionAlgorithmIdentifier'{}};
record('ContentEncryptionAlgorithmIdentifier') ->
    {record_info(fields, 'ContentEncryptionAlgorithmIdentifier'), #'ContentEncryptionAlgorithmIdentifier'{}};
record('MessageAuthenticationCodeAlgorithm') ->
    {record_info(fields, 'MessageAuthenticationCodeAlgorithm'), #'MessageAuthenticationCodeAlgorithm'{}};
record('KeyDerivationAlgorithmIdentifier') ->
    {record_info(fields, 'KeyDerivationAlgorithmIdentifier'), #'KeyDerivationAlgorithmIdentifier'{}};
record('OtherRevocationInfoFormat') ->
    {record_info(fields, 'OtherRevocationInfoFormat'), #'OtherRevocationInfoFormat'{}};
record('OtherCertificateFormat') ->
    {record_info(fields, 'OtherCertificateFormat'), #'OtherCertificateFormat'{}};
record('IssuerAndSerialNumber') ->
    {record_info(fields, 'IssuerAndSerialNumber'), #'IssuerAndSerialNumber'{}};
record('OtherKeyAttribute') ->
    {record_info(fields, 'OtherKeyAttribute'), #'OtherKeyAttribute'{}};
record('ExtendedCertificate') ->
    {record_info(fields, 'ExtendedCertificate'), #'ExtendedCertificate'{}};
record('ExtendedCertificateInfo') ->
    {record_info(fields, 'ExtendedCertificateInfo'), #'ExtendedCertificateInfo'{}};
record('ORAddress') ->
    {record_info(fields, 'ORAddress'), #'ORAddress'{}};
record('BuiltInStandardAttributes') ->
    {record_info(fields, 'BuiltInStandardAttributes'), #'BuiltInStandardAttributes'{}};
record('PersonalName') ->
    {record_info(fields, 'PersonalName'), #'PersonalName'{}};
record('BuiltInDomainDefinedAttribute') ->
    {record_info(fields, 'BuiltInDomainDefinedAttribute'), #'BuiltInDomainDefinedAttribute'{}};
record('ExtensionAttribute') ->
    {record_info(fields, 'ExtensionAttribute'), #'ExtensionAttribute'{}};
record('PDSParameter') ->
    {record_info(fields, 'PDSParameter'), #'PDSParameter'{}};
record('PresentationAddress') ->
    {record_info(fields, 'PresentationAddress'), #'PresentationAddress'{}};
record('TeletexDomainDefinedAttribute') ->
    {record_info(fields, 'TeletexDomainDefinedAttribute'), #'TeletexDomainDefinedAttribute'{}}.

macro('pkcs-1') -> ?'pkcs-1';
macro('id-RSAES-OAEP') -> ?'id-RSAES-OAEP';
macro('id-pSpecified') -> ?'id-pSpecified';
macro('pSpecifiedEmpty') -> ?'pSpecifiedEmpty';
macro('emptyString') -> ?'emptyString';
macro('nullOctetString') -> ?'nullOctetString';
macro('nullParameters') -> ?'nullParameters';
macro('id-ml-dsa-44') -> ?'id-ml-dsa-44';
macro('id-ml-dsa-65') -> ?'id-ml-dsa-65';
macro('id-ml-dsa-87') -> ?'id-ml-dsa-87';
macro('id-alg-ml-kem-512') -> ?'id-alg-ml-kem-512';
macro('id-alg-ml-kem-768') -> ?'id-alg-ml-kem-768';
macro('id-alg-ml-kem-1024') -> ?'id-alg-ml-kem-1024';
macro('id-slh-dsa-sha2-128s') -> ?'id-slh-dsa-sha2-128s';
macro('id-slh-dsa-sha2-128f') -> ?'id-slh-dsa-sha2-128f';
macro('id-slh-dsa-sha2-192s') -> ?'id-slh-dsa-sha2-192s';
macro('id-slh-dsa-sha2-192f') -> ?'id-slh-dsa-sha2-192f';
macro('id-slh-dsa-sha2-256s') -> ?'id-slh-dsa-sha2-256s';
macro('id-slh-dsa-sha2-256f') -> ?'id-slh-dsa-sha2-256f';
macro('id-slh-dsa-shake-128s') -> ?'id-slh-dsa-shake-128s';
macro('id-slh-dsa-shake-128f') -> ?'id-slh-dsa-shake-128f';
macro('id-slh-dsa-shake-192s') -> ?'id-slh-dsa-shake-192s';
macro('id-slh-dsa-shake-192f') -> ?'id-slh-dsa-shake-192f';
macro('id-slh-dsa-shake-256s') -> ?'id-slh-dsa-shake-256s';
macro('id-slh-dsa-shake-256f') -> ?'id-slh-dsa-shake-256f';
macro('id-edwards-curve-algs') -> ?'id-edwards-curve-algs';
macro('id-Ed25519') -> ?'id-Ed25519';
macro('id-Ed448') -> ?'id-Ed448';
macro('id-X25519') -> ?'id-X25519';
macro('id-X448') -> ?'id-X448';
macro('secp192r1') -> ?'secp192r1';
macro('sect163k1') -> ?'sect163k1';
macro('sect163r2') -> ?'sect163r2';
macro('secp224r1') -> ?'secp224r1';
macro('sect233k1') -> ?'sect233k1';
macro('sect233r1') -> ?'sect233r1';
macro('secp256r1') -> ?'secp256r1';
macro('sect283k1') -> ?'sect283k1';
macro('sect283r1') -> ?'sect283r1';
macro('secp384r1') -> ?'secp384r1';
macro('sect409k1') -> ?'sect409k1';
macro('sect409r1') -> ?'sect409r1';
macro('secp521r1') -> ?'secp521r1';
macro('sect571k1') -> ?'sect571k1';
macro('sect571r1') -> ?'sect571r1';
macro('secp224k1') -> ?'secp224k1';
macro('secp192k1') -> ?'secp192k1';
macro('secp160r2') -> ?'secp160r2';
macro('secp128r2') -> ?'secp128r2';
macro('secp128r1') -> ?'secp128r1';
macro('sect193r2') -> ?'sect193r2';
macro('sect193r1') -> ?'sect193r1';
macro('sect131r2') -> ?'sect131r2';
macro('sect131r1') -> ?'sect131r1';
macro('secp256k1') -> ?'secp256k1';
macro('secp160k1') -> ?'secp160k1';
macro('secp160r1') -> ?'secp160r1';
macro('secp112r2') -> ?'secp112r2';
macro('secp112r1') -> ?'secp112r1';
macro('sect113r2') -> ?'sect113r2';
macro('sect113r1') -> ?'sect113r1';
macro('sect239k1') -> ?'sect239k1';
macro('sect163r1') -> ?'sect163r1';
macro('ellipticCurveRFC5639') -> ?'ellipticCurveRFC5639';
macro('versionOne') -> ?'versionOne';
macro('ecStdCurvesAndGeneration') -> ?'ecStdCurvesAndGeneration';
macro('brainpoolP160r1') -> ?'brainpoolP160r1';
macro('brainpoolP160t1') -> ?'brainpoolP160t1';
macro('brainpoolP192r1') -> ?'brainpoolP192r1';
macro('brainpoolP192t1') -> ?'brainpoolP192t1';
macro('brainpoolP224r1') -> ?'brainpoolP224r1';
macro('brainpoolP224t1') -> ?'brainpoolP224t1';
macro('brainpoolP256r1') -> ?'brainpoolP256r1';
macro('brainpoolP256t1') -> ?'brainpoolP256t1';
macro('brainpoolP320r1') -> ?'brainpoolP320r1';
macro('brainpoolP320t1') -> ?'brainpoolP320t1';
macro('brainpoolP384r1') -> ?'brainpoolP384r1';
macro('brainpoolP384t1') -> ?'brainpoolP384t1';
macro('brainpoolP512r1') -> ?'brainpoolP512r1';
macro('brainpoolP512t1') -> ?'brainpoolP512t1';
macro('id-at') -> ?'id-at';
macro('id-at-name') -> ?'id-at-name';
macro('id-at-surname') -> ?'id-at-surname';
macro('id-at-givenName') -> ?'id-at-givenName';
macro('id-at-initials') -> ?'id-at-initials';
macro('id-at-generationQualifier') -> ?'id-at-generationQualifier';
macro('id-at-commonName') -> ?'id-at-commonName';
macro('id-at-localityName') -> ?'id-at-localityName';
macro('id-at-stateOrProvinceName') -> ?'id-at-stateOrProvinceName';
macro('id-at-organizationName') -> ?'id-at-organizationName';
macro('id-at-title') -> ?'id-at-title';
macro('id-at-dnQualifier') -> ?'id-at-dnQualifier';
macro('id-at-countryName') -> ?'id-at-countryName';
macro('id-at-serialNumber') -> ?'id-at-serialNumber';
macro('id-at-pseudonym') -> ?'id-at-pseudonym';
macro('id-domainComponent') -> ?'id-domainComponent';
macro('id-emailAddress') -> ?'id-emailAddress';
macro('id-at-organizationalUnitName') -> ?'id-at-organizationalUnitName';
macro('id-ce') -> ?'id-ce';
macro('id-ce-targetInformation') -> ?'id-ce-targetInformation';
macro('id-ce-invalidityDate') -> ?'id-ce-invalidityDate';
macro('id-ce-holdInstructionCode') -> ?'id-ce-holdInstructionCode';
macro('id-ce-certificateIssuer') -> ?'id-ce-certificateIssuer';
macro('id-ce-cRLReasons') -> ?'id-ce-cRLReasons';
macro('id-ce-deltaCRLIndicator') -> ?'id-ce-deltaCRLIndicator';
macro('id-ce-issuingDistributionPoint') -> ?'id-ce-issuingDistributionPoint';
macro('id-ce-cRLNumber') -> ?'id-ce-cRLNumber';
macro('id-ce-freshestCRL') -> ?'id-ce-freshestCRL';
macro('id-ce-inhibitAnyPolicy') -> ?'id-ce-inhibitAnyPolicy';
macro('id-ce-extKeyUsage') -> ?'id-ce-extKeyUsage';
macro('id-ce-cRLDistributionPoints') -> ?'id-ce-cRLDistributionPoints';
macro('id-ce-policyConstraints') -> ?'id-ce-policyConstraints';
macro('id-ce-nameConstraints') -> ?'id-ce-nameConstraints';
macro('id-ce-basicConstraints') -> ?'id-ce-basicConstraints';
macro('id-ce-subjectDirectoryAttributes') -> ?'id-ce-subjectDirectoryAttributes';
macro('id-ce-issuerAltName') -> ?'id-ce-issuerAltName';
macro('id-ce-subjectAltName') -> ?'id-ce-subjectAltName';
macro('id-ce-policyMappings') -> ?'id-ce-policyMappings';
macro('id-ce-certificatePolicies') -> ?'id-ce-certificatePolicies';
macro('id-ce-privateKeyUsagePeriod') -> ?'id-ce-privateKeyUsagePeriod';
macro('id-ce-keyUsage') -> ?'id-ce-keyUsage';
macro('id-ce-subjectKeyIdentifier') -> ?'id-ce-subjectKeyIdentifier';
macro('id-ce-authorityKeyIdentifier') -> ?'id-ce-authorityKeyIdentifier';
macro('id-ad-caIssuers') -> ?'id-ad-caIssuers';
macro('id-ad-timeStamping') -> ?'id-ad-timeStamping';
macro('id-ad-caRepository') -> ?'id-ad-caRepository';
macro('id-pe-authorityInfoAccess') -> ?'id-pe-authorityInfoAccess';
macro('id-pe-subjectInfoAccess') -> ?'id-pe-subjectInfoAccess';
macro('anyExtendedKeyUsage') -> ?'anyExtendedKeyUsage';
macro('anyPolicy') -> ?'anyPolicy';
macro('id-pkix') -> ?'id-pkix';
macro('id-kp') -> ?'id-kp';
macro('id-kp-timeStamping') -> ?'id-kp-timeStamping';
macro('id-kp-emailProtection') -> ?'id-kp-emailProtection';
macro('id-kp-codeSigning') -> ?'id-kp-codeSigning';
macro('id-kp-clientAuth') -> ?'id-kp-clientAuth';
macro('id-kp-serverAuth') -> ?'id-kp-serverAuth';
macro('id-qt') -> ?'id-qt';
macro('id-qt-cps') -> ?'id-qt-cps';
macro('id-qt-unotice') -> ?'id-qt-unotice';
macro('holdInstruction') -> ?'holdInstruction';
macro('id-holdinstruction-none') -> ?'id-holdinstruction-none';
macro('id-holdinstruction-callissuer') -> ?'id-holdinstruction-callissuer';
macro('id-holdinstruction-reject') -> ?'id-holdinstruction-reject';
macro('id-sha224') -> ?'id-sha224';
macro('id-sha256') -> ?'id-sha256';
macro('id-sha384') -> ?'id-sha384';
macro('id-sha512') -> ?'id-sha512';
macro('id-sha1') -> ?'id-sha1';
macro('id-md2') -> ?'id-md2';
macro('id-md5') -> ?'id-md5';
macro('id-RSASSA-PSS') -> ?'id-RSASSA-PSS';
macro('rSASSA-PSS-Default-Identifier') -> ?'rSASSA-PSS-Default-Identifier';
macro('rSAES-OAEP-Default-Identifier') -> ?'rSAES-OAEP-Default-Identifier';
macro('id-mgf1') -> ?'id-mgf1';
macro('sha1Identifier') -> ?'sha1Identifier';
macro('sha1') -> ?'sha1';
macro('mgf1SHA1') -> ?'mgf1SHA1';
macro('id-ecPublicKey') -> ?'id-ecPublicKey';
macro('ecdsa-with-SHA224') -> ?'ecdsa-with-SHA224';
macro('ecdsa-with-SHA256') -> ?'ecdsa-with-SHA256';
macro('ecdsa-with-SHA384') -> ?'ecdsa-with-SHA384';
macro('ecdsa-with-SHA512') -> ?'ecdsa-with-SHA512';
macro('ecdsa-with-SHA2') -> ?'ecdsa-with-SHA2';
macro('rsaEncryption') -> ?'rsaEncryption';
macro('md2WithRSAEncryption') -> ?'md2WithRSAEncryption';
macro('md5WithRSAEncryption') -> ?'md5WithRSAEncryption';
macro('sha1WithRSAEncryption') -> ?'sha1WithRSAEncryption';
macro('sha224WithRSAEncryption') -> ?'sha224WithRSAEncryption';
macro('sha256WithRSAEncryption') -> ?'sha256WithRSAEncryption';
macro('sha384WithRSAEncryption') -> ?'sha384WithRSAEncryption';
macro('sha512WithRSAEncryption') -> ?'sha512WithRSAEncryption';
macro('sha512-224WithRSAEncryption') -> ?'sha512-224WithRSAEncryption';
macro('sha512-256WithRSAEncryption') -> ?'sha512-256WithRSAEncryption';
macro('sha-1WithRSAEncryption') -> ?'sha-1WithRSAEncryption';
macro('id-hmacWithSHA1') -> ?'id-hmacWithSHA1';
macro('ecdsa-with-SHA1') -> ?'ecdsa-with-SHA1';
macro('id-dsa') -> ?'id-dsa';
macro('id-dsaWithSHA1') -> ?'id-dsaWithSHA1';
macro('id-dsa-with-sha1') -> ?'id-dsa-with-sha1';
macro('id-dsa-with-sha224') -> ?'id-dsa-with-sha224';
macro('id-dsa-with-sha256') -> ?'id-dsa-with-sha256';
macro('dhpublicnumber') -> ?'dhpublicnumber';
macro('id-keyExchangeAlgorithm') -> ?'id-keyExchangeAlgorithm';
macro('pkcs-3') -> ?'pkcs-3';
macro('dhKeyAgreement') -> ?'dhKeyAgreement';
macro('pkcs-9-at-extensionRequest') -> ?'pkcs-9-at-extensionRequest';
macro(unspecified) -> ?unspecified;
macro(keyCompromise) -> ?keyCompromise;
macro(cACompromise) -> ?cACompromise;
macro(affiliationChanged) -> ?affiliationChanged;
macro(superseded) -> ?superseded;
macro(cessationOfOperation) -> ?cessationOfOperation;
macro(certificateHold) -> ?certificateHold;
macro(removeFromCRL) -> ?removeFromCRL;
macro(privilegeWithdrawn) -> ?privilegeWithdrawn;
macro(aACompromise) -> ?aACompromise;
macro('id-kp-OCSPSigning') -> ?'id-kp-OCSPSigning';
macro('id-pkix-ocsp') -> ?'id-pkix-ocsp';
macro('id-pkix-ocsp-basic') -> ?'id-pkix-ocsp-basic';
macro('id-pkix-ocsp-nonce') -> ?'id-pkix-ocsp-nonce';
macro('id-pkix-ocsp-crl') -> ?'id-pkix-ocsp-crl';
macro('id-pkix-ocsp-response') -> ?'id-pkix-ocsp-response';
macro('id-pkix-ocsp-nocheck') -> ?'id-pkix-ocsp-nocheck';
macro('id-pkix-ocsp-archive-cutoff') -> ?'id-pkix-ocsp-archive-cutoff';
macro('id-pkix-ocsp-service-locator') -> ?'id-pkix-ocsp-service-locator';
macro('id-pkix-ocsp-pref-sig-algs') -> ?'id-pkix-ocsp-pref-sig-algs';
macro('id-pkix-ocsp-extended-revoke') -> ?'id-pkix-ocsp-extended-revoke';
macro('id-ad-ocsp') -> ?'id-ad-ocsp';
macro('pkcs-5') -> ?'pkcs-5';
macro('pbeWithSHA1AndRC2-CBC') -> ?'pbeWithSHA1AndRC2-CBC';
macro('pbeWithSHA1AndDES-CBC') -> ?'pbeWithSHA1AndDES-CBC';
macro('pbeWithMD5AndRC2-CBC') -> ?'pbeWithMD5AndRC2-CBC';
macro('pbeWithMD5AndDES-CBC') -> ?'pbeWithMD5AndDES-CBC';
macro('pbeWithMD2AndRC2-CBC') -> ?'pbeWithMD2AndRC2-CBC';
macro('pbeWithMD2AndDES-CBC') -> ?'pbeWithMD2AndDES-CBC';
macro('id-PBES2') -> ?'id-PBES2';
macro('id-PBKDF2') -> ?'id-PBKDF2';
macro('defaultPBKDF2') -> ?'defaultPBKDF2';
macro('id-hmacWithSHA224') -> ?'id-hmacWithSHA224';
macro('id-hmacWithSHA256') -> ?'id-hmacWithSHA256';
macro('id-hmacWithSHA384') -> ?'id-hmacWithSHA384';
macro('id-hmacWithSHA512') -> ?'id-hmacWithSHA512';
macro('aes') -> ?'aes';
macro('id-aes256-wrap') -> ?'id-aes256-wrap';
macro('id-aes192-wrap') -> ?'id-aes192-wrap';
macro('id-aes128-wrap') -> ?'id-aes128-wrap';
macro('id-aes128-CBC') -> ?'id-aes128-CBC';
macro('id-aes192-CBC') -> ?'id-aes192-CBC';
macro('id-aes256-CBC') -> ?'id-aes256-CBC';
macro('rc2CBC') -> ?'rc2CBC';
macro('des-EDE3-CBC') -> ?'des-EDE3-CBC';
macro('desCBC') -> ?'desCBC';
macro('id-ct-contentInfo') -> ?'id-ct-contentInfo';
macro('id-data') -> ?'id-data';
macro('id-signedData') -> ?'id-signedData';
macro('id-envelopedData') -> ?'id-envelopedData';
macro('id-digestedData') -> ?'id-digestedData';
macro('id-encryptedData') -> ?'id-encryptedData';
macro('id-ct-authData') -> ?'id-ct-authData';
macro('id-contentType') -> ?'id-contentType';
macro('id-messageDigest') -> ?'id-messageDigest';
macro('id-signingTime') -> ?'id-signingTime';
macro('id-countersignature') -> ?'id-countersignature';
macro('des-ede3-cbc') -> ?'des-ede3-cbc';
macro('rc2-cbc') -> ?'rc2-cbc';
macro('encryptedData') -> ?'encryptedData';
macro('digestedData') -> ?'digestedData';
macro('envelopedData') -> ?'envelopedData';
macro('signedData') -> ?'signedData';
macro('data') -> ?'data';
macro('ubMax') -> ?'ubMax';
macro('ub-match') -> ?'ub-match';
macro('ub-common-name-length') -> ?'ub-common-name-length';
macro('ub-country-name-alpha-length') -> ?'ub-country-name-alpha-length';
macro('ub-country-name-numeric-length') -> ?'ub-country-name-numeric-length';
macro('ub-domain-defined-attributes') -> ?'ub-domain-defined-attributes';
macro('ub-domain-defined-attribute-type-length') -> ?'ub-domain-defined-attribute-type-length';
macro('ub-domain-defined-attribute-value-length') -> ?'ub-domain-defined-attribute-value-length';
macro('ub-domain-name-length') -> ?'ub-domain-name-length';
macro('ub-extension-attributes') -> ?'ub-extension-attributes';
macro('ub-e163-4-number-length') -> ?'ub-e163-4-number-length';
macro('ub-e163-4-sub-address-length') -> ?'ub-e163-4-sub-address-length';
macro('ub-generation-qualifier-length') -> ?'ub-generation-qualifier-length';
macro('ub-given-name-length') -> ?'ub-given-name-length';
macro('ub-initials-length') -> ?'ub-initials-length';
macro('ub-integer-options') -> ?'ub-integer-options';
macro('ub-numeric-user-id-length') -> ?'ub-numeric-user-id-length';
macro('ub-organization-name-length') -> ?'ub-organization-name-length';
macro('ub-organizational-unit-name-length') -> ?'ub-organizational-unit-name-length';
macro('ub-organizational-units') -> ?'ub-organizational-units';
macro('ub-pds-name-length') -> ?'ub-pds-name-length';
macro('ub-pds-parameter-length') -> ?'ub-pds-parameter-length';
macro('ub-pds-physical-address-lines') -> ?'ub-pds-physical-address-lines';
macro('ub-postal-code-length') -> ?'ub-postal-code-length';
macro('ub-surname-length') -> ?'ub-surname-length';
macro('ub-terminal-id-length') -> ?'ub-terminal-id-length';
macro('ub-unformatted-address-length') -> ?'ub-unformatted-address-length';
macro('ub-x121-address-length') -> ?'ub-x121-address-length';
macro('ub-state-name') -> ?'ub-state-name';
macro('ub-organization-name') -> ?'ub-organization-name';
macro('ub-organizational-unit-name') -> ?'ub-organizational-unit-name';
macro('ub-title') -> ?'ub-title';
macro('ub-serial-number') -> ?'ub-serial-number';
macro('ub-pseudonym') -> ?'ub-pseudonym';
macro('ub-emailaddress-length') -> ?'ub-emailaddress-length';
macro('ub-locality-name') -> ?'ub-locality-name';
macro('ub-common-name') -> ?'ub-common-name';
macro('ub-name') -> ?'ub-name'.
