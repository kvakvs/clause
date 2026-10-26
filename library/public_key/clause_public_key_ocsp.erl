%% clause_public_key_ocsp: public_key:pkix_ocsp_validate/5 and ocsp_extensions/1 (plan 21 step LIB80D4,
%% docs/libraries/public_key.md#ocsp-validation): RFC 6960 response checks as OTP's public_key and pubkey_ocsp do
%% them: the response status and type, the responder and its authorisation, the signature, the nonce, and the
%% single response for the certificate with its times.
-module(clause_public_key_ocsp).
-export([ocsp_extensions/1, pkix_ocsp_validate/5]).

-include("include/public_key.hrl").

%% The largest response decoded, as OTP (OpenSSL's OSSL_HTTP_DEFAULT_MAX_RESP_LEN).
-define(MAX_OCSP_RESPONSE_SIZE, 102400).

%% public_key:pkix_ocsp_validate/5: the certificate's status in a DER OCSP response.
pkix_ocsp_validate(DerCert, IssuerCert, OcspRespDer, NonceExt, Options) when is_binary(DerCert) ->
    pkix_ocsp_validate(public_key:pkix_decode_cert(DerCert, otp), IssuerCert, OcspRespDer, NonceExt, Options);
pkix_ocsp_validate(Cert, DerIssuerCert, OcspRespDer, NonceExt, Options) when is_binary(DerIssuerCert) ->
    pkix_ocsp_validate(Cert, public_key:pkix_decode_cert(DerIssuerCert, otp), OcspRespDer, NonceExt, Options);
pkix_ocsp_validate(Cert, IssuerCert, OcspRespDer, NonceExt, Options) when
    is_record(Cert, 'OTPCertificate'), is_record(IssuerCert, 'OTPCertificate')
->
    IsTrustedResponderFun = proplists:get_value(is_trusted_responder_fun, Options, fun(_) -> false end),
    maybe
        {ok, #'BasicOCSPResponse'{certs = Certs} = BasicOcspResponse} ?= decode_response(OcspRespDer),
        ResponderCerts =
            [clause_public_key_path:combined_cert(C) || C <- certs(Certs)] ++
                [#cert{der = <<>>, otp = IssuerCert}],
        {ok, Responses, Details} ?=
            verify_response(BasicOcspResponse, ResponderCerts, NonceExt, IssuerCert, IsTrustedResponderFun),
        {ok, #'SingleResponse'{certStatus = CertStatus}} ?= find_single_response(Cert, IssuerCert, Responses),
        status(CertStatus, Details)
    else
        {error, Reason} -> {error, {bad_cert, {revocation_status_undetermined, Reason}}}
    end.

certs(asn1_NOVALUE) -> [];
certs(Certs) -> Certs.

%% public_key:ocsp_extensions/1: the request's nonce extension, if any.
ocsp_extensions(undefined) -> [];
ocsp_extensions(Nonce) when is_binary(Nonce) -> [#'Extension'{extnID = ?'id-pkix-ocsp-nonce', extnValue = Nonce}].

decode_response(ResponseDer) when byte_size(ResponseDer) =< ?MAX_OCSP_RESPONSE_SIZE ->
    case public_key:der_decode('OCSPResponse', ResponseDer) of
        #'OCSPResponse'{responseStatus = successful, responseBytes = ResponseBytes} ->
            decode_response_bytes(ResponseBytes);
        #'OCSPResponse'{responseStatus = Error} ->
            {error, Error}
    end;
decode_response(ResponseDer) when is_binary(ResponseDer) ->
    {error, {ocsp_response_too_large, byte_size(ResponseDer)}}.

decode_response_bytes(#'ResponseBytes'{responseType = ?'id-pkix-ocsp-basic', response = Data}) ->
    {ok, public_key:der_decode('BasicOCSPResponse', Data)};
decode_response_bytes(#'ResponseBytes'{responseType = RespType}) ->
    {error, {ocsp_response_type_not_supported, RespType}}.

%% The response was produced in the past, by an authorised responder whose key verifies it, with the nonce sent.
verify_response(
    #'BasicOCSPResponse'{tbsResponseData = ResponseData, signatureAlgorithm = SignatureAlgo, signature = Signature},
    ResponderCerts,
    Nonce,
    IssuerCert,
    IsTrustedResponderFun
) ->
    #'ResponseData'{responderID = ResponderID, producedAt = ProducedAt} = ResponseData,
    Algorithm = SignatureAlgo#'BasicOCSPResponse_signatureAlgorithm'.algorithm,
    Responder = {ResponderID, IssuerCert, IsTrustedResponderFun},
    maybe
        ok ?= verify_past_timestamp(ProducedAt),
        ok ?=
            verify_signature(
                public_key:der_encode('ResponseData', ResponseData), Algorithm, Signature, ResponderCerts, Responder
            ),
        verify_nonce(ResponseData, Nonce)
    end.

%% The first responder certificate that the responder ID names, is authorised and verifies the signature.
verify_signature(_, _, _, [], _) ->
    {error, ocsp_responder_cert_not_found};
verify_signature(
    ResponseDataDer, Algorithm, Signature, [ResponderCert | Rest], {ResponderID, IssuerCert, TrustedFun} = Responder
) ->
    maybe
        true ?= is_responder_cert(ResponderID, ResponderCert),
        true ?= is_authorized_responder(ResponderCert, IssuerCert, TrustedFun),
        ok ?= do_verify_signature(ResponseDataDer, Signature, Algorithm, ResponderCert)
    else
        _ -> verify_signature(ResponseDataDer, Algorithm, Signature, Rest, Responder)
    end.

is_responder_cert({byName, Name}, #cert{otp = Cert}) ->
    public_key:der_encode('Name', Name) == subject_name(Cert);
is_responder_cert({byKey, Key}, #cert{otp = Cert}) ->
    hash_equals(Key, crypto:hash(sha, public_key_der(Cert))).

%% A responder certificate in its validity period that is the issuer itself, a designated responder the issuer
%% signed for OCSP signing, or one the trusted responder fun accepts.
is_authorized_responder(#cert{otp = ResponderCert} = CombinedCert, IssuerCert, TrustedFun) ->
    case clause_public_key_path:validity(ResponderCert) of
        ok -> check_responder_authorization(CombinedCert, IssuerCert, TrustedFun);
        _ExpiredOrError -> not_authorized_responder
    end.

check_responder_authorization(#cert{der = Der, otp = ResponderCert} = CombinedCert, IssuerCert, TrustedFun) ->
    Authorized =
        ResponderCert == IssuerCert orelse
            (public_key:pkix_is_issuer(ResponderCert, IssuerCert) andalso
                designated_for_ocsp_signing(ResponderCert) andalso
                public_key:pkix_verify(Der, public_key_rec(IssuerCert))) orelse
            TrustedFun(CombinedCert),
    case Authorized of
        true -> true;
        false -> not_authorized_responder
    end.

designated_for_ocsp_signing(#'OTPCertificate'{tbsCertificate = TBSCert}) ->
    Extensions = clause_public_key_cert:extensions_list(TBSCert#'OTPTBSCertificate'.extensions),
    case clause_public_key_cert:select_extension(?'id-ce-extKeyUsage', Extensions) of
        undefined -> false;
        #'Extension'{extnValue = KeyUses} -> lists:member(?'id-kp-OCSPSigning', KeyUses)
    end.

do_verify_signature(ResponseDataDer, Signature, Algorithm, #cert{otp = ResponderCert}) ->
    {DigestType, _SignatureType} = public_key:pkix_sign_types(Algorithm),
    case public_key:verify(ResponseDataDer, DigestType, Signature, public_key_rec(ResponderCert)) of
        true -> ok;
        false -> {error, ocsp_response_bad_signature}
    end.

%% The responses, with {missing, ocsp_nonce} in the details when a nonce was sent but none came back.
verify_nonce(#'ResponseData'{responses = Responses, responseExtensions = ResponseExtns}, NonceSent) ->
    case {NonceSent, nonce_value(ResponseExtns)} of
        {undefined, _} -> {ok, Responses, []};
        {NonceSent, undefined} when is_binary(NonceSent) -> {ok, Responses, [{missing, ocsp_nonce}]};
        {NonceSent, NonceSent} -> {ok, Responses, []};
        _ -> {error, nonce_mismatch}
    end.

nonce_value(asn1_NOVALUE) -> undefined;
nonce_value([]) -> undefined;
nonce_value([#'Extension'{extnID = ?'id-pkix-ocsp-nonce', extnValue = Value} | _]) -> Value;
nonce_value([_ | Rest]) -> nonce_value(Rest).

%% The current single response for the certificate's serial number and issuer.
find_single_response(Cert, IssuerCert, Responses) ->
    Issuer = {subject_name(IssuerCert), public_key_der(IssuerCert)},
    SerialNum = (Cert#'OTPCertificate'.tbsCertificate)#'OTPTBSCertificate'.serialNumber,
    match_single_response(Issuer, SerialNum, Responses).

match_single_response(_, _, []) ->
    {error, no_matched_response};
match_single_response(Issuer, SerialNum, [#'SingleResponse'{certID = #'CertID'{}} = SingleResponse | Rest]) ->
    case matches(Issuer, SerialNum, SingleResponse) of
        true -> {ok, SingleResponse};
        false -> match_single_response(Issuer, SerialNum, Rest)
    end;
match_single_response(Issuer, SerialNum, [_ | Rest]) ->
    match_single_response(Issuer, SerialNum, Rest).

matches({IssuerName, IssuerKey}, SerialNum, #'SingleResponse'{certID = CertID} = SingleResponse) ->
    #'SingleResponse'{thisUpdate = ThisUpdate, nextUpdate = NextUpdate} = SingleResponse,
    #'CertID'{hashAlgorithm = Algo, issuerNameHash = NameHash, issuerKeyHash = KeyHash} = CertID,
    HashType = public_key:pkix_hash_type(Algo#'CertID_hashAlgorithm'.algorithm),
    SerialNum == CertID#'CertID'.serialNumber andalso
        hash_equals(crypto:hash(HashType, IssuerName), NameHash) andalso
        hash_equals(crypto:hash(HashType, IssuerKey), KeyHash) andalso
        verify_past_timestamp(ThisUpdate) == ok andalso
        verify_next_update(NextUpdate) == ok.

status({good, _}, Details) -> {ok, Details};
status({unknown, Reason}, _) -> {error, {bad_cert, {revocation_status_undetermined, Reason}}};
status({revoked, Reason}, _) -> {error, {bad_cert, {revoked, Reason}}}.

verify_next_update(asn1_NOVALUE) -> ok;
verify_next_update(NextUpdate) -> verify_timestamp(NextUpdate, future_timestamp).

verify_past_timestamp(Timestamp) -> verify_timestamp(Timestamp, past_timestamp).

verify_timestamp(Timestamp, Direction) ->
    Now = calendar:datetime_to_gregorian_seconds(calendar:universal_time()),
    Seconds = clause_public_key_path:gregorian_seconds({generalTime, Timestamp}),
    case Direction of
        past_timestamp when Seconds =< Now -> ok;
        future_timestamp when Now =< Seconds -> ok;
        _ -> {error, ocsp_stale_response}
    end.

%% The certificate's public key as public_key:verify/4 takes it.
public_key_rec(#'OTPCertificate'{tbsCertificate = TBSCert}) ->
    #'OTPSubjectPublicKeyInfo'{algorithm = Algorithm, subjectPublicKey = Key} =
        TBSCert#'OTPTBSCertificate'.subjectPublicKeyInfo,
    case {Key, Algorithm#'PublicKeyAlgorithm'.parameters} of
        {#'RSAPublicKey'{}, 'NULL'} -> Key;
        {_, Params} -> {Key, Params}
    end.

%% The certificate's public key bits, as CertID's issuerKeyHash and a byKey responder ID hash them.
public_key_der(Cert) ->
    case public_key_rec(Cert) of
        #'RSAPublicKey'{} = Key -> public_key:der_encode('RSAPublicKey', Key);
        {DsaInt, #'Dss-Parms'{}} when is_integer(DsaInt) -> public_key:der_encode('DSAPublicKey', DsaInt);
        {#'ECPoint'{point = Key}, _} -> Key
    end.

subject_name(#'OTPCertificate'{tbsCertificate = TBSCert}) ->
    public_key:pkix_encode('Name', TBSCert#'OTPTBSCertificate'.subject, otp).

%% Constant-time comparison; hashes of different sizes (another hash algorithm) never match.
hash_equals(A, B) when byte_size(A) =:= byte_size(B) -> crypto:hash_equals(A, B);
hash_equals(_, _) -> false.
