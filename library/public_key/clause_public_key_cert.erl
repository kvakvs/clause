%% clause_public_key_cert: the certificate functions of Clause's public_key (plan 21 step LIB80D,
%% docs/libraries/public_key.md#certificates-and-names), which public_key's functions call: name normalisation and
%% comparison, issuer and subject ids, CRL issuers and signatures, distribution points, name matching and hostname
%% verification, as OTP's pubkey_cert, pubkey_crl and public_key do them.
-module(clause_public_key_cert).
-export([
    distribution_points/1,
    extensions_list/1,
    is_dir_name/3,
    is_fixed_dh_cert/1,
    is_issuer/2,
    match_name/3,
    normalize_general_name/1,
    pkix_crl_issuer/1,
    pkix_crl_verify/2,
    pkix_dist_point/1,
    pkix_dist_points/1,
    pkix_is_fixed_dh_cert/1,
    pkix_is_issuer/2,
    pkix_is_self_signed/1,
    pkix_issuer_id/2,
    pkix_match_dist_point/2,
    pkix_normalize_name/1,
    pkix_subject_id/1,
    pkix_verify_hostname/3,
    pkix_verify_hostname_match_fun/1,
    select_extension/2,
    verify_crl_signature/4
]).

-include("include/public_key.hrl").

%% The OID of the SRV-ID other name (RFC 4985).
-define(SRV_NAME_OID, {1, 3, 6, 1, 4, 1, 434, 2, 2, 1, 37, 0}).

%%--------------------------------------------------------------------------------------------------------------------
%% Names
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:pkix_normalize_name/1.
pkix_normalize_name(Issuer) when is_binary(Issuer) ->
    pkix_normalize_name(transform(public_key:der_decode('Name', Issuer)));
pkix_normalize_name(Issuer) ->
    normalize_general_name(Issuer).

%% A name with its printable strings trimmed, their inner white space runs made single and case folded.
normalize_general_name({rdnSequence, Issuer}) ->
    {rdnSequence, lists:map(fun normalize_attribute/1, Issuer)}.

normalize_attribute([{Description, Type, {printableString, Value}}]) ->
    [{Description, Type, {printableString, string:casefold(strip_spaces(Value, false))}}];
normalize_attribute(Attribute) ->
    Attribute.

%% Whether Issuer names Candidate: the normalised names equal attribute by attribute.
is_issuer({rdnSequence, _} = Issuer, {rdnSequence, _} = Candidate) ->
    {rdnSequence, IssuerDirName} = normalize_general_name(Issuer),
    {rdnSequence, CandidateDirName} = normalize_general_name(Candidate),
    is_dir_name(IssuerDirName, CandidateDirName, true).

%% Whether two attribute lists name the same entry; without Exact, a prefix of the first may match the whole second.
is_dir_name([], [], _Exact) ->
    true;
is_dir_name([H | R1], [H | R2], Exact) ->
    is_dir_name(R1, R2, Exact);
is_dir_name(
    [[{'AttributeTypeAndValue', Type, What1}] | Rest1], [[{'AttributeTypeAndValue', Type, What2}] | Rest2], Exact
) ->
    is_dir_name2(What1, What2) andalso is_dir_name(Rest1, Rest2, Exact);
is_dir_name(_, [], false) ->
    true;
is_dir_name(_, _, _) ->
    false.

%% Attribute values equal as terms, or as printable or UTF-8 strings compared without case and extra white space.
is_dir_name2(Str, Str) ->
    true;
is_dir_name2({T1, Str1}, Str2) when T1 == printableString; T1 == utf8String ->
    is_dir_name2(Str1, Str2);
is_dir_name2(Str1, {T2, Str2}) when T2 == printableString; T2 == utf8String ->
    is_dir_name2(Str1, Str2);
is_dir_name2(Str1, Str2) when (is_list(Str1) orelse is_binary(Str1)) andalso (is_list(Str2) orelse is_binary(Str2)) ->
    string:equal(strip_spaces(Str1, true), strip_spaces(Str2, true), true);
