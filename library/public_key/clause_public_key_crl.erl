%% clause_public_key_crl: public_key:pkix_crls_validate/3 (plan 21 step LIB80D3,
%% docs/libraries/public_key.md#crl-validation): RFC 5280 6.3 CRL validation as OTP's public_key and pubkey_crl do
%% it: CRLs grouped by distribution point and combined with their delta CRLs, freshness and the update_crl fun,
%% issuer and scope, reason masks, the CRL signer's path through issuer_fun, and the revocation status.
-module(clause_public_key_crl).
-export([pkix_crls_validate/3]).

-include("include/public_key.hrl").

%% The state of revocation checking, as OTP's #revoke_state{}.
-record(revoke_state, {reasons_mask, cert_status, interim_reasons_mask, valid_ext, details}).
%% The verify fun state of a CRL signer's path: the other distribution points' CRLs and the issuing distribution
%% point.
-record(userstate, {dpcrls, idp}).

%% public_key:pkix_crls_validate/3: DPs with their CRLs (as {DP, {Der, CRL}} or already combined with delta CRLs).
pkix_crls_validate(OtpCert, [{_, _, _} | _] = DPAndCRLs, Options) ->
    crls_validate(OtpCert, DPAndCRLs, DPAndCRLs, Options, init_revoke_state());
pkix_crls_validate(OtpCert, DPAndCRLs0, Options) ->
    DPAndCRLs = sort_dp_crls(DPAndCRLs0, update_crl(Options)),
    crls_validate(OtpCert, DPAndCRLs, DPAndCRLs, Options, init_revoke_state()).

update_crl(Options) ->
    proplists:get_value(update_crl, Options, fun(_, CurrCRL) -> CurrCRL end).

init_revoke_state() ->
    #revoke_state{reasons_mask = sets:new(), interim_reasons_mask = sets:new(), cert_status = unrevoked, details = []}.

crls_validate(_, [], _, Options, #revoke_state{details = Details}) ->
    undetermined(Options, format_details(Details));
crls_validate(OtpCert, [{DP, CRL, DeltaCRL} | Rest], All, Options, RevokedState) ->
    case fresh_crl(DP, CRL, update_crl(Options)) of
        {fresh, CRL} ->
            dp_validate(OtpCert, [{DP, CRL, DeltaCRL} | Rest], All, Options, RevokedState);
        {fresh, NewCRL} ->
            NewAll = [{DP, NewCRL, DeltaCRL} | All -- [{DP, CRL, DeltaCRL}]],
            dp_validate(OtpCert, [{DP, NewCRL, DeltaCRL} | Rest], NewAll, Options, RevokedState);
        no_fresh_crl ->
            crls_validate(OtpCert, Rest, All, Options, RevokedState)
    end.

dp_validate(OtpCert, [{DP, CRL, DeltaCRL} | Rest], All, Options, RevokedState0) ->
    OtherDPCRLs = All -- [{DP, CRL, DeltaCRL}],
    case validate(OtpCert, OtherDPCRLs, DP, CRL, DeltaCRL, Options, RevokedState0) of
        {undetermined, unrevoked, #revoke_state{details = Details}} when Rest == [] ->
            undetermined(Options, Details);
        {undetermined, unrevoked, RevokedState} when Rest =/= [] ->
            crls_validate(OtpCert, Rest, All, Options, RevokedState);
        {finished, unrevoked} ->
            valid;
        {finished, Status} ->
            {bad_cert, Status}
    end.

undetermined(Options, Details) ->
    case proplists:get_value(undetermined_details, Options, false) of
        false -> {bad_cert, revocation_status_undetermined};
        true -> {bad_cert, {revocation_status_undetermined, {bad_crls, Details}}}
    end.

format_details([]) -> no_relevant_crls;
format_details(Details) -> Details.

%%--------------------------------------------------------------------------------------------------------------------
%% Grouping and freshness
%%--------------------------------------------------------------------------------------------------------------------

%% The CRLs of each distribution point, each with the delta CRL that combines with it.
sort_dp_crls(DpsAndCrls, FreshCB) ->
    Grouped = lists:foldl(
        fun({DP, CRL}, M) ->
            case M of
                #{DP := CRLs} -> M#{DP := [CRL | CRLs]};
                _ -> M#{DP => [CRL]}
            end
        end,
        #{},
        DpsAndCrls
    ),
    lists:foldl(
        fun({DP, AllCRLs}, Acc) ->
            {DeltaCRLs, CRLs} = lists:partition(fun({_, CRL}) -> is_delta_crl(CRL) end, AllCRLs),
            combine(CRLs, DeltaCRLs, DP, FreshCB, []) ++ Acc
        end,
        [],
        maps:to_list(Grouped)
    ).

combine([], _, _, _, Acc) ->
    Acc;
combine([{_, CRL} = Entry | CRLs], DeltaCRLs, DP, FreshCB, Acc) ->
    case fresh_crl(DP, delta_of(CRL, DeltaCRLs), FreshCB) of
        no_fresh_crl -> combine(CRLs, DeltaCRLs, DP, FreshCB, [{DP, Entry, {undefined, undefined}} | Acc]);
        {fresh, NewDeltaCRL} -> combine(CRLs, DeltaCRLs, DP, FreshCB, [{DP, Entry, NewDeltaCRL} | Acc])
    end.

%% The delta CRL that combines with a CRL; of several, the one updated last.
delta_of(CRL, DeltaCRLs) ->
    case [Delta || {_, DeltaCRL} = Delta <- DeltaCRLs, combines(CRL, DeltaCRL)] of
        [] ->
            {undefined, undefined};
        [Delta] ->
            Delta;
        [First | Others] ->
            Later = fun(
                {_, #'CertificateList'{tbsCertList = FirstTBS}} = CRL1,
                {_, #'CertificateList'{tbsCertList = SecondTBS}} = CRL2
            ) ->
                case seconds(FirstTBS#'TBSCertList'.thisUpdate) > seconds(SecondTBS#'TBSCertList'.thisUpdate) of
                    true -> CRL1;
                    false -> CRL2
                end
            end,
            lists:foldl(Later, First, Others)
    end.

%% A CRL past its next update replaced by the update_crl fun's, while it gives a new one.
fresh_crl(_, {undefined, undefined}, _) ->
    no_fresh_crl;
fresh_crl(DP, {_, #'CertificateList'{tbsCertList = TBSCRL}} = CRL, CallBack) ->
    Now = calendar:datetime_to_gregorian_seconds(calendar:universal_time()),
    case Now >= seconds(TBSCRL#'TBSCertList'.nextUpdate) of
        true ->
            case CallBack(DP, CRL) of
                CRL -> no_fresh_crl;
                NewCRL -> fresh_crl(DP, NewCRL, CallBack)
            end;
        false ->
            {fresh, CRL}
    end.

is_delta_crl(#'CertificateList'{tbsCertList = TBSCRL}) ->
    select_extension(?'id-ce-deltaCRLIndicator', TBSCRL#'TBSCertList'.crlExtensions) =/= undefined.

%% Whether a delta CRL extends a CRL: numbers in order, the same issuer and scope.
combines(CRL, DeltaCRL) ->
    check_crl_num(CRL, DeltaCRL) andalso check_delta_issuer_and_scope(CRL, DeltaCRL).

check_crl_num(#'CertificateList'{tbsCertList = TBSCRL}, #'CertificateList'{tbsCertList = TBSDeltaCRL}) ->
    Extensions = TBSCRL#'TBSCertList'.crlExtensions,
    DeltaExtensions = TBSDeltaCRL#'TBSCertList'.crlExtensions,
    try
        CRLNum = assert_extension_value(?'id-ce-cRLNumber', 'CRLNumber', Extensions),
        DeltaBaseNum = assert_extension_value(?'id-ce-deltaCRLIndicator', 'CRLNumber', DeltaExtensions),
        DeltaCRLNum = assert_extension_value(?'id-ce-cRLNumber', 'CRLNumber', DeltaExtensions),
        (CRLNum >= DeltaBaseNum) andalso (CRLNum < DeltaCRLNum)
    catch
        throw:no_extension_present -> false
    end;
check_crl_num(_, _) ->
    false.

check_delta_issuer_and_scope(_, undefined) ->
    true;
check_delta_issuer_and_scope(#'CertificateList'{tbsCertList = TBSCRL}, #'CertificateList'{tbsCertList = TBSDelta}) ->
    clause_public_key_cert:is_issuer(TBSCRL#'TBSCertList'.issuer, TBSDelta#'TBSCertList'.issuer) andalso
        issuing_distribution_point(TBSCRL#'TBSCertList'.crlExtensions) =:=
            issuing_distribution_point(TBSDelta#'TBSCertList'.crlExtensions) andalso
        authority_key_identifier(TBSCRL#'TBSCertList'.crlExtensions) =:=
            authority_key_identifier(TBSDelta#'TBSCertList'.crlExtensions).

%%--------------------------------------------------------------------------------------------------------------------
%% One distribution point's CRL
%%--------------------------------------------------------------------------------------------------------------------

%% The revocation status after one CRL: {undetermined, Status, State} or {finished, Status}.
validate(OtpCert, OtherDPCRLs, DP, {DerCRL, CRL}, {DerDeltaCRL, DeltaCRL}, Options, RevokedState0) ->
    RevokedState =
        case verify_crl(OtpCert, DP, CRL, DerCRL, DeltaCRL, DerDeltaCRL, OtherDPCRLs, Options, RevokedState0) of
            {valid, Revoked, DeltaRevoked, RevokedState1, IDP} ->
                #'OTPCertificate'{tbsCertificate = TBSCert} = OtpCert,
                #'OTPTBSCertificate'{serialNumber = SerialNumber, issuer = CertIssuer} = TBSCert,
                CRLIssuer = (CRL#'CertificateList'.tbsCertList)#'TBSCertList'.issuer,
                AltNames =
                    case select_extension(?'id-ce-subjectAltName', TBSCert#'OTPTBSCertificate'.extensions) of
                        #'Extension'{extnValue = Value} -> Value;
                        _ -> []
                    end,
                revoked_status(
                    DP,
                    IDP,
                    {directoryName, CRLIssuer},
                    [{directoryName, CertIssuer} | AltNames],
                    SerialNumber,
                    Revoked,
                    DeltaRevoked,
                    RevokedState1
                );
            {invalid, Revoked} ->
                Revoked
        end,
    crl_status(RevokedState).

crl_status({skip, #revoke_state{cert_status = Status} = RevokedState}) ->
    {undetermined, status(Status), RevokedState};
crl_status(#revoke_state{cert_status = unrevoked = Status, valid_ext = false} = RevokedState) ->
    {undetermined, Status, RevokedState};
crl_status(#revoke_state{cert_status = Status, valid_ext = false}) ->
    {finished, status(Status)};
crl_status(#revoke_state{reasons_mask = Mask, cert_status = Status, valid_ext = true} = RevokedState) ->
    case is_all_reasons(Mask) of
        true -> {finished, status(Status)};
        false when Status == unrevoked -> {undetermined, Status, RevokedState};
        _ -> {finished, status(Status)}
    end.

status(unrevoked) -> unrevoked;
status(Reason) -> {revoked, Reason}.

%% Whether the mask covers all reasons; unspecified may be missing, as the conformance tests need.
is_all_reasons(Mask) ->
    AllReasons = all_reasons(),
    sets:is_subset(AllReasons, Mask) orelse sets:is_subset(sets:del_element(unspecified, AllReasons), Mask).

all_reasons() ->
    sets:from_list([
        unspecified,
        keyCompromise,
        cACompromise,
        affiliationChanged,
        superseded,
        cessationOfOperation,
        certificateHold,
        privilegeWithdrawn,
        aACompromise
    ]).

%% The CRL checked: extensions, issuer and scope, the issuer fun's path, reason masks and signatures.
verify_crl(OtpCert, DP, CRL, DerCRL, DeltaCRL, DerDeltaCRL, OtherDPCRLs, Options, State0) ->
    #'CertificateList'{tbsCertList = #'TBSCertList'{crlExtensions = Extensions, revokedCertificates = TmpRevoked}} =
        CRL,
    Revoked = revoked(TmpRevoked),
    IDP = issuing_distribution_point(Extensions),
    DeltaRevoked =
        case DeltaCRL of
            undefined -> [];
            #'CertificateList'{tbsCertList = #'TBSCertList'{revokedCertificates = DR}} -> revoked(DR)
        end,
    ValidExt = verify_extensions(Extensions) andalso verify_extensions(Revoked),
    RevokedState = State0#revoke_state{interim_reasons_mask = interim_reasons_mask(DP, IDP), valid_ext = ValidExt},
    {Fun, AdditionalArgs} = IssuerFun = proplists:get_value(issuer_fun, Options),
    Verify = fun(TrustedOtpCert, Path) ->
        verify_mask_and_signatures(
            Revoked,
            DeltaRevoked,
            RevokedState,
            {CRL, DerCRL, DeltaCRL, DerDeltaCRL},
            {IssuerFun, TrustedOtpCert, Path},
            OtherDPCRLs,
            IDP
        )
    end,
    NoChain = {{bad_crl, no_issuer_cert_chain}, CRL},
    try verify_issuer_and_scope(OtpCert, DP, IDP, CRL) of
        {ok, Issuer} ->
            case Fun(DP, CRL, Issuer, AdditionalArgs) of
                {ok, TrustedOtpCert, Path} -> Verify(TrustedOtpCert, Path);
                _ -> {invalid, RevokedState#revoke_state{details = [NoChain | RevokedState#revoke_state.details]}}
            end;
        {error, issuer_not_found} ->
            case Fun(DP, CRL, issuer_not_found, AdditionalArgs) of
                {ok, TrustedOtpCert, Path} -> Verify(TrustedOtpCert, Path);
                _ -> {invalid, {skip, State0#revoke_state{details = [NoChain | State0#revoke_state.details]}}}
            end
    catch
        throw:{bad_crl, invalid_issuer} = Reason ->
            {invalid, {skip, RevokedState#revoke_state{details = [{Reason, CRL} | RevokedState#revoke_state.details]}}};
        throw:Reason ->
            {invalid, RevokedState#revoke_state{details = [{Reason, CRL} | RevokedState#revoke_state.details]}}
    end.

verify_mask_and_signatures(Revoked, DeltaRevoked, RevokedState, CRLs, Signer, OtherDPCRLs, IDP) ->
    {CRL, _, _, _} = CRLs,
    ReasonsMask = sets:union(RevokedState#revoke_state.reasons_mask, RevokedState#revoke_state.interim_reasons_mask),
    Details = RevokedState#revoke_state.details,
    try
        verify_interim_reasons_mask(RevokedState),
        true = verify_crl_signatures(CRLs, Signer, OtherDPCRLs, IDP),
        {valid, Revoked, DeltaRevoked, RevokedState#revoke_state{reasons_mask = ReasonsMask}, IDP}
    catch
        throw:Reason ->
            {invalid, RevokedState#revoke_state{details = [{Reason, CRL} | Details]}};
        error:{badmatch, _} ->
            {invalid, RevokedState#revoke_state{details = [{{bad_crl, invalid_signature}, CRL} | Details]}}
    end.

%% The CRL signer's path validated (its verify fun checks the cRLSign bit and the signer's own revocation), then the
%% CRL's and delta CRL's signatures with its key; false when either fails.
verify_crl_signatures({CRL, DerCRL, DeltaCRL, DerDeltaCRL}, {IssuerFun, TrustedOtpCert, Path}, OtherDPCRLs, IDP) ->
    try
        VerifyFunAndState = {
            fun
                (_, {bad_cert, _} = Reason, _) ->
                    {fail, Reason};
                (_, {extension, _}, UserState) ->
                    {unknown, UserState};
                (_, valid, UserState) ->
                    {valid, UserState};
                (Cert, valid_peer, UserState) ->
                    case verify_crl_keybit(Cert, cRLSign) of
                        true -> handle_crlsigner(Cert, IssuerFun, UserState);
                        false -> {fail, crl_sign_bit_not_set}
                    end
            end,
            #userstate{dpcrls = OtherDPCRLs, idp = IDP}
        },
        {ok, {{_, Key, KeyParams}, _}} =
            public_key:pkix_path_validation(TrustedOtpCert, Path, [{verify_fun, VerifyFunAndState}]),
        true = clause_public_key_cert:verify_crl_signature(CRL, DerCRL, Key, KeyParams),
        true = clause_public_key_cert:verify_crl_signature(DeltaCRL, DerDeltaCRL, Key, KeyParams)
    catch
        error:{badmatch, _} -> false
    end.

%% A CRL signer that cannot sign certificates, of a direct CRL and not self-signed, needs its own revocation status.
handle_crlsigner(OtpCert, IssuerFun, #userstate{idp = IDP} = UserState) ->
    case verify_crl_keybit(OtpCert, keyCertSign) of
        true ->
            {valid, UserState};
        false ->
            case not is_indirect_crl(IDP) andalso not public_key:pkix_is_self_signed(OtpCert) of
                true -> validate_crl_signing_cert(OtpCert, IssuerFun, UserState);
                false -> {valid, UserState}
            end
    end.

validate_crl_signing_cert(_, _, #userstate{dpcrls = []} = UserState) ->
    {valid, UserState};
validate_crl_signing_cert(OtpCert, IssuerFun, #userstate{dpcrls = CRLInfo} = UserState) ->
    case public_key:pkix_crls_validate(OtpCert, CRLInfo, [{issuer_fun, IssuerFun}]) of
        valid -> {valid, UserState};
        Reason -> {fail, Reason}
    end.

%% Whether a key usage extension allows a bit; true without one.
verify_crl_keybit(#'OTPCertificate'{tbsCertificate = TBS}, Bit) ->
    case select_extension(?'id-ce-keyUsage', TBS#'OTPTBSCertificate'.extensions) of
        #'Extension'{extnID = ?'id-ce-keyUsage', extnValue = KeyUse} -> lists:member(Bit, KeyUse);
        _ -> true
    end.

revoked(asn1_NOVALUE) -> [];
revoked(Revoked) -> Revoked.

%% Only the critical extensions a CRL may have.
verify_extensions([#'TBSCertList_revokedCertificates_SEQOF'{crlEntryExtensions = Ext} | Rest]) ->
    verify_extensions(clause_public_key_cert:extensions_list(Ext)) andalso verify_extensions(Rest);
verify_extensions([]) ->
    true;
verify_extensions(asn1_NOVALUE) ->
    true;
verify_extensions([#'Extension'{critical = true, extnID = Id} | Rest]) ->
    Known = [
        ?'id-ce-authorityKeyIdentifier',
        ?'id-ce-issuerAltName',
        ?'id-ce-cRLNumber',
        ?'id-ce-certificateIssuer',
        ?'id-ce-deltaCRLIndicator',
        ?'id-ce-issuingDistributionPoint',
        ?'id-ce-freshestCRL'
    ],
    lists:member(Id, Known) andalso verify_extensions(Rest);
verify_extensions([_ | Rest]) ->
    verify_extensions(Rest).

%% The reasons this CRL covers for the distribution point.
interim_reasons_mask(#'DistributionPoint'{reasons = asn1_NOVALUE}, #'IssuingDistributionPoint'{
    onlySomeReasons = asn1_NOVALUE
}) ->
    all_reasons();
interim_reasons_mask(#'DistributionPoint'{reasons = asn1_NOVALUE}, undefined) ->
    all_reasons();
interim_reasons_mask(#'DistributionPoint'{reasons = asn1_NOVALUE}, #'IssuingDistributionPoint'{
    onlySomeReasons = IDPReasons
}) ->
    sets:from_list(IDPReasons);
interim_reasons_mask(#'DistributionPoint'{reasons = DPReasons}, #'IssuingDistributionPoint'{
    onlySomeReasons = asn1_NOVALUE
}) ->
    sets:from_list(DPReasons);
interim_reasons_mask(#'DistributionPoint'{reasons = DPReasons}, undefined) ->
    sets:from_list(DPReasons);
interim_reasons_mask(#'DistributionPoint'{reasons = DPReasons}, #'IssuingDistributionPoint'{
    onlySomeReasons = IDPReasons
}) ->
    sets:intersection(sets:from_list(DPReasons), sets:from_list(IDPReasons)).

%% The CRL must add a reason the mask lacks.
verify_interim_reasons_mask(#revoke_state{reasons_mask = Mask, interim_reasons_mask = IntMask}) ->
    case lists:any(fun(Element) -> not sets:is_element(Element, Mask) end, sets:to_list(IntMask)) of
        true -> ok;
        false -> throw({bad_crl, mask_error})
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Issuer and scope
%%--------------------------------------------------------------------------------------------------------------------

%% The CRL's issuer is the distribution point's (of an indirect CRL) or the certificate's; then its scope.
verify_issuer_and_scope(
    #'OTPCertificate'{tbsCertificate = TBSCert} = Cert,
    #'DistributionPoint'{cRLIssuer = DPIssuer} = DP,
    IDP,
    #'CertificateList'{tbsCertList = TBSCRL} = CRL
) when DPIssuer =/= asn1_NOVALUE ->
    CRLIssuer = transform(TBSCRL#'TBSCertList'.issuer),
    [{directoryName, Issuer}] = transform(DPIssuer),
    case clause_public_key_cert:is_issuer(Issuer, CRLIssuer) andalso is_indirect_crl(IDP) of
        true ->
            verify_scope(Cert, DP, IDP),
            crl_issuer_id(Cert, CRL);
        false ->
            Direct = DP#'DistributionPoint'{
                distributionPoint = [TBSCert#'OTPTBSCertificate'.issuer], cRLIssuer = asn1_NOVALUE
            },
            verify_issuer_and_scope(Cert, Direct, IDP, CRL)
    end;
verify_issuer_and_scope(#'OTPCertificate'{tbsCertificate = TBSCert} = Cert, DP, IDP, #'CertificateList'{
    tbsCertList = TBSCRL
}) ->
    case clause_public_key_cert:is_issuer(TBSCert#'OTPTBSCertificate'.issuer, transform(TBSCRL#'TBSCertList'.issuer)) of
        true ->
            verify_scope(Cert, DP, IDP),
            cert_issuer_id(Cert);
        false ->
            throw({bad_crl, invalid_issuer})
    end.

is_indirect_crl(#'IssuingDistributionPoint'{indirectCRL = Value}) -> Value;
is_indirect_crl(_) -> false.

%% The distribution point's names match the issuing distribution point's, and the certificate kind its booleans.
verify_scope(_, _, undefined) ->
    ok;
verify_scope(#'OTPCertificate'{tbsCertificate = TBSCert}, #'DistributionPoint'{cRLIssuer = DPIssuer} = DP, IDP) ->
    CertIssuer = TBSCert#'OTPTBSCertificate'.issuer,
    Names =
        case gen_names(DPIssuer) of
            [{directoryName, TNames}] -> TNames;
            Other -> Other
        end,
    DPName = dp_names(DP#'DistributionPoint'.distributionPoint, Names, CertIssuer),
    IDPName = dp_names(IDP#'IssuingDistributionPoint'.distributionPoint, Names, CertIssuer),
    case {DPName, Names} of
        {asn1_NOVALUE, asn1_NOVALUE} ->
            throw({bad_crl, scope_error});
        {asn1_NOVALUE, _} ->
            verify_dp_name(IDPName, Names);
        _ ->
            verify_dp_name(IDPName, DPName)
    end,
    verify_dp_bools(TBSCert, IDP).

dp_names(asn1_NOVALUE, _, _) ->
    asn1_NOVALUE;
dp_names({fullName, Name}, _, _) ->
    gen_names(Name);
dp_names({nameRelativeToCRLIssuer, Fragment}, asn1_NOVALUE, {rdnSequence, RDNs}) ->
    [{directoryName, {rdnSequence, RDNs ++ [lists:map(fun transform/1, Fragment)]}}];
dp_names({nameRelativeToCRLIssuer, Fragment}, {rdnSequence, RDNs}, _) ->
    [{directoryName, {rdnSequence, RDNs ++ [lists:map(fun transform/1, Fragment)]}}];
dp_names([{rdnSequence, _}] = Name0, _, _) ->
    [Name] = transform(Name0),
    [{directoryName, Name}].

gen_names(asn1_NOVALUE) -> asn1_NOVALUE;
gen_names(Names) -> [{NameType, transform(Name)} || {NameType, Name} <- Names].

verify_dp_name(asn1_NOVALUE, _) ->
    ok;
verify_dp_name(IDPNames, DPorIssuerNames) ->
    case match_one(DPorIssuerNames, IDPNames) of
        true -> ok;
        false -> throw({bad_crl, scope_error})
    end.

match_one([], _) ->
    false;
match_one([{Type, Name} | Names], CandidateNames) ->
    case [NameName || {NameType, NameName} <- CandidateNames, NameType == Type] of
        [] -> false;
        Candidates -> clause_public_key_cert:match_name(Type, Name, Candidates) orelse match_one(Names, CandidateNames)
    end.

%% The issuing distribution point's onlyContains booleans against the certificate's basic constraints.
verify_dp_bools(TBSCert, IDP) ->
    BasicConstraints = select_extension(?'id-ce-basicConstraints', TBSCert#'OTPTBSCertificate'.extensions),
    IsCA =
        case BasicConstraints of
            #'Extension'{extnValue = #'BasicConstraints'{cA = true}} -> true;
            _ -> false
        end,
    #'IssuingDistributionPoint'{
        onlyContainsUserCerts = OnlyUser, onlyContainsCACerts = OnlyCA, onlyContainsAttributeCerts = OnlyAttribute
    } = IDP,
    UserOk = not (IsCA andalso OnlyUser == true),
    CAOk = IsCA orelse OnlyCA =/= true,
    case UserOk andalso CAOk andalso not OnlyAttribute of
        true -> ok;
        _ -> throw({bad_crl, scope_error})
    end.

%% The CRL's issuer id: its authority key identifier's issuer and serial number, else the certificate's issuer's.
crl_issuer_id(Cert, #'CertificateList'{tbsCertList = TBSCRL}) ->
    case authority_key_identifier(clause_public_key_cert:extensions_list(TBSCRL#'TBSCertList'.crlExtensions)) of
        undefined ->
            cert_issuer_id(Cert);
        #'AuthorityKeyIdentifier'{authorityCertIssuer = asn1_NOVALUE, authorityCertSerialNumber = asn1_NOVALUE} ->
            cert_issuer_id(Cert);
        #'AuthorityKeyIdentifier'{authorityCertIssuer = Issuer, authorityCertSerialNumber = Nr} ->
            {ok, {Nr, Issuer}}
    end.

cert_issuer_id(#'OTPCertificate'{} = Cert) ->
    case public_key:pkix_is_self_signed(Cert) of
        true -> public_key:pkix_issuer_id(Cert, self);
        false -> public_key:pkix_issuer_id(Cert, other)
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Revocation status
%%--------------------------------------------------------------------------------------------------------------------

%% The certificate looked up in the delta CRL, then in the CRL unless found; removeFromCRL counts as unrevoked.
revoked_status(DP, IDP, CRLIssuer, Names, SerialNumber, Revoked, DeltaRevoked, RevokedState0) ->
    RevokedState1 = check_revoked(
        DP, IDP, default_issuer(CRLIssuer, DeltaRevoked), Names, SerialNumber, DeltaRevoked, RevokedState0
    ),
    RevokedState =
        case RevokedState1#revoke_state.cert_status of
            unrevoked ->
                check_revoked(DP, IDP, default_issuer(CRLIssuer, Revoked), Names, SerialNumber, Revoked, RevokedState1);
            _ ->
                RevokedState1
        end,
    case RevokedState#revoke_state.cert_status of
        removeFromCRL -> RevokedState#revoke_state{cert_status = unrevoked};
        _ -> RevokedState
    end.

default_issuer(_, []) ->
    undefined;
default_issuer(Default, [#'TBSCertList_revokedCertificates_SEQOF'{crlEntryExtensions = Extensions} | _]) ->
    case extension_value(?'id-ce-certificateIssuer', 'GeneralNames', Extensions) of
        undefined -> [transform(Default)];
        GeneralNames -> gen_names(GeneralNames)
    end.

check_revoked(_, _, _, _, _, [], State) ->
    State;
check_revoked(
    #'DistributionPoint'{cRLIssuer = DPIssuer} = DP,
    IDP,
    DefaultIssuer0,
    Names,
    SerialNr,
    [#'TBSCertList_revokedCertificates_SEQOF'{userCertificate = SerialNr, crlEntryExtensions = Extensions} | Rest],
    State
) ->
    Reason = revoked_reason(Extensions),
    case DPIssuer =/= asn1_NOVALUE andalso is_indirect_crl(IDP) of
        true ->
            case check_crl_issuer_extension(Names, Extensions, DefaultIssuer0) of
                {true, _} -> State#revoke_state{cert_status = Reason};
                {false, DefaultIssuer} -> check_revoked(DP, IDP, DefaultIssuer, Names, SerialNr, Rest, State)
            end;
        false ->
            State#revoke_state{cert_status = Reason}
    end;
check_revoked(
    DP,
    IDP,
    DefaultIssuer0,
    Names,
    SerialNr,
    [#'TBSCertList_revokedCertificates_SEQOF'{crlEntryExtensions = Extensions} | Rest],
    State
) ->
    DefaultIssuer =
        case extension_value(?'id-ce-certificateIssuer', 'GeneralNames', Extensions) of
            undefined -> DefaultIssuer0;
            GeneralNames -> gen_names(GeneralNames)
        end,
    check_revoked(DP, IDP, DefaultIssuer, Names, SerialNr, Rest, State).

check_crl_issuer_extension(Names, Extensions, Default0) ->
    case extension_value(?'id-ce-certificateIssuer', 'GeneralNames', Extensions) of
        undefined ->
            {match_one(Default0, Names), Default0};
        GeneralNames ->
            Default = gen_names(GeneralNames),
            {match_one(Default, Names), Default}
    end.

revoked_reason(Extensions) ->
    case extension_value(?'id-ce-cRLReasons', 'CRLReason', Extensions) of
        undefined -> unspecified;
        Value -> Value
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Helpers
%%--------------------------------------------------------------------------------------------------------------------

select_extension(Id, Extensions) -> clause_public_key_cert:select_extension(Id, Extensions).

%% An extension's value decoded by a type; undefined without the extension.
extension_value(Extension, ExtType, Extensions) ->
    case select_extension(Extension, Extensions) of
        #'Extension'{extnValue = Value} -> public_key:der_decode(ExtType, iolist_to_binary(Value));
        _ -> undefined
    end.

assert_extension_value(Extension, ExtType, Extensions) ->
    case extension_value(Extension, ExtType, Extensions) of
        undefined -> throw(no_extension_present);
        Value -> Value
    end.

issuing_distribution_point(Extensions) ->
    transform(extension_value(?'id-ce-issuingDistributionPoint', 'IssuingDistributionPoint', Extensions)).

authority_key_identifier(Extensions) ->
    transform(extension_value(?'id-ce-authorityKeyIdentifier', 'AuthorityKeyIdentifier', Extensions)).

%% A CRL time in Gregorian seconds (pubkey_cert:time_str_2_gregorian_sec/1).
seconds(Time) -> clause_public_key_path:gregorian_seconds(Time).

transform(Term) -> clause_public_key_codec:transform(Term, decode).
