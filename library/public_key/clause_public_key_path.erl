%% clause_public_key_path: public_key:pkix_path_validation/3 (plan 21 step LIB80D2,
%% docs/libraries/public_key.md#path-validation): RFC 5280 basic path validation as OTP's public_key and pubkey_cert
%% do it, with the validation state, the extensions, name constraints, the valid policy tree and the verify fun.
-module(clause_public_key_path).
-export([gregorian_seconds/1, pkix_path_validation/3]).

-include("include/public_key.hrl").

%% The state of a path validation (RFC 5280 6.1.2), as OTP's #path_validation_state{}.
-record(state, {
    %% The valid policy tree and the count of nodes ever added to it (never decremented by pruning).
    valid_policy_tree,
    policy_tree_node_count = 1,
    user_initial_policy_set,
    explicit_policy,
    inhibit_any_policy,
    inhibit_policy_mapping,
    %% Policy extensions of the current certificate, handled when the next one is prepared.
    policy_mapping_ext,
    policy_constraint_ext,
    policy_inhibitany_ext,
    policy_ext_present,
    policy_ext_any,
    current_any_policy_qualifiers,
    cert_num,
    last_cert = false,
    %% Name constraints.
    permitted_subtrees = no_constraints,
    excluded_subtrees = [],
    working_public_key_algorithm,
    working_public_key,
    working_public_key_parameters,
    working_issuer_name,
    max_path_length,
    verify_fun,
    user_state
}).

%% The most nodes a policy tree may ever get.
-define(MAX_POLICY_TREE_NODES, 1000).

%%--------------------------------------------------------------------------------------------------------------------
%% Path validation
%%--------------------------------------------------------------------------------------------------------------------

%% public_key:pkix_path_validation/3: a chain checked from a trusted certificate, or a path error given to the verify
%% fun first.
pkix_path_validation(Cert, CertChain, Options) when (not is_atom(Cert)), is_list(CertChain), is_list(Options) ->
    TrustedCert = combined_cert(Cert),
    {VerifyFun, UserState0} = proplists:get_value(verify_fun, Options, default_verify_fun()),
    try validate_time(TrustedCert, UserState0, VerifyFun) of
        UserState1 ->
            State = init_state(
                TrustedCert,
                length(CertChain),
                [{verify_fun, {VerifyFun, UserState1}} | proplists:delete(verify_fun, Options)]
            ),
            case exists_duplicate_cert(CertChain) of
                true -> {error, {bad_cert, duplicate_cert_in_path}};
                false -> path_validation(CertChain, State)
            end
    catch
        throw:{bad_cert, _} = Result -> {error, Result}
    end;
pkix_path_validation(PathErr, [Cert0 | Chain], Options0) when is_atom(PathErr) ->
    {VerifyFun, UserState0} = proplists:get_value(verify_fun, Options0, default_verify_fun()),
    Cert = combined_cert(Cert0),
    Reason = {bad_cert, PathErr},
    try apply_fun(VerifyFun, Cert#cert.otp, Cert#cert.der, Reason, UserState0) of
        {valid, UserState} ->
            Options = proplists:delete(verify_fun, Options0),
            pkix_path_validation(Cert, Chain, [{verify_fun, {VerifyFun, UserState}} | Options]);
        {fail, UserReason} ->
            {error, UserReason}
    catch
        _:_ -> {error, Reason}
    end.

%% OTP's verify fun: bad certificates fail, unknown extensions are left to the critical check.
default_verify_fun() ->
    {
        fun
            (_, {bad_cert, _} = Reason, _) -> {fail, Reason};
            (_, {extension, _}, UserState) -> {unknown, UserState};
            (_, valid, UserState) -> {valid, UserState};
            (_, valid_peer, UserState) -> {valid, UserState}
        end,
        []
    }.

%% Whether a certificate appears twice in a row.
exists_duplicate_cert([]) -> false;
exists_duplicate_cert([Cert, Cert | _]) -> true;
exists_duplicate_cert([_ | Rest]) -> exists_duplicate_cert(Rest).

%% Each certificate validated in turn; at the end, the working key and the valid policy nodes with their qualifiers.
path_validation([], #state{
    working_public_key_algorithm = Algorithm,
    working_public_key = PublicKey,
    working_public_key_parameters = PublicKeyParams,
    valid_policy_tree = Tree
}) ->
    CollectQualifiers = fun(#{expected_policy_set := PolicySet} = Node) ->
        Node#{qualifier_set => lists:flatmap(fun(Policy) -> collect_qualifiers(Tree, Policy) end, PolicySet)}
    end,
    {ok, {{Algorithm, PublicKey, PublicKeyParams}, lists:map(CollectQualifiers, constrained_policy_node_set(Tree))}};
path_validation([DerCert | Rest], #state{max_path_length = Len} = State) when Len >= 0 ->
    try validate(DerCert, State#state{last_cert = Rest =:= []}) of
        #state{} = NewState -> path_validation(Rest, NewState)
    catch
        throw:Reason -> {error, Reason}
    end;
path_validation([Cert | _] = Path, #state{user_state = UserState0, verify_fun = VerifyFun} = State) ->
    Reason = {bad_cert, max_path_length_reached},
    try apply_fun(VerifyFun, otp_cert(Cert), der_cert(Cert), Reason, UserState0) of
        {valid, UserState} -> path_validation(Path, State#state{max_path_length = 0, user_state = UserState});
        {fail, _} -> {error, Reason}
    catch
        _:_ -> {error, Reason}
    end.

%% One certificate checked (RFC 5280 6.1.3) and the state prepared for the next (6.1.4).
validate(
    Cert0,
    #state{
        working_issuer_name = Issuer,
        working_public_key = Key,
        working_public_key_parameters = KeyParams,
        permitted_subtrees = Permit,
        excluded_subtrees = Exclude,
        last_cert = Last,
        user_state = UserState0,
        verify_fun = VerifyFun
    } = State0
) ->
    Cert = combined_cert(Cert0),
    {State1, UserState1} = validate_extensions(Cert, State0, UserState0, VerifyFun),
    UserState2 = validate_time(Cert, UserState1, VerifyFun),
    UserState3 = validate_issuer(Cert, Issuer, UserState2, VerifyFun),
    UserState4 = validate_names(Cert, Permit, Exclude, Last, UserState3, VerifyFun),
    UserState5 = validate_signature(Cert, Key, KeyParams, UserState4, VerifyFun),
    Result =
        case Last of
            false -> valid;
            true -> valid_peer
        end,
    UserState = verify_fun(Cert, Result, UserState5, VerifyFun),
    prepare_for_next_cert(Cert, State1#state{user_state = UserState}).

%% A certificate in both forms.
combined_cert(#'Certificate'{} = Cert) ->
    Der = public_key:der_encode('Certificate', Cert),
    #cert{der = Der, otp = public_key:pkix_decode_cert(Der, otp)};
combined_cert(#cert{} = Cert) ->
    Cert;
combined_cert(Der) when is_binary(Der) ->
    #cert{der = Der, otp = public_key:pkix_decode_cert(Der, otp)};
combined_cert(#'OTPCertificate'{} = Otp) ->
    #cert{der = der_cert(Otp), otp = Otp}.

otp_cert(Der) when is_binary(Der) -> public_key:pkix_decode_cert(Der, otp);
otp_cert(#'OTPCertificate'{} = Cert) -> Cert;
otp_cert(#cert{otp = OtpCert}) -> OtpCert.

der_cert(#'OTPCertificate'{} = Cert) -> public_key:pkix_encode('OTPCertificate', Cert, otp);
der_cert(Der) when is_binary(Der) -> Der;
der_cert(#cert{der = DerCert}) -> DerCert.

%%--------------------------------------------------------------------------------------------------------------------
%% Validation state
%%--------------------------------------------------------------------------------------------------------------------

%% The initial state (RFC 5280 6.1.2) prepared with the trusted certificate.
init_state(Cert, DefaultPathLen, Options) ->
    MaxLen = proplists:get_value(max_path_length, Options, DefaultPathLen),
    {VerifyFun, UserState} = proplists:get_value(verify_fun, Options, default_verify_fun()),
    State = #state{
        max_path_length = MaxLen,
        user_initial_policy_set = policy_set(Options, [?anyPolicy]),
        valid_policy_tree = root(),
        explicit_policy = policy_indicator(MaxLen, proplists:get_value(explicit_policy, Options, false)),
        inhibit_any_policy = policy_indicator(MaxLen, proplists:get_value(inhibit_any_policy, Options, false)),
        inhibit_policy_mapping = policy_indicator(MaxLen, proplists:get_value(inhibit_policy_mapping, Options, false)),
        verify_fun = VerifyFun,
        user_state = UserState,
        cert_num = 0
    },
    prepare_for_next_cert(Cert, State).

%% 6.1.2 d, e, f: 0 when the indicator is set, else the path length plus one.
policy_indicator(_, true) -> 0;
policy_indicator(N, false) -> N + 1.

policy_set(Opts, Default) ->
    case proplists:get_value(policy_set, Opts, undefined) of
        undefined -> Default;
        Set -> [oidify(OidStr) || OidStr <- Set]
    end.

oidify(Oid) when is_tuple(Oid) ->
    Oid;
oidify(Oid) when is_list(Oid) ->
    list_to_tuple([list_to_integer(StrInt) || StrInt <- string:tokens(Oid, "$.")]).

%% The state for the next certificate: pending policy mappings handled, the working key and issuer from this one,
%% the policy counters decremented and constrained.
prepare_for_next_cert(Cert, #state{policy_mapping_ext = Ext} = State0) when Ext =/= undefined ->
    State1 = handle_policy_mappings(Cert, State0),
    prepare_for_next_cert(Cert, State1#state{policy_mapping_ext = undefined, current_any_policy_qualifiers = undefined});
prepare_for_next_cert(
    Cert,
    #state{
        working_public_key_algorithm = PrevAlgo,
        working_public_key_parameters = PrevParams,
        cert_num = CertNum,
        explicit_policy = ExplicitPolicy,
        inhibit_policy_mapping = PolicyMapping,
        inhibit_any_policy = AnyPolicy
    } = State0
) ->
    #'OTPCertificate'{tbsCertificate = TBSCert} = OtpCert = otp_cert(Cert),
    {Algorithm, PublicKey, PublicKeyParams0} =
        public_key_info(TBSCert#'OTPTBSCertificate'.subjectPublicKeyInfo, State0),
    PublicKeyParams =
        case PublicKeyParams0 of
            'NULL' when Algorithm =:= PrevAlgo -> PrevParams;
            asn1_NOVALUE when Algorithm =:= PrevAlgo -> PrevParams;
            _ -> PublicKeyParams0
        end,
    IsSelfSigned = clause_public_key_cert:pkix_is_self_signed(OtpCert),
    State1 = State0#state{
        working_public_key_algorithm = Algorithm,
        working_public_key = PublicKey,
        working_public_key_parameters = PublicKeyParams,
        working_issuer_name = TBSCert#'OTPTBSCertificate'.subject,
        cert_num = CertNum + 1,
        policy_ext_present = false,
        valid_policy_tree = assert_valid_policy_tree(
            State0#state.explicit_policy, State0#state.policy_ext_present, State0#state.valid_policy_tree
        ),
        current_any_policy_qualifiers = undefined,
        policy_ext_any = undefined,
        explicit_policy = maybe_decrement(ExplicitPolicy, IsSelfSigned),
        inhibit_policy_mapping = maybe_decrement(PolicyMapping, IsSelfSigned),
        inhibit_any_policy = maybe_decrement(AnyPolicy, IsSelfSigned)
    },
    handle_last_cert(Cert, handle_inhibit_anypolicy(handle_policy_constraints(State1))).

%% The algorithm, key and parameters of a certificate's public key; inherited parameters as OTP takes them.
public_key_info(#'OTPSubjectPublicKeyInfo'{subjectPublicKey = PublicKey, algorithm = AlgInfo}, #state{
    working_public_key_algorithm = WorkingAlgorithm, working_public_key_parameters = WorkingParams
}) ->
    Algorithm = AlgInfo#'PublicKeyAlgorithm'.algorithm,
    Params =
        case AlgInfo#'PublicKeyAlgorithm'.parameters of
            {null, 'NULL'} when WorkingAlgorithm == Algorithm -> WorkingParams;
            asn1_NOVALUE when Algorithm == ?'id-Ed25519'; Algorithm == ?'id-Ed448' -> {namedCurve, Algorithm};
            {params, P} -> P;
            P -> P
        end,
    {Algorithm, PublicKey, Params}.

maybe_decrement(0, _) -> 0;
maybe_decrement(N, false) -> N - 1;
maybe_decrement(N, true) -> N.

%% Calls the verify fun of arity 4 or 3.
apply_fun(Fun, OtpCert, DerCert, Result, UserState) ->
    if
        is_function(Fun, 4) -> Fun(OtpCert, DerCert, Result, UserState);
        is_function(Fun, 3) -> Fun(OtpCert, Result, UserState)
    end.

%% The verify fun's answer to a result: the new user state, or a thrown {bad_cert, Reason}.
verify_fun(#cert{der = DerCert, otp = OtpCert}, Result, UserState0, VerifyFun) ->
    case apply_fun(VerifyFun, OtpCert, DerCert, Result, UserState0) of
        {valid, UserState} ->
            UserState;
        {valid_peer, UserState} ->
            UserState;
        {fail, {bad_cert, _} = Reason} ->
            throw(Reason);
        {fail, Reason} ->
            throw({bad_cert, Reason});
        {unknown, UserState} ->
            case Result of
                {extension, #'Extension'{critical = true}} -> throw({bad_cert, unknown_critical_extension});
                _ -> UserState
            end
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Checks
%%--------------------------------------------------------------------------------------------------------------------

%% The validity period includes the current time (cert_expired), and its dates parse (invalid_validity_dates).
validate_time(Cert, UserState, VerifyFun) ->
    case validity(otp_cert(Cert)) of
        expired -> verify_fun(Cert, {bad_cert, cert_expired}, UserState, VerifyFun);
        error -> verify_fun(Cert, {bad_cert, invalid_validity_dates}, UserState, VerifyFun);
        ok -> UserState
    end.

validity(#'OTPCertificate'{tbsCertificate = TBSCert}) ->
    {'Validity', NotBeforeStr, NotAfterStr} = TBSCert#'OTPTBSCertificate'.validity,
    Now = calendar:datetime_to_gregorian_seconds(calendar:universal_time()),
    try
        NotBefore = gregorian_seconds(notBefore, NotBeforeStr),
        NotAfter = gregorian_seconds(notAfter, NotAfterStr),
        if
            NotBefore =< Now, Now =< NotAfter -> ok;
            true -> expired
        end
    catch
        error:function_clause -> error
    end.

%% A UTC time of notBefore takes years 50-99 as 19xx (as PKITS needs), else the current century; notAfter uses the
%% sliding window around the current year.
gregorian_seconds(notBefore, {utcTime, [FirstDigitYear | _] = UtcTime}) ->
    Y1 = list_to_integer([FirstDigitYear]),
    YearPrefix =
        case Y1 > 4 andalso Y1 =< 9 of
            true ->
                "19";
            false ->
                {Y, _, _} = erlang:date(),
                integer_to_list(Y div 100)
        end,
    gregorian_seconds({generalTime, YearPrefix ++ UtcTime});
gregorian_seconds(notAfter, {utcTime, UtcTime}) ->
    gregorian_seconds({generalTime, sliding_year_window(UtcTime)});
gregorian_seconds(_, {generalTime, _} = GeneralTime) ->
    gregorian_seconds(GeneralTime).

gregorian_seconds({utcTime, UtcTime}) ->
    gregorian_seconds(notAfter, {utcTime, UtcTime});
gregorian_seconds({generalTime, [Y1, Y2, Y3, Y4, M1, M2, D1, D2, H1, H2, M3, M4, S1, S2, $Z]}) ->
    Date = {list_to_integer([Y1, Y2, Y3, Y4]), list_to_integer([M1, M2]), list_to_integer([D1, D2])},
    Time = {list_to_integer([H1, H2]), list_to_integer([M3, M4]), list_to_integer([S1, S2])},
    calendar:datetime_to_gregorian_seconds({Date, Time}).

sliding_year_window([Y1, Y2, M1, M2, D1, D2, H1, H2, M3, M4, S1, S2, Z]) ->
    {{CurrentYear, _, _}, _} = calendar:universal_time(),
    MinYear = positive_rem((CurrentYear rem 100) - 50, 100),
    YearWindow =
        case list_to_integer([Y1, Y2]) of
            N when N < MinYear -> CurrentYear + 50;
            _ -> CurrentYear - 49
        end,
    [Year1, Year2] = integer_to_list(YearWindow div 100),
    [Year1, Year2, Y1, Y2, M1, M2, D1, D2, H1, H2, M3, M4, S1, S2, Z].

positive_rem(A, B) when A > 0 -> A rem B;
positive_rem(A, B) when A < 0 -> positive_rem(A + B, B);
positive_rem(0, _) -> 0.

%% The certificate's issuer is the working issuer name.
validate_issuer(Cert, Issuer, UserState, VerifyFun) ->
    #'OTPCertificate'{tbsCertificate = TBSCert} = otp_cert(Cert),
    case clause_public_key_cert:is_issuer(Issuer, TBSCert#'OTPTBSCertificate'.issuer) of
        true -> UserState;
        _ -> verify_fun(Cert, {bad_cert, invalid_issuer}, UserState, VerifyFun)
    end.

%% The subject (with its e-mail addresses) and subject alternative names are within the name constraints; a
%% self-signed certificate that is not the last is not checked.
validate_names(Cert, Permit, Exclude, Last, UserState, VerifyFun) ->
    OtpCert = otp_cert(Cert),
    case clause_public_key_cert:pkix_is_self_signed(OtpCert) andalso (not Last) of
        true ->
            UserState;
        false ->
            TBSCert = OtpCert#'OTPCertificate'.tbsCertificate,
            Subject = TBSCert#'OTPTBSCertificate'.subject,
            Name = [{directoryName, Subject} | extract_email(Subject)],
            AltNames =
                case select_extension(?'id-ce-subjectAltName', TBSCert) of
                    undefined -> [];
                    #'Extension'{extnValue = Value} -> Value
                end,
            case is_permitted_name(Name, Permit, Exclude) of
                false ->
                    verify_fun(Cert, {bad_cert, distinguished_name_not_permitted}, UserState, VerifyFun);
                true ->
                    case is_permitted_name(AltNames, Permit, Exclude) of
                        false -> verify_fun(Cert, {bad_cert, name_not_permitted}, UserState, VerifyFun);
                        true -> UserState
                    end
            end
    end.

%% RFC 3280 4.1.2.6: e-mail addresses in a subject.
extract_email({rdnSequence, List}) ->
    case [Mail || [#'AttributeTypeAndValue'{type = ?'id-emailAddress', value = Mail}] <- List] of
        [Mail | _] -> [{rfc822Name, Mail}];
        [] -> []
    end.

is_permitted_name(Name, Permit, Exclude) ->
    is_permitted(Name, Permit) andalso (not is_excluded(Name, Exclude)).

is_permitted(_, no_constraints) -> true;
is_permitted(Names, Constraints) -> is_valid_name(Names, Constraints, true).

is_excluded([], _) -> false;
is_excluded(Names, Constraints) -> is_valid_name(Names, Constraints, false).

%% Default unless a name of a constrained type matches (or fails to match) differently.
is_valid_name([], _, Default) ->
    Default;
is_valid_name([{Type, Name} | Rest], Constraints, Default) ->
    case [N || #'GeneralSubtree'{base = {TreeType, N}} <- Constraints, TreeType =:= Type] of
        [] ->
            is_valid_name(Rest, Constraints, Default);
        ConstraintNames ->
            case clause_public_key_cert:match_name(Type, Name, ConstraintNames) of
                Default -> is_valid_name(Rest, Constraints, Default);
                Fail -> Fail
            end
    end.

%% The signature verifies with the working key (parameters of RSASSA-PSS taken from the signature algorithm when the
%% key has none).
validate_signature(#cert{der = DerCert} = Cert, Key, KeyParams0, UserState, VerifyFun) ->
    OtpCert = otp_cert(Cert),
    KeyParams = key_params(OtpCert#'OTPCertificate'.tbsCertificate, KeyParams0),
    case verify_signature(OtpCert, DerCert, Key, KeyParams) of
        true -> UserState;
        false -> verify_fun(Cert, {bad_cert, invalid_signature}, UserState, VerifyFun)
    end.

key_params(
    #'OTPTBSCertificate'{signature = #'SignatureAlgorithm'{algorithm = ?'id-RSASSA-PSS', parameters = KeyParams}},
    KeyParams0
) when KeyParams0 == asn1_NOVALUE; KeyParams0 == 'NULL' ->
    KeyParams;
key_params(_, KeyParams) ->
    KeyParams.

verify_signature(#'OTPCertificate'{signature = Signature, signatureAlgorithm = SigAlg}, DerCert, Key, KeyParams) ->
    {DigestType, _, _} = clause_public_key_keys:sign_algorithm_types(SigAlg),
    PlainText = clause_public_key_keys:first_element(DerCert),
    case Key of
        #'RSAPublicKey'{} ->
            case KeyParams of
                #'RSASSA-PSS-params'{} ->
                    public_key:verify(
                        PlainText, DigestType, Signature, Key, clause_public_key_keys:rsa_opts(KeyParams)
                    );
                'NULL' ->
                    public_key:verify(PlainText, DigestType, Signature, Key);
                asn1_NOVALUE ->
                    public_key:verify(PlainText, DigestType, Signature, Key)
            end;
        #'SLH-DSAPublicKey'{} ->
            public_key:verify(PlainText, none, Signature, Key);
        _ ->
            public_key:verify(PlainText, DigestType, Signature, {Key, KeyParams})
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Extensions
%%--------------------------------------------------------------------------------------------------------------------

select_extension(Id, #'OTPTBSCertificate'{extensions = Extensions}) ->
    clause_public_key_cert:select_extension(Id, clause_public_key_cert:extensions_list(Extensions)).

%% The extensions of basic path validation; every certificate is checked as a CA unless it is the last.
validate_extensions(Cert, State, UserState, VerifyFun) ->
    #'OTPCertificate'{tbsCertificate = TBSCert} = OtpCert = otp_cert(Cert),
    SelfSigned = clause_public_key_cert:pkix_is_self_signed(OtpCert),
    Context = {Cert, SelfSigned, VerifyFun},
    validate_extensions(Context, TBSCert#'OTPTBSCertificate'.extensions, State, no_basic_constraint, UserState).

validate_extensions(Context, asn1_NOVALUE, State, BasicConstraint, UserState) ->
    validate_extensions(Context, [], State, BasicConstraint, UserState);
validate_extensions({Cert, _, VerifyFun}, [], State, basic_constraint, UserState) ->
    {State, validate_ext_key_usage(Cert, UserState, VerifyFun, ca)};
validate_extensions({Cert, SelfSigned, VerifyFun}, [], State, no_basic_constraint, UserState) ->
    no_basic_constraint(Cert, SelfSigned, VerifyFun, State, UserState);
validate_extensions(Context, [#'Extension'{} = Extension | Rest], State0, BasicConstraint0, UserState0) ->
    {State, BasicConstraint, UserState} = extension(Context, Extension, State0, BasicConstraint0, UserState0),
    validate_extensions(Context, Rest, State, BasicConstraint, UserState).

%% Without a cA basic constraint: a last certificate that is self-signed passes, one that may sign certificates is
%% missing its constraint; any other certificate is.
no_basic_constraint(Cert, SelfSigned, VerifyFun, #state{max_path_length = Len, last_cert = Last} = State, UserState) ->
    case Last of
        true when SelfSigned ->
            {State, UserState};
        true ->
            case is_digitally_sign_cert(Cert) of
                true ->
                    missing_basic_constraints(Cert, SelfSigned, State, VerifyFun, UserState, Len);
                false ->
                    {
                        State#state{max_path_length = Len - 1},
                        validate_ext_key_usage(Cert, UserState, VerifyFun, endentity)
                    }
            end;
        false ->
            missing_basic_constraints(Cert, SelfSigned, State, VerifyFun, UserState, Len)
    end.

missing_basic_constraints(Cert, SelfSigned, State, VerifyFun, UserState0, Len) ->
    UserState = verify_fun(Cert, {bad_cert, missing_basic_constraint}, UserState0, VerifyFun),
    case SelfSigned of
        true -> {State, UserState};
        false -> {State#state{max_path_length = Len - 1}, UserState}
    end.

%% One extension's effect: {State, BasicConstraint, UserState}.
extension(
    {_, SelfSigned, _},
    #'Extension'{
        extnID = ?'id-ce-basicConstraints', extnValue = #'BasicConstraints'{cA = true, pathLenConstraint = N}
    },
    #state{max_path_length = Len} = State,
    _,
    UserState
) ->
    Length =
        if
            SelfSigned -> erlang:min(N, Len);
            true -> erlang:min(N, Len - 1)
        end,
    {State#state{max_path_length = Length}, basic_constraint, UserState};
extension(
    _, #'Extension'{extnID = ?'id-ce-basicConstraints', extnValue = #'BasicConstraints'{cA = false}}, State, B, U
) ->
    {State, B, U};
extension(
    {Cert, _, VerifyFun},
    #'Extension'{extnID = ?'id-ce-keyUsage', extnValue = KeyUses},
    #state{last_cert = Last} = State,
    B,
    UserState
) ->
    case Last orelse lists:member(keyCertSign, KeyUses) of
        true -> {State, B, UserState};
        false -> {State, B, verify_fun(Cert, {bad_cert, invalid_key_usage}, UserState, VerifyFun)}
    end;
extension(
    {Cert, _, VerifyFun},
    #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = Names, critical = true} = Ext,
    State,
    B,
    UserState
) ->
    case validate_subject_alt_names(Names) of
        true -> {State, B, UserState};
        false -> {State, B, verify_fun(Cert, {extension, Ext}, UserState, VerifyFun)}
    end;
extension(_, #'Extension'{extnID = ?'id-ce-nameConstraints', extnValue = NameConst}, State, B, UserState) ->
    #'NameConstraints'{permittedSubtrees = Permitted, excludedSubtrees = Excluded} = NameConst,
    {add_name_constraints(Permitted, Excluded, State), B, UserState};
extension(
    {_, SelfSigned, _}, #'Extension'{extnID = ?'id-ce-certificatePolicies', extnValue = Info}, State, B, UserState
) ->
    {Tree, NodeCount} = process_policy_tree(Info, SelfSigned, State),
    {
        State#state{
            policy_ext_present = true,
            current_any_policy_qualifiers = current_any_policy_qualifiers(Info),
            valid_policy_tree = Tree,
            policy_tree_node_count = NodeCount
        },
        B,
        UserState
    };
extension(_, #'Extension'{extnID = ?'id-ce-policyConstraints'} = Ext, State, B, UserState) ->
    {State#state{policy_constraint_ext = Ext}, B, UserState};
extension(_, #'Extension'{extnID = ?'id-ce-policyMappings'} = Ext, State, B, UserState) ->
    {State#state{policy_mapping_ext = Ext}, B, UserState};
extension(_, #'Extension'{extnID = ?'id-ce-inhibitAnyPolicy'} = Ext, State, B, UserState) ->
    {State#state{policy_inhibitany_ext = Ext}, B, UserState};
extension(
    {Cert, _, VerifyFun},
    #'Extension'{extnID = ?'id-ce-extKeyUsage', critical = true, extnValue = ExtKeyUse} = Extension,
    #state{last_cert = false} = State,
    B,
    UserState
) ->
    case ext_keyusage_includes_any(ExtKeyUse) of
        true ->
            {State, B, verify_fun(Cert, {bad_cert, invalid_ext_key_usage}, UserState, VerifyFun)};
        false ->
            case ca_known_ext_key_usage(ExtKeyUse) of
                true -> {State, B, UserState};
                false -> {State, B, verify_fun(Cert, {extension, Extension}, UserState, VerifyFun)}
            end
    end;
extension({Cert, _, VerifyFun}, Extension, State, B, UserState) ->
    {State, B, verify_fun(Cert, {extension, Extension}, UserState, VerifyFun)}.

%% Whether a subject alternative names extension holds a usable name.
validate_subject_alt_names(Names) ->
    lists:any(fun is_valid_subject_alt_name/1, Names).

is_valid_subject_alt_name({Name, Value}) when Name == rfc822Name; Name == dNSName -> Value =/= "";
is_valid_subject_alt_name({iPAdress, Addr}) -> length(Addr) == 4 orelse length(Addr) == 16;
is_valid_subject_alt_name({uniformResourceIdentifier, URI}) -> is_valid_uri(URI);
is_valid_subject_alt_name({directoryName, _}) -> true;
is_valid_subject_alt_name({_, [_ | _]}) -> true;
is_valid_subject_alt_name({_, _}) -> false.

is_valid_uri(AbsURI) ->
    case uri_string:normalize(AbsURI, [return_map]) of
        #{scheme := _} -> true;
        _ -> false
    end.

%% The permitted subtrees narrowed and the excluded ones joined (RFC 5280 6.1.4 g).
add_name_constraints(
    NewPermitted, NewExcluded, #state{permitted_subtrees = Permitted, excluded_subtrees = Excluded} = S
) ->
    S#state{
        permitted_subtrees = subtree_intersection(NewPermitted, Permitted),
        excluded_subtrees = subtree_union(NewExcluded, Excluded)
    }.

subtree_union(asn1_NOVALUE, Trees) -> Trees;
subtree_union(Trees1, Trees2) -> Trees1 ++ Trees2.

subtree_intersection(asn1_NOVALUE, Trees) -> Trees;
subtree_intersection(List, no_constraints) -> List;
subtree_intersection([Tree | Trees1], Trees2) -> subtree_intersection(Trees1, is_in_intersection(Tree, Trees2));
subtree_intersection([], TreesInt) -> TreesInt.

%% A subtree added to the intersection: kept when it matches the first subtree of its type, else made empty.
is_in_intersection(
    #'GeneralSubtree'{base = {directoryName, {rdnSequence, Name1}}} = Name,
    [#'GeneralSubtree'{base = {directoryName, {rdnSequence, Name2}}} | Trees]
) ->
    case clause_public_key_cert:is_dir_name(Name1, Name2, false) of
        true -> [Name | Trees];
        false -> [Name#'GeneralSubtree'{base = {directoryName, {rdnSequence, [none]}}} | Trees]
    end;
is_in_intersection(#'GeneralSubtree'{base = {ipAdress, Ip}}, Trees = [#'GeneralSubtree'{base = {ipAdress, Ip}} | _]) ->
    Trees;
is_in_intersection(
    #'GeneralSubtree'{base = {x400Address, OrAddr1}} = Addr, [#'GeneralSubtree'{base = {x400Address, OrAddr2}} | Trees]
) ->
    case clause_public_key_cert:match_name(x400Address, OrAddr1, [OrAddr2]) of
        true -> [Addr | Trees];
        false -> [#'GeneralSubtree'{base = {x400Address, ""}} | Trees]
    end;
is_in_intersection(#'GeneralSubtree'{base = {Type, Name1}} = Name, [#'GeneralSubtree'{base = {Type, Name2}} | Trees]) ->
    case string:equal(Name1, Name2, true) of
        true -> [Name | Trees];
        false -> [#'GeneralSubtree'{base = {Type, ""}} | Trees]
    end;
is_in_intersection(New, []) ->
    [New];
is_in_intersection(Name, [Other | IntCandidates]) ->
    [Other | is_in_intersection(Name, IntCandidates)].

%% Whether a certificate's key usage allows signing certificates.
is_digitally_sign_cert(Cert) ->
    #'OTPCertificate'{tbsCertificate = TBSCert} = otp_cert(Cert),
    case select_extension(?'id-ce-keyUsage', TBSCert) of
        undefined -> false;
        #'Extension'{extnValue = KeyUse} -> lists:member(keyCertSign, KeyUse)
    end.

%% The key usage and extended key usage agree, for a CA or an end entity.
validate_ext_key_usage(#cert{otp = #'OTPCertificate'{tbsCertificate = TBSCert}} = Cert, UserState, VerifyFun, Type) ->
    KeyUseExt = select_extension(?'id-ce-keyUsage', TBSCert),
    ExtKeyUseExt = select_extension(?'id-ce-extKeyUsage', TBSCert),
    case compatible_ext_key_usage(KeyUseExt, ExtKeyUseExt, Type) of
        true -> UserState;
        false -> verify_fun(Cert, {bad_cert, {key_usage_mismatch, {KeyUseExt, ExtKeyUseExt}}}, UserState, VerifyFun)
    end.

compatible_ext_key_usage(undefined, _, endentity) ->
    true;
compatible_ext_key_usage(_, undefined, _) ->
    true;
compatible_ext_key_usage(
    #'Extension'{extnID = ?'id-ce-keyUsage', extnValue = KeyUses},
    #'Extension'{extnID = ?'id-ce-extKeyUsage', extnValue = Purposes},
    Type
) ->
    ext_keyusage_includes_any(Purposes) orelse lists:all(fun(P) -> compatible_purpose(KeyUses, P, Type) end, Purposes).

%% Whether the key usages allow one extended key usage purpose; unknown purposes are the verify fun's.
compatible_purpose(_, Purpose, ca) when Purpose == ?'id-kp-serverAuth'; Purpose == ?'id-kp-clientAuth' ->
    true;
compatible_purpose(KeyUses, ?'id-kp-serverAuth', endentity) ->
    any_member([digitalSignature, keyAgreement, keyEncipherment], KeyUses);
compatible_purpose(KeyUses, ?'id-kp-clientAuth', endentity) ->
    any_member([digitalSignature, keyAgreement], KeyUses);
compatible_purpose(KeyUses, ?'id-kp-codeSigning', _) ->
    lists:member(digitalSignature, KeyUses);
compatible_purpose(KeyUses, ?'id-kp-emailProtection', _) ->
    any_member([digitalSignature, nonRepudiation, keyAgreement, keyEncipherment], KeyUses);
compatible_purpose(KeyUses, Id, _) when Id == ?'id-kp-timeStamping'; Id == ?'id-kp-OCSPSigning' ->
    any_member([digitalSignature, nonRepudiation], KeyUses);
compatible_purpose(_, _, _) ->
    true.

any_member(Items, List) -> lists:any(fun(Item) -> lists:member(Item, List) end, Items).

ext_keyusage_includes_any(KeyUses) when is_list(KeyUses) -> lists:member(?anyExtendedKeyUsage, KeyUses);
ext_keyusage_includes_any(_) -> false.

%% Whether a CA's extended key usages include one that is known.
ca_known_ext_key_usage(ExtKeyUses) ->
    Known = [
        ?'id-kp-serverAuth',
        ?'id-kp-clientAuth',
        ?'id-kp-codeSigning',
        ?'id-kp-emailProtection',
        ?'id-kp-timeStamping',
        ?'id-kp-OCSPSigning'
    ],
    any_member(Known, ExtKeyUses).

%%--------------------------------------------------------------------------------------------------------------------
%% Policies (RFC 5280 6.1.3 d-f, 6.1.4 a-b and i-j, 6.1.5 b and g)
%%--------------------------------------------------------------------------------------------------------------------

%% 6.1.3 e, f: without a policies extension the tree becomes empty once explicit policy is required.
assert_valid_policy_tree(0, PresentPolicyExtension, Tree) ->
    case PresentPolicyExtension of
        false -> empty();
        _ -> Tree
    end;
assert_valid_policy_tree(_, _, Tree) ->
    Tree.

%% 6.1.3 d: the policies of a certificate added to a non-empty tree, then the childless nodes pruned.
process_policy_tree(
    PolicyInformation, SelfSigned, #state{valid_policy_tree = Tree0, policy_tree_node_count = Count0} = S
) ->
    case is_empty(Tree0) of
        true ->
            {Tree0, Count0};
        false ->
            {Tree, Count} = add_policy_children(PolicyInformation, SelfSigned, S),
            assert_policy_tree_node_count(Count),
            {prune_tree(Tree), Count}
    end.

add_policy_children(PolicyInfoList0, SelfSigned, #state{
    valid_policy_tree = Tree0,
    policy_tree_node_count = Count0,
    inhibit_any_policy = AnyPolicyConstraint,
    cert_num = CertNum,
    max_path_length = PathLen
}) ->
    {AnyExt, PolicyInfoList} =
        case lists:keytake(?anyPolicy, #'PolicyInformation'.policyIdentifier, PolicyInfoList0) of
            {value, AnyExt0, PolicyInfoList1} -> {AnyExt0, PolicyInfoList1};
            false -> {undefined, PolicyInfoList0}
        end,
    LeafFun = fun(#{expected_policy_set := ExpPolicySet}) -> policy_children(ExpPolicySet, PolicyInfoList) end,
    {Tree1, Count1} = add_leaves(Tree0, Count0, LeafFun),
    AllLeaves = all_leaves(Tree1),
    Siblings = fun
        (#{valid_policy := ?anyPolicy}) -> any_policy_children(AllLeaves, PolicyInfoList);
        (_) -> []
    end,
    Grown = add_leaf_siblings(Tree1, Count1, Siblings),
    handle_any_ext(Grown, AnyExt, AnyPolicyConstraint, SelfSigned, CertNum, PathLen).

%% 6.1.3 d 1 i: a child for each policy in the parent's expected set.
policy_children(ExpPolicySet, PolicyInfoList) ->
    lists:foldl(
        fun(#'PolicyInformation'{policyIdentifier = Policy, policyQualifiers = Qualifiers}, Acc) ->
            case lists:member(Policy, ExpPolicySet) of
                true -> [policy_node(Policy, Qualifiers, [Policy]) | Acc];
                false -> Acc
            end
        end,
        [],
        PolicyInfoList
    ).

%% 6.1.3 d 1 ii: without any match, children of the anyPolicy node for every policy.
any_policy_children([], PolicyInfoList) ->
    lists:foldl(
        fun(#'PolicyInformation'{policyIdentifier = Policy, policyQualifiers = Qualifiers}, Acc) ->
            [policy_node(Policy, Qualifiers, [Policy]) | Acc]
        end,
        [],
        PolicyInfoList
    );
any_policy_children(_, _) ->
    no_sibling.

%% 6.1.3 d 2: anyPolicy in the certificate adds the expected policies no child has yet.
handle_any_ext(Tree, undefined, _, _, _, _) ->
    Tree;
handle_any_ext(
    {Tree, Count},
    #'PolicyInformation'{policyIdentifier = ?anyPolicy, policyQualifiers = Qualifiers},
    AnyPolicyConstraint,
    SelfSigned,
    CertNum,
    PathLen
) ->
    case AnyPolicyConstraint > 0 orelse ((CertNum < PathLen) andalso SelfSigned) of
        true ->
            AllLeaves = all_leaves(Tree),
            Siblings = fun(#{expected_policy_set := ExpPolicySet}) ->
                [policy_node(Policy, Qualifiers, [Policy]) || Policy <- ExpPolicySet, not in_set(Policy, AllLeaves)]
            end,
            add_leaf_siblings(Tree, Count, Siblings);
        false ->
            {Tree, Count}
    end.

%% 6.1.4 a, b: the policy mappings of the previous certificate.
handle_policy_mappings(
    Cert,
    #state{
        valid_policy_tree = Tree0,
        policy_tree_node_count = Count0,
        policy_mapping_ext = #'Extension'{extnID = ?'id-ce-policyMappings', extnValue = PolicyMappings}
    } = State
) ->
    case handle_policy_mappings(PolicyMappings, Cert, Tree0, Count0, State) of
        {tree, Tree, Count} -> State#state{valid_policy_tree = Tree, policy_tree_node_count = Count};
        {user_state, UserState} -> State#state{user_state = UserState}
    end.

handle_policy_mappings([], _, Tree, Count, _) ->
    {tree, Tree, Count};
handle_policy_mappings([Mapping | Rest], Cert, Tree0, Count0, State) ->
    case handle_policy_mapping(Mapping, Cert, Tree0, Count0, State) of
        {tree, Tree, Count} ->
            assert_policy_tree_node_count(Count),
            handle_policy_mappings(Rest, Cert, Tree, Count, State);
        Other ->
            Other
    end.

%% 6.1.4 a: anyPolicy may not be mapped.
handle_policy_mapping(
    #'PolicyMappings_SEQOF'{issuerDomainPolicy = IssuerPolicy, subjectDomainPolicy = SubjectPolicy} = Ext,
    Cert,
    Tree0,
    Count0,
    #state{
        inhibit_policy_mapping = PolicyMapping,
        current_any_policy_qualifiers = AnyQualifiers,
        verify_fun = VerifyFun,
        user_state = UserState
    }
) ->
    case ?anyPolicy =/= IssuerPolicy andalso ?anyPolicy =/= SubjectPolicy of
        true ->
            {Tree, Count} = map_policy(Ext, Tree0, Count0, PolicyMapping, AnyQualifiers),
            {tree, Tree, Count};
        false ->
            {user_state, verify_fun(Cert, {bad_cert, {invalid_policy_mapping, Ext}}, UserState, VerifyFun)}
    end.

%% 6.1.4 b 2: with mapping inhibited, the issuer policy's leaves are deleted; b 1: otherwise their expected sets map
%% to the subject policy, or an anyPolicy leaf gets a mapped sibling.
map_policy(#'PolicyMappings_SEQOF'{issuerDomainPolicy = IssuerPolicy}, Tree0, Count, 0, _) ->
    {prune_tree(prune_leaves(Tree0, IssuerPolicy)), Count};
map_policy(
    #'PolicyMappings_SEQOF'{issuerDomainPolicy = IssuerPolicy, subjectDomainPolicy = SubjectPolicy},
    Tree,
    Count,
    N,
    AnyQualifiers
) when N > 0 ->
    MapPolicy = fun
        (#{valid_policy := ValidPolicy, expected_policy_set := Set} = Node) when ValidPolicy == IssuerPolicy ->
            case Set of
                [ValidPolicy] when ValidPolicy =/= SubjectPolicy ->
                    Node#{expected_policy_set => [SubjectPolicy]};
                _ ->
                    case lists:member(SubjectPolicy, Set) of
                        true -> Node;
                        false -> Node#{expected_policy_set => Set ++ [SubjectPolicy]}
                    end
            end;
        (Node) ->
            Node
    end,
    AnySiblings = fun
        (#{valid_policy := ?anyPolicy}) -> [policy_node(IssuerPolicy, AnyQualifiers, [SubjectPolicy])];
        (_) -> no_sibling
    end,
    case map_leaves(Tree, MapPolicy) of
        Tree -> add_leaf_siblings(Tree, Count, AnySiblings);
        NewTree -> {NewTree, Count}
    end.

%% 6.1.4 i: the policy constraints extension lowers the explicit policy and policy mapping counters.
handle_policy_constraints(
    #state{
        policy_constraint_ext = #'Extension'{
            extnID = ?'id-ce-policyConstraints',
            extnValue = #'PolicyConstraints'{requireExplicitPolicy = Explicit, inhibitPolicyMapping = InhibitMap}
        },
        explicit_policy = CurrentExplicit,
        inhibit_policy_mapping = CurrentMapping
    } = State
) ->
    State#state{
        explicit_policy = policy_constraint(CurrentExplicit, Explicit),
        inhibit_policy_mapping = policy_constraint(CurrentMapping, InhibitMap),
        policy_constraint_ext = undefined
    };
handle_policy_constraints(State) ->
    State.

%% 6.1.4 j: the inhibit anyPolicy extension lowers its counter.
handle_inhibit_anypolicy(
    #state{
        policy_inhibitany_ext = #'Extension'{extnID = ?'id-ce-inhibitAnyPolicy', extnValue = InhibitAnyPolicy},
        inhibit_any_policy = CurrentAnyPolicy
    } = State
) ->
    State#state{
        inhibit_any_policy = policy_constraint(CurrentAnyPolicy, InhibitAnyPolicy), policy_inhibitany_ext = undefined
    };
handle_inhibit_anypolicy(State) ->
    State.

policy_constraint(Current, asn1_NOVALUE) -> Current;
policy_constraint(Current, New) -> erlang:min(Current, New).

current_any_policy_qualifiers(Info) ->
    case lists:keyfind(?anyPolicy, #'PolicyInformation'.policyIdentifier, Info) of
        #'PolicyInformation'{policyQualifiers = AnyQualifiers} -> AnyQualifiers;
        _ -> []
    end.

%% 6.1.5 b and g: the last certificate's explicit policy and the tree intersected with the user's policies.
handle_last_cert(
    Cert,
    #state{
        last_cert = true, user_initial_policy_set = PolicySet, valid_policy_tree = Tree, policy_tree_node_count = Count0
    } = State0
) ->
    #'OTPCertificate'{tbsCertificate = TBSCert} = otp_cert(Cert),
    State =
        case select_extension(?'id-ce-policyConstraints', TBSCert) of
            #'Extension'{extnValue = #'PolicyConstraints'{requireExplicitPolicy = 0}} ->
                State0#state{explicit_policy = 0};
            _ ->
                State0
        end,
    {ValidTree, Count} = policy_tree_intersection(PolicySet, Tree, Count0),
    validate_policy_tree(Cert, State#state{valid_policy_tree = ValidTree, policy_tree_node_count = Count});
handle_last_cert(_, State) ->
    State.

%% An explicit policy requirement fails on an empty tree.
validate_policy_tree(
    Cert,
    #state{
        explicit_policy = Explicit, valid_policy_tree = Tree, user_state = UserState0, verify_fun = VerifyFun
    } = State
) ->
    case (Explicit > 0) orelse not is_empty(Tree) of
        true ->
            State;
        false ->
            Reason =
                {policy_requirement_not_met, {
                    {explicit_policy, Explicit}, {policy_set, constrained_policy_node_set(Tree)}
                }},
            State#state{user_state = verify_fun(Cert, {bad_cert, Reason}, UserState0, VerifyFun)}
    end.

policy_tree_intersection([?anyPolicy], Tree, Count) ->
    {Tree, Count};
policy_tree_intersection(UserPolicySet, Tree0, Count0) ->
    case is_empty(Tree0) of
        true ->
            {Tree0, Count0};
        false ->
            ValidPolicyNodeSet = valid_policy_node_set(Tree0),
            InvalidNodes = [
                Node
             || #{valid_policy := Policy} = Node <- lists:reverse(ValidPolicyNodeSet),
                Policy =/= ?anyPolicy,
                not lists:member(Policy, UserPolicySet)
            ],
            Tree1 = prune_invalid_nodes(Tree0, InvalidNodes),
            {Tree, Count} = handle_any_policy_leaves(Tree1, Count0, ValidPolicyNodeSet, UserPolicySet),
            {prune_tree(Tree), Count}
    end.

handle_any_policy_leaves(Tree0, Count0, ValidPolicyNodeSet, UserPolicySet) ->
    case any_leaves(Tree0) of
        [] ->
            {Tree0, Count0};
        AnyLeaves ->
            {Tree, Count} = add_policy_nodes(AnyLeaves, Tree0, Count0, ValidPolicyNodeSet, UserPolicySet),
            {prune_leaves(Tree, ?anyPolicy), Count}
    end.

add_policy_nodes([], Tree, Count, _, _) ->
    {Tree, Count};
add_policy_nodes([#{qualifier_set := Qualifiers} | Rest], Tree0, Count0, ValidPolicyNodeSet, UserPolicySet) ->
    Children = [
        policy_node(Policy, Qualifiers, [Policy])
     || Policy <- UserPolicySet, not in_set(Policy, ValidPolicyNodeSet)
    ],
    Siblings = fun
        (#{valid_policy := ?anyPolicy, qualifier_set := QSet}) when QSet == Qualifiers -> Children;
        (_) -> []
    end,
    {Tree, Count} = add_leaf_siblings(Tree0, Count0, Siblings),
    add_policy_nodes(Rest, Tree, Count, ValidPolicyNodeSet, UserPolicySet).

assert_policy_tree_node_count(Count) when Count > ?MAX_POLICY_TREE_NODES -> throw({bad_cert, policy_tree_exceeded});
assert_policy_tree_node_count(_) -> ok.

%%--------------------------------------------------------------------------------------------------------------------
%% The valid policy tree (OTP's pubkey_policy_tree): {} or {Node, Children}, where children are subtrees or leaves.
%%--------------------------------------------------------------------------------------------------------------------

policy_node(ValidPolicy, Qualifiers, ExpPolicySet) ->
    QualifierSet =
        case Qualifiers of
            asn1_NOVALUE -> [];
            _ -> Qualifiers
        end,
    #{valid_policy => ValidPolicy, qualifier_set => QualifierSet, expected_policy_set => ExpPolicySet}.

root() -> {policy_node(?anyPolicy, [], [?anyPolicy]), []}.

empty() -> {}.

is_empty({}) -> true;
is_empty(_) -> false.

in_set(Policy, Nodes) -> lists:any(fun(#{valid_policy := P}) -> P =:= Policy end, Nodes).

%% Each leaf given the children the fun makes; the count grows by them.
add_leaves({Parent, []}, Count, LeafFun) ->
    Leaves = LeafFun(Parent),
    {{Parent, Leaves}, Count + length(Leaves)};
add_leaves(Tree0, Count, LeafFun) ->
    {Tree, Added} = map_leaves_count(Tree0, fun(Leaf) -> {Leaf, LeafFun(Leaf)} end),
    {Tree, Count + Added}.

map_leaves_count({Parent, [{_, _} | _] = ChildNodes}, LeafFun) ->
    {Children, Added} = lists:mapfoldl(
        fun(ChildNode, Acc) ->
            {Node, N} = map_leaves_count(ChildNode, LeafFun),
            {Node, Acc + N}
        end,
        0,
        ChildNodes
    ),
    {{Parent, Children}, Added};
map_leaves_count({Parent, Leaves0}, LeafFun) ->
    {Leaves, Added} = lists:mapfoldl(
        fun(Leaf, Acc) ->
            {_, ChildNodes} = Node = LeafFun(Leaf),
            {Node, Acc + length(ChildNodes)}
        end,
        0,
        Leaves0
    ),
    {{Parent, Leaves}, Added}.

%% The deepest parents given siblings for their leaves, as the fun makes them for the parent.
add_leaf_siblings({Parent, [{_, _} | _] = ChildNodes}, Count, SiblingFun) ->
    {Children, Added} = lists:mapfoldl(
        fun(ChildNode, Acc) ->
            {Node, N} = add_leaf_siblings(ChildNode, 0, SiblingFun),
            {Node, Acc + N}
        end,
        0,
        ChildNodes
    ),
    {{Parent, Children}, Count + Added};
add_leaf_siblings({Parent, Leaves} = Node, Count, SiblingFun) ->
    case SiblingFun(Parent) of
        no_sibling -> {Node, Count};
        Siblings -> {{Parent, Leaves ++ Siblings}, Count + length(Siblings)}
    end.

any_leaves(Tree) -> [Leaf || #{valid_policy := ?anyPolicy} = Leaf <- all_leaves(Tree)].

all_leaves({}) -> [];
all_leaves({_, [{_, _} | _] = ChildNodes}) -> lists:flatmap(fun all_leaves/1, ChildNodes);
all_leaves({_, Leaves}) -> Leaves.

map_leaves({Parent, [{_, _} | _] = ChildNodes}, LeafFun) ->
    {Parent, [map_leaves(ChildNode, LeafFun) || ChildNode <- ChildNodes]};
map_leaves({Parent, Leaves}, LeafFun) ->
    {Parent, lists:map(LeafFun, Leaves)}.

prune_leaves({}, _) ->
    {};
prune_leaves({_, _} = Tree, Policy) ->
    filter_leaves(Tree, fun(#{valid_policy := ValidPolicy}) -> ValidPolicy =/= Policy end).

filter_leaves({Parent, [{_, _} | _] = ChildNodes}, Keep) ->
    {Parent, [filter_leaves(ChildNode, Keep) || ChildNode <- ChildNodes]};
filter_leaves({Parent, Leaves}, Keep) ->
    {Parent, lists:filter(Keep, Leaves)}.

%% Subtrees without leaves removed; a tree left without children is empty.
prune_tree({}) ->
    {};
prune_tree({_, []}) ->
    {};
prune_tree({Root, ChildNodes}) ->
    case prune_children(ChildNodes) of
        [] -> {};
        NewChildNodes -> {Root, NewChildNodes}
    end.

prune_children(ChildNodes) ->
    lists:filtermap(
        fun
            ({Parent, Children}) ->
                case prune_children(Children) of
                    [] -> false;
                    NewChildren -> {true, {Parent, NewChildren}}
                end;
            (Leaf) ->
                {true, Leaf}
        end,
        ChildNodes
    ).

prune_invalid_nodes(Tree, []) ->
    Tree;
prune_invalid_nodes({Root, ChildNodes}, InvalidNodes) ->
    case prune_invalid_children(ChildNodes, InvalidNodes) of
        [] -> {};
        NewChildNodes -> {Root, NewChildNodes}
    end.

prune_invalid_children(ChildNodes, InvalidNodes) ->
    lists:filtermap(
        fun
            ({#{valid_policy := ?anyPolicy} = Parent, Children}) ->
                {true, {Parent, prune_invalid_children(Children, InvalidNodes)}};
            ({Parent, Children}) ->
                case lists:member(Parent, InvalidNodes) of
                    true -> false;
                    false -> {true, {Parent, Children}}
                end;
            (#{valid_policy := ?anyPolicy} = Child) ->
                {true, Child};
            (#{} = Child) ->
                not lists:member(Child, InvalidNodes)
        end,
        ChildNodes
    ).

%% The nodes whose parents are anyPolicy nodes, depth first (6.1.5 g iii 1).
valid_policy_node_set({#{valid_policy := ?anyPolicy}, [{_, _} | _] = ChildNodes}) ->
    [Parent || {Parent, _} <- ChildNodes] ++
        lists:foldl(fun(ChildNode, Acc) -> valid_policy_node_set(ChildNode) ++ Acc end, [], ChildNodes);
valid_policy_node_set({#{valid_policy := ?anyPolicy}, Leaves}) when is_list(Leaves) ->
    Leaves;
valid_policy_node_set(_) ->
    [].

%% The anyPolicy leaves, or else the valid policy nodes other than anyPolicy.
constrained_policy_node_set({}) ->
    [];
constrained_policy_node_set(Tree) ->
    case any_leaves(Tree) of
        [] -> constrain(Tree);
        AnyLeaves -> AnyLeaves
    end.

constrain({#{valid_policy := ?anyPolicy}, [{_, _} | _] = ChildNodes}) ->
    [Parent || {#{valid_policy := P} = Parent, _} <- ChildNodes, P =/= ?anyPolicy] ++
        lists:foldl(fun(Child, Acc) -> constrain(Child) ++ Acc end, [], ChildNodes);
constrain({#{valid_policy := ?anyPolicy}, Leaves}) when is_list(Leaves) ->
    [Leaf || #{valid_policy := P} = Leaf <- Leaves, P =/= ?anyPolicy];
constrain(_) ->
    [].

%% The user notices and CPS URIs of the nodes under the child expecting a policy (or anyPolicy).
collect_qualifiers({_, ChildNodes}, Policy) ->
    case collect_children_qualifiers(ChildNodes, Policy) of
        [] -> collect_children_qualifiers(ChildNodes, ?anyPolicy);
        QSet -> QSet
    end.

collect_children_qualifiers([], _) ->
    [];
collect_children_qualifiers([{#{expected_policy_set := Set}, _} = ChildNode | ChildNodes], Policy) ->
    case lists:member(Policy, Set) of
        true -> lists:flatten(children_collect(ChildNode));
        false -> collect_children_qualifiers(ChildNodes, Policy)
    end;
collect_children_qualifiers(ChildNodes, _) ->
    lists:flatten(children_collect(ChildNodes)).

children_collect({Parent, ChildNodes}) -> qualifiers(Parent) ++ children_collect(ChildNodes);
children_collect([{_, _} = ChildNode | ChildNodes]) -> children_collect(ChildNode) ++ children_collect(ChildNodes);
children_collect(Leaves) -> lists:map(fun qualifiers/1, Leaves).

qualifiers(#{qualifier_set := QSet}) -> lists:map(fun qualifier/1, QSet).

qualifier(#'PolicyQualifierInfo'{policyQualifierId = ?'id-qt-unotice', qualifier = Qualifier}) when
    is_tuple(Qualifier)
->
    Qualifier;
qualifier(#'PolicyQualifierInfo'{policyQualifierId = ?'id-qt-cps', qualifier = Qualifier}) ->
    {uri, Qualifier}.