is_dir_name2(_, _) ->
    false.

%% A string trimmed, with runs of spaces made single; deep chardata when KeepDeep, else a flat list.
strip_spaces(String0, KeepDeep) ->
    Trimmed = string:trim(String0),
    strip_many_spaces(string:split(Trimmed, "  ", all), KeepDeep).

strip_many_spaces([OnlySingleSpace], _) ->
    OnlySingleSpace;
strip_many_spaces(Strings, KeepDeep) ->
    DeepList = lists:join(" ", [string:trim(Str, leading, " ") || Str <- Strings, Str /= []]),
    case KeepDeep of
        true -> DeepList;
        false -> unicode:characters_to_list(DeepList)
    end.

%% public_key:pkix_is_issuer/2: a certificate's or a CRL's issuer against a candidate's subject.
pkix_is_issuer(Cert, IssuerCert) when is_binary(Cert) ->
    pkix_is_issuer(public_key:pkix_decode_cert(Cert, otp), IssuerCert);
pkix_is_issuer(Cert, IssuerCert) when is_binary(IssuerCert) ->
    pkix_is_issuer(Cert, public_key:pkix_decode_cert(IssuerCert, otp));
pkix_is_issuer(#'OTPCertificate'{tbsCertificate = TBSCert}, #'OTPCertificate'{tbsCertificate = Candidate}) ->
    is_issuer(TBSCert#'OTPTBSCertificate'.issuer, Candidate#'OTPTBSCertificate'.subject);
pkix_is_issuer(#'CertificateList'{tbsCertList = TBSCRL}, #'OTPCertificate'{tbsCertificate = Candidate}) ->
    is_issuer(Candidate#'OTPTBSCertificate'.subject, transform(TBSCRL#'TBSCertList'.issuer)).

%% public_key:pkix_is_self_signed/1.
pkix_is_self_signed(#'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{issuer = Issuer, subject = Subject}}) ->
    is_issuer(Issuer, Subject);
pkix_is_self_signed(Cert) when is_binary(Cert) ->
    pkix_is_self_signed(public_key:pkix_decode_cert(Cert, otp)).

%% public_key:pkix_is_fixed_dh_cert/1.
pkix_is_fixed_dh_cert(#'OTPCertificate'{} = OTPCert) ->
    is_fixed_dh_cert(OTPCert);
pkix_is_fixed_dh_cert(Cert) when is_binary(Cert) ->
    pkix_is_fixed_dh_cert(public_key:pkix_decode_cert(Cert, otp)).

%% Whether a certificate's key is a DH key that may agree on keys.
is_fixed_dh_cert(#'OTPCertificate'{
    tbsCertificate = #'OTPTBSCertificate'{subjectPublicKeyInfo = PublicKeyInfo, extensions = Extensions}
}) ->
    #'OTPSubjectPublicKeyInfo'{algorithm = #'PublicKeyAlgorithm'{algorithm = Algorithm}} = PublicKeyInfo,
    IsDh = Algorithm == ?'dhpublicnumber',
    case select_extension(?'id-ce-keyUsage', extensions_list(Extensions)) of
        undefined -> IsDh;
        #'Extension'{extnValue = KeyUses} -> IsDh andalso lists:member(keyAgreement, KeyUses)
    end.

%% public_key:pkix_issuer_id/2: the issuer's serial number and name, of the certificate itself or of its authority
%% key identifier.
pkix_issuer_id(#'OTPCertificate'{} = OtpCert, Signed) when (Signed == self) orelse (Signed == other) ->
    issuer_id(OtpCert, Signed);
pkix_issuer_id(Cert, Signed) when is_binary(Cert) ->
    pkix_issuer_id(public_key:pkix_decode_cert(Cert, otp), Signed).

issuer_id(#'OTPCertificate'{tbsCertificate = TBSCert}, other) ->
    case select_extension(?'id-ce-authorityKeyIdentifier', extensions_list(TBSCert#'OTPTBSCertificate'.extensions)) of
        undefined -> {error, issuer_not_found};
        #'Extension'{extnValue = Value} -> cert_auth_key_id(Value)
    end;
issuer_id(#'OTPCertificate'{tbsCertificate = TBSCert}, self) ->
    #'OTPTBSCertificate'{issuer = Issuer, serialNumber = SerialNr} = TBSCert,
    {ok, {SerialNr, normalize_general_name(Issuer)}}.

cert_auth_key_id(#'AuthorityKeyIdentifier'{authorityCertIssuer = asn1_NOVALUE}) ->
    {error, issuer_not_found};
cert_auth_key_id(#'AuthorityKeyIdentifier'{authorityCertIssuer = AuthCertIssuer, authorityCertSerialNumber = Nr}) ->
    {ok, {Nr, decode_general_name(AuthCertIssuer)}}.

decode_general_name([{directoryName, Issuer}]) -> normalize_general_name(Issuer);
decode_general_name([{_, Issuer}]) -> Issuer.

%% public_key:pkix_subject_id/1.
pkix_subject_id(#'OTPCertificate'{tbsCertificate = TBSCert}) ->
    #'OTPTBSCertificate'{subject = Subject, serialNumber = SerialNr} = TBSCert,
    {SerialNr, normalize_general_name(Subject)};
pkix_subject_id(Cert) when is_binary(Cert) ->
    pkix_subject_id(public_key:pkix_decode_cert(Cert, otp)).

%%--------------------------------------------------------------------------------------------------------------------
%% CRLs and distribution points
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:pkix_crl_issuer/1.
pkix_crl_issuer(CRL) when is_binary(CRL) ->
    pkix_crl_issuer(public_key:der_decode('CertificateList', CRL));
pkix_crl_issuer(#'CertificateList'{tbsCertList = TBSCRL}) ->
    transform(TBSCRL#'TBSCertList'.issuer).

%% public_key:pkix_crl_verify/2: a CRL's signature checked with its issuer certificate's key.
pkix_crl_verify(CRL, Cert) when is_binary(CRL) ->
    pkix_crl_verify(public_key:der_decode('CertificateList', CRL), Cert);
pkix_crl_verify(CRL, Cert) when is_binary(Cert) ->
    pkix_crl_verify(CRL, public_key:pkix_decode_cert(Cert, otp));
pkix_crl_verify(#'CertificateList'{} = CRL, #'OTPCertificate'{tbsCertificate = TBSCert}) ->
    #'OTPSubjectPublicKeyInfo'{subjectPublicKey = PublicKey, algorithm = AlgInfo} =
        TBSCert#'OTPTBSCertificate'.subjectPublicKeyInfo,
    Params =
        case AlgInfo#'PublicKeyAlgorithm'.algorithm of
            ?'id-Ed25519' = AlgId -> {namedCurve, AlgId};
            ?'id-Ed448' = AlgId -> {namedCurve, AlgId};
            _ -> AlgInfo#'PublicKeyAlgorithm'.parameters
        end,
    verify_crl_signature(CRL, public_key:der_encode('CertificateList', CRL), PublicKey, Params).

%% Whether a CRL's signature verifies with a key; true without a CRL (pubkey_crl:verify_crl_signature/4).
verify_crl_signature(undefined, undefined, _, _) ->
    true;
verify_crl_signature(CRL, DerCRL, Key, KeyParams) ->
    #'CertificateList'{signatureAlgorithm = {_, SigAlg, _}, signature = Signature} = CRL,
    {DigestType, _} = public_key:pkix_sign_types(SigAlg),
    PlainText = clause_public_key_keys:first_element(DerCRL),
    case Key of
        #'RSAPublicKey'{} -> public_key:verify(PlainText, DigestType, Signature, Key);
        _ -> public_key:verify(PlainText, DigestType, Signature, {Key, KeyParams})
    end.

%% public_key:pkix_dist_point/1: the distribution point of the CRLs of a certificate's issuer.
pkix_dist_point(OtpCert) when is_binary(OtpCert) ->
    pkix_dist_point(public_key:pkix_decode_cert(OtpCert, otp));
pkix_dist_point(#'OTPCertificate'{tbsCertificate = TBSCert}) ->
    Issuer = public_key:pkix_normalize_name(TBSCert#'OTPTBSCertificate'.issuer),
    Extensions = extensions_list(TBSCert#'OTPTBSCertificate'.extensions),
    AltNames =
        case select_extension(?'id-ce-issuerAltName', Extensions) of
            undefined -> [];
            #'Extension'{extnValue = Value} -> Value
        end,
    #'DistributionPoint'{
        cRLIssuer = asn1_NOVALUE,
        reasons = asn1_NOVALUE,
        distributionPoint = {fullName, [{directoryName, Issuer} | AltNames]}
    }.

%% public_key:pkix_dist_points/1: a certificate's distribution points, in reverse order as OTP gives them.
pkix_dist_points(OtpCert) when is_binary(OtpCert) ->
    pkix_dist_points(public_key:pkix_decode_cert(OtpCert, otp));
pkix_dist_points(OtpCert) ->
    lists:foldl(fun(Point, Acc) -> [transform(Point) | Acc] end, [], distribution_points(OtpCert)).

%% The values of a certificate's CRL distribution points extension; [] without one.
distribution_points(#'OTPCertificate'{tbsCertificate = TBSCert}) ->
    case select_extension(?'id-ce-cRLDistributionPoints', extensions_list(TBSCert#'OTPTBSCertificate'.extensions)) of
        undefined -> [];
        #'Extension'{extnValue = Value} -> Value
    end.

%% public_key:pkix_match_dist_point/2: whether a CRL's issuing distribution point matches a distribution point.
pkix_match_dist_point(CRL, DistPoint) when is_binary(CRL) ->
    pkix_match_dist_point(public_key:der_decode('CertificateList', CRL), DistPoint);
pkix_match_dist_point(#'CertificateList'{}, #'DistributionPoint'{distributionPoint = asn1_NOVALUE}) ->
    true;
pkix_match_dist_point(
    #'CertificateList'{tbsCertList = #'TBSCertList'{crlExtensions = Extensions}},
    #'DistributionPoint'{distributionPoint = {fullName, DPs}}
) ->
    case select_extension(?'id-ce-issuingDistributionPoint', Extensions) of
        undefined ->
            true;
        #'Extension'{extnValue = IDPValue} ->
            IDP = public_key:der_decode('IssuingDistributionPoint', IDPValue),
            #'IssuingDistributionPoint'{distributionPoint = {fullName, IDPs}} = transform(IDP),
            match_one(IDPs, DPs)
    end.

%% Whether any name of the first list matches a name of its type in the second (pubkey_crl:match_one/2).
match_one([], _) ->
    false;
match_one([{Type, Name} | Names], CandidateNames) ->
    case [NameName || {NameType, NameName} <- CandidateNames, NameType == Type] of
        [] -> false;
        Candidates -> match_name(Type, Name, Candidates) orelse match_one(Names, CandidateNames)
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Extensions
%%--------------------------------------------------------------------------------------------------------------------

%% A certificate's extensions; [] for none.
extensions_list(asn1_NOVALUE) -> [];
extensions_list(Extensions) -> Extensions.

%% The first extension of an id; a CRL distribution points value still encoded is decoded. undefined for none.
select_extension(_, asn1_NOVALUE) ->
    undefined;
select_extension(_, []) ->
    undefined;
select_extension(Id, [#'Extension'{extnID = ?'id-ce-cRLDistributionPoints' = Id, extnValue = Value} = Ext | _]) when
    is_binary(Value)
->
    Ext#'Extension'{extnValue = public_key:der_decode('CRLDistributionPoints', Value)};
select_extension(Id, [#'Extension'{extnID = Id} = Extension | _]) ->
    Extension;
select_extension(Id, [_ | Extensions]) ->
    select_extension(Id, Extensions).

%%--------------------------------------------------------------------------------------------------------------------
%% Name matching
%%--------------------------------------------------------------------------------------------------------------------

%% Whether a name matches any of names by the rules of its type (pubkey_cert:match_name/3).
match_name(rfc822Name, Name, [PermittedName | Rest]) ->
    match_name(fun is_valid_host_or_domain/2, Name, PermittedName, Rest);
match_name(directoryName, DirName, [PermittedName | Rest]) ->
    match_name(fun is_rdn_sequence/2, DirName, PermittedName, Rest);
match_name(uniformResourceIdentifier, URI, [PermittedName | Rest]) ->
    case uri_string:normalize(URI, [return_map]) of
        #{host := Host} ->
            PN =
                case uri_string:normalize(PermittedName, [return_map]) of
                    #{host := PNhost} -> PNhost;
                    _ -> PermittedName
                end,
            match_name(fun is_valid_host_or_domain/2, Host, PN, Rest);
        _ ->
            false
    end;
match_name(emailAddress, Name, [PermittedName | Rest]) ->
    Fun = fun(Email, PermittedEmail) ->
        is_valid_email_address(Email, PermittedEmail, string:tokens(PermittedEmail, "@"))
    end,
    match_name(Fun, Name, PermittedName, Rest);
match_name(dNSName, Name, [PermittedName | Rest]) ->
    match_name(fun is_dns_name/2, Name, PermittedName, Rest);
match_name(x400Address, OrAddress, [PermittedAddr | Rest]) ->
    match_name(fun is_or_address/2, OrAddress, PermittedAddr, Rest);
match_name(ipAdress, IP, [PermittedIP | Rest]) ->
    match_name(fun is_ip/2, IP, PermittedIP, Rest).

match_name(Fun, Name, PermittedName, []) ->
    Fun(Name, PermittedName);
match_name(Fun, Name, PermittedName, [Head | Tail]) ->
    Fun(Name, PermittedName) orelse match_name(Fun, Name, Head, Tail).

is_rdn_sequence({rdnSequence, []}, {rdnSequence, [none]}) -> true;
is_rdn_sequence({rdnSequence, DirName}, {rdnSequence, Permitted}) -> is_dir_name(DirName, Permitted, false).

%% A domain name within a permitted domain ('.example.com' for its subdomains) or equal to a permitted host.
is_dns_name(Domain, [$. | Domain]) ->
    true;
is_dns_name(Name1, [$. | _] = Name2) ->
    is_suffix(Name2, Name1);
is_dns_name(Name1, Name2) ->
    StrLen1 = string:len(Name1),
    StrLen2 = string:len(Name2),
    if
        StrLen1 > StrLen2 -> is_suffix([$. | Name2], Name1);
        StrLen1 == StrLen2 -> string:casefold(Name1) == string:casefold(Name2);
        true -> false
    end.

%% An IPv4 or IPv6 address within a permitted address and mask.
is_ip(IP, PermittedAndMask) when length(IP) * 2 == length(PermittedAndMask), length(IP) == 4 orelse length(IP) == 16 ->
    {Permitted, Mask} = lists:split(length(IP), PermittedAndMask),
    lists:all(fun({C, P, M}) -> (C band M) == P end, lists:zip3(IP, Permitted, Mask));
is_ip(_, _) ->
    false.

is_valid_host_or_domain([], _) ->
    false;
is_valid_host_or_domain(Candidate, [$. | _] = Permitted) ->
    is_suffix(Permitted, Candidate);
is_valid_host_or_domain(Candidate, Permitted) ->
    case string:tokens(Candidate, "@") of
        [CandidateHost] -> string:equal(CandidateHost, Permitted, true);
        [_, CandidateHost] -> string:equal(CandidateHost, Permitted, true)
    end.

is_valid_email_address(Candidate, [$. | Permitted], [_]) ->
    is_suffix(Permitted, Candidate);
is_valid_email_address(Candidate, PermittedHost, [_]) ->
    [_, CandidateHost] = string:tokens(Candidate, "@"),
    string:equal(CandidateHost, PermittedHost, true);
is_valid_email_address(Candidate, Permitted, [_, _]) ->
    string:equal(Candidate, Permitted, true).

is_suffix(Suffix, Str) ->
    lists:suffix(string:casefold(Suffix), string:casefold(Str)).

is_or_address(Address, Candidate) ->
    is_double_quoted(Address) andalso is_double_quoted(Candidate) andalso string:equal(Address, Candidate, true).

is_double_quoted(["\"" | Tail]) ->
    is_double_quote(lists:last(Tail));
is_double_quoted("%22" ++ Tail) ->
    case lists:reverse(Tail) of
        [A, B, C | _] -> is_double_quote([C, B, A]);
        _ -> false
    end;
is_double_quoted(_) ->
    false.

is_double_quote("%22") -> true;
is_double_quote("\"") -> true;
is_double_quote(_) -> false.

%%--------------------------------------------------------------------------------------------------------------------
%% Hostnames
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:pkix_verify_hostname/3: reference ids against the certificate's subject alternative names, then the
%% DNS names the fqdn_fun takes from them.
pkix_verify_hostname(BinCert, ReferenceIDs, Options) when is_binary(BinCert) ->
    pkix_verify_hostname(public_key:pkix_decode_cert(BinCert, otp), ReferenceIDs, Options);
pkix_verify_hostname(Cert = #'OTPCertificate'{tbsCertificate = TbsCert}, ReferenceIDs0, Opts) ->
    MatchFun = proplists:get_value(match_fun, Opts, undefined),
    FailCB = proplists:get_value(fail_callback, Opts, fun(_Cert) -> false end),
    FqdnFun = proplists:get_value(fqdn_fun, Opts, fun extract_fqdn_default/1),
    ReferenceIDs = [{T, to_string(V)} || {T, V} <- ReferenceIDs0],
    case presented_ids(TbsCert) of
        [] ->
            false;
        PresentedIDs ->
            match_loop(ReferenceIDs, PresentedIDs, MatchFun, FailCB, Cert) orelse
                begin
                    DNSReferenceIDs = [{dns_id, X} || X <- fqdns(ReferenceIDs, FqdnFun)],
                    match_loop(DNSReferenceIDs, PresentedIDs, MatchFun, FailCB, Cert)
                end
    end.

%% The subject alternative names; [] without them or when the extensions are not a list.
presented_ids(TbsCert) ->
    try lists:keyfind(?'id-ce-subjectAltName', #'Extension'.extnID, TbsCert#'OTPTBSCertificate'.extensions) of
        #'Extension'{extnValue = ExtVals} -> [{T, to_string(V)} || {T, V} <- ExtVals];
        false -> []
    catch
        _:_ -> []
    end.

%% public_key:pkix_verify_hostname_match_fun/1: HTTPS wildcard matching of DNS names.
pkix_verify_hostname_match_fun(https) ->
    fun
        ({dns_id, FQDN = [_ | _]}, {dNSName, Name = [_ | _]}) -> match_wildcard(FQDN, Name);
        (_, _) -> default
    end.

extract_fqdn_default({dns_id, S}) ->
    S;
extract_fqdn_default({uri_id, URI}) ->
    #{scheme := "https", host := Host} = uri_string:normalize(URI, [return_map]),
    Host.

%% The host names the fqdn fun takes from reference ids: non-empty strings that are not IP addresses.
fqdns(ReferenceIDs, FqdnFun) ->
    [
        E
     || E0 <- ReferenceIDs,
        E <- [fqdn(FqdnFun, E0)],
        is_list(E),
        E =/= "",
        {error, einval} == inet:parse_address(E)
    ].

fqdn(FqdnFun, Id) ->
    try FqdnFun(Id) of
        default -> extract_fqdn_default(Id);
        Other -> Other
    catch
        _:_ -> undefined
    end.

%% Whether any reference id matches any presented id; each failed comparison asks the fail callback.
match_loop(Refs0, Pres0, undefined, FailCB, Cert) ->
    Pres = lists:map(fun to_lower_ascii/1, Pres0),
    Refs = lists:map(fun to_lower_ascii/1, Refs0),
    lists:any(fun(R) -> lists:any(fun(P) -> match_default(R, P) orelse FailCB(Cert) end, Pres) end, Refs);
match_loop(Refs, Pres, MatchFun, FailCB, Cert) ->
    lists:any(
        fun(R) ->
            lists:any(
                fun(P) ->
                    (case MatchFun(R, P) of
                        default -> match_default(R, P);
                        Bool -> Bool
                    end) orelse FailCB(Cert)
                end,
                Pres
            )
        end,
        Refs
    ).

match_default(Ref, Pres) ->
    match_default0(to_lower_ascii(Ref), to_lower_ascii(Pres)).

match_default0({dns_id, R}, {dNSName, P}) ->
    R == P;
match_default0({uri_id, R}, {uniformResourceIdentifier, P}) ->
    R == P;
match_default0({ip, R}, {iPAddress, P}) when length(P) == 4 ->
    try
        list_to_tuple(P) ==
            if
                tuple_size(R) == 4 -> R;
                is_list(R) -> ok(inet:parse_ipv4strict_address(R))
            end
    catch
        _:_ -> false
    end;
match_default0({ip, R}, {iPAddress, P}) when length(P) == 16 ->
    try
        ipv6_tuple(P, []) ==
            if
                tuple_size(R) == 8 -> R;
                is_list(R) -> ok(inet:parse_ipv6strict_address(R))
            end
    catch
        _:_ -> false
    end;
match_default0({srv_id, R}, {srvName, P}) ->
    R == P;
match_default0({srv_id, R}, {?SRV_NAME_OID, P}) ->
    R == P;
match_default0(_, _) ->
    false.

ok({ok, X}) -> X.

%% Sixteen bytes as the eight words of an IPv6 address.
ipv6_tuple([A, B | T], Acc) -> ipv6_tuple(T, [(A bsl 8) bor B | Acc]);
ipv6_tuple([], Acc) -> list_to_tuple(lists:reverse(Acc)).

%% A host name against a name whose first label may hold one wildcard; the other labels equal.
match_wildcard(FQDN, Name) ->
    [F1 | Fs] = string:split(to_lower_ascii(FQDN), "."),
    [N1 | Ns] = string:split(to_lower_ascii(Name), "."),
    match_wild(F1, N1) andalso Fs == Ns.

match_wild(A, [$* | B]) -> match_wild_sfx(lists:reverse(A), lists:reverse(B));
match_wild([C | A], [C | B]) -> match_wild(A, B);
match_wild([], []) -> true;
match_wild(_, _) -> false.

%% The parts after the wildcard compared from their ends; neither may hold another wildcard.
match_wild_sfx([$* | _], _) -> false;
match_wild_sfx(_, [$* | _]) -> false;
match_wild_sfx([A | Ar], [A | Br]) -> match_wild_sfx(Ar, Br);
match_wild_sfx(Ar, []) -> not lists:member($*, Ar);
match_wild_sfx(_, _) -> false.

to_lower_ascii({ip, _} = X) -> X;
to_lower_ascii({iPAddress, _} = X) -> X;
to_lower_ascii(S) when is_list(S) -> lists:map(fun to_lower_ascii/1, S);
to_lower_ascii({T, S}) -> {T, to_lower_ascii(S)};
to_lower_ascii(C) when $A =< C, C =< $Z -> C + ($a - $A);
to_lower_ascii(C) -> C.

to_string(S) when is_list(S) -> S;
to_string(B) when is_binary(B) -> binary_to_list(B);
to_string(X) -> X.

%%--------------------------------------------------------------------------------------------------------------------
%% Helpers
%%--------------------------------------------------------------------------------------------------------------------

%% pubkey_cert_records:transform/2 in the decode direction.
transform(Term) -> clause_public_key_codec:transform(Term, decode).
