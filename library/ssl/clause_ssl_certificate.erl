%% Peer certificate checks of ssl connections (plan 21 step LIB81C1, docs/libraries/ssl.md#verification), written
%% from OTP's documentation and observed results: the trusted certificate and path of a peer's chain as OTP's ssl
%% picks them (cacerts, then partial_chain), public_key's path validation with ssl's verify fun (host names, the
%% user's verify_fun), and the alert a failure sends. LIB81D4a adds the CRL checks of crl_check over crl_cache.
-module(clause_ssl_certificate).
-export([verify/3]).

-include_lib("public_key/include/public_key.hrl").

%% The verdict on a peer's chain (DER certificates, the peer's first): ok, or {alert, Description, Reason}.
verify(Chain, Ssl, Role) ->
    Certs = [{Der, public_key:pkix_decode_cert(Der, otp)} || Der <- Chain],
    {Trusted, Path} = trusted_and_path(Certs, Ssl),
    ServerName = server_name(Ssl, Role),
    Status = fun(Cert, Event) -> crl_status(Cert, Event, Ssl, Certs) end,
    Options = [
        {max_path_length, maps:get(depth, Ssl)},
        {verify_fun, verify_fun(maps:get(verify_fun, Ssl), ServerName, {Ssl, Role, Status})}
        | maps:get(cert_policy_opts, Ssl, [])
    ],
    try public_key:pkix_path_validation(Trusted, [Der || {Der, _} <- Path], Options) of
        {ok, _} -> ok;
        {error, Reason} -> alert(Reason, ServerName, element(2, hd(Certs)))
    catch
        _:Reason -> {alert, 40, Reason}
    end.

%% The name a client checks the server's certificate against: none for servers.
server_name(_, server) ->
    undefined;
server_name(#{server_name_indication := disable}, client) ->
    disable;
server_name(#{server_name_indication := undefined, host := Host}, client) ->
    host_name(Host);
server_name(#{server_name_indication := Name}, client) ->
    host_name(Name).

host_name(Host) when is_atom(Host), Host =/= undefined -> atom_to_list(Host);
host_name(Host) -> Host.

%%--------------------------------------------------------------------------------------------------------------------
%% Trusted certificate and path
%%--------------------------------------------------------------------------------------------------------------------

%% {Trusted, Path}: Trusted a DER certificate or the error atom public_key gives the verify fun; Path the rest of
%% the chain, the root's side first.
trusted_and_path([{_, Peer}] = Chain, Ssl) ->
    case public_key:pkix_is_self_signed(Peer) of
        true -> {selfsigned_peer, Chain};
        false -> incomplete(Chain, Ssl, {unknown_ca, Chain})
    end;
trusted_and_path([Peer | _] = Chain, Ssl) ->
    case ordered(Chain) of
        true ->
            case partial(lists:reverse(Chain), Ssl) of
                {unknown_ca, _} = Unknown -> incomplete(Chain, Ssl, Unknown);
                Found -> Found
            end;
        false ->
            trusted_and_path([Peer], Ssl)
    end.

% Whether each certificate of a chain is issued by the next.
ordered([{_, Cert}, {_, Issuer} = Next | Rest]) ->
    public_key:pkix_is_issuer(Cert, Issuer) andalso ordered([Next | Rest]);
ordered(_) ->
    true.

% A chain completed from cacerts, when that adds certificates.
incomplete([{PeerDer, _} | _] = Chain, Ssl, Default) ->
    Candidate = clause_ssl_options:cert_chain([PeerDer], cacerts(Ssl)),
    case length(Candidate) > length(Chain) andalso lists:prefix([D || {D, _} <- Chain], Candidate) of
        true -> partial(lists:reverse([{D, public_key:pkix_decode_cert(D, otp)} || D <- Candidate]), Ssl);
        false -> Default
    end.

cacerts(Ssl) ->
    case maps:get(cacerts, Ssl, undefined) of
        undefined -> [];
        CaCerts -> CaCerts
    end.

% The trusted root of a path (the root's side first): a sent root in cacerts, else its issuer from cacerts; the
% partial_chain fun may trust a certificate of the path instead.
partial([{TopDer, Top} | Rest] = Path, Ssl) ->
    CaCerts = cacerts(Ssl),
    case public_key:pkix_is_self_signed(Top) of
        true ->
            case lists:member(TopDer, CaCerts) of
                true -> shorten(Path, Ssl, {TopDer, Rest});
                false -> shorten(Path, Ssl, {unknown_ca, Path})
            end;
        false ->
            Issuers = [{D, O} || D <- CaCerts, O <- [public_key:pkix_decode_cert(D, otp)]],
            case [Found || {_, O} = Found <- Issuers, clause_ssl_options:issued_by(TopDer, Top, O)] of
                [{IssuerDer, Issuer} = Found | _] ->
                    Longer = [Found | Path],
                    case public_key:pkix_is_self_signed(Issuer) of
                        true -> shorten(Longer, Ssl, {IssuerDer, Path});
                        false -> shorten(Longer, Ssl, {unknown_ca, Longer})
                    end;
                [] ->
                    shorten(Path, Ssl, {unknown_ca, Path})
            end
    end.

% The user's partial_chain fun choosing a trusted certificate of the path.
shorten(Path, #{partial_chain := PartialChain}, Default) ->
    try PartialChain([Der || {Der, _} <- Path]) of
        {trusted_ca, Root} -> trusted_from(Root, Path, Default);
        _ -> Default
    catch
        _:_ -> Default
    end.

trusted_from(Root, [{Root, _} | Rest], _) -> {Root, Rest};
trusted_from(Root, [_ | Rest], Default) -> trusted_from(Root, Rest, Default);
trusted_from(_, [], Default) -> Default.

%%--------------------------------------------------------------------------------------------------------------------
%% The verify fun
%%--------------------------------------------------------------------------------------------------------------------

%% ssl's verify fun over public_key's events: without a user fun, failures fail, valid certificates pass the CRL
%% status check (LIB81D4a) and the peer's host name is checked; a user fun sees every event, valid_peer turned into
%% the host name failure when the name does not match, and when it accepts a valid certificate a failed status check
%% is shown to it next, as OTP's.
verify_fun(undefined, ServerName, {Ssl, Role, Status}) ->
    Fun = fun
        (_, {extension, _}, State) ->
            {unknown, State};
        (_, {bad_cert, _} = Reason, _) ->
            {fail, Reason};
        (Cert, valid, State) ->
            checked(Status(Cert, valid), {valid, State});
        (Cert, valid_peer, State) ->
            checked(Status(Cert, valid_peer), peer_check(Cert, ServerName, Ssl, Role, State))
    end,
    {Fun, []};
verify_fun({UserFun, UserState0}, ServerName, {Ssl, Role, Status}) ->
    Fun = fun
        (Cert, Event0, UserState) when Event0 == valid; Event0 == valid_peer ->
            Event =
                case Event0 == valid_peer andalso peer_check(Cert, ServerName, Ssl, Role, UserState) of
                    {fail, Reason} -> Reason;
                    _ -> Event0
                end,
            case user_verdict(UserFun(Cert, Event, UserState)) of
                {valid, State} ->
                    case Status(Cert, Event) of
                        valid -> {valid, State};
                        Failure -> user_verdict(UserFun(Cert, Failure, State))
                    end;
                Verdict ->
                    Verdict
            end;
        (Cert, Event, UserState) ->
            user_verdict(UserFun(Cert, Event, UserState))
    end,
    {Fun, UserState0}.

checked(valid, Verdict) -> Verdict;
checked(Failure, _Verdict) -> {fail, Failure}.

user_verdict({valid_peer, State}) -> {valid, State};
user_verdict(Verdict) -> Verdict.

peer_check(Cert, ServerName, Ssl, client, State) when ServerName =/= undefined, ServerName =/= disable ->
    Customize = maps:get(customize_hostname_check, Ssl),
    case public_key:pkix_verify_hostname(Cert, [host_id(ServerName)], Customize) of
        true -> {valid, State};
        false -> {fail, {bad_cert, hostname_check_failed}}
    end;
peer_check(_, _, _, _, State) ->
    {valid, State}.

host_id(Address) when is_tuple(Address) -> {ip, Address};
host_id(Name) ->
    case inet:parse_strict_address(Name) of
        {ok, Address} -> {ip, Address};
        _ -> {dns_id, Name}
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% CRL checks (LIB81D4a)
%%--------------------------------------------------------------------------------------------------------------------

%% A certificate's revocation status under crl_check, as OTP's ssl checks it: valid, or {bad_cert, Reason}. CRLs come
%% from crl_cache by the certificate's distribution points, else by its issuer; with best_effort an undetermined
%% status is valid, and with peer only the peer's certificate is checked.
crl_status(_Cert, _Event, #{crl_check := false}, _Chain) ->
    valid;
crl_status(_Cert, valid, #{crl_check := peer}, _Chain) ->
    valid;
crl_status(Cert, _Event, #{crl_check := Check, crl_cache := {Callback, Handle}} = Ssl, Chain) ->
    Db = {Callback, db_info(Handle)},
    Options = [
        {issuer_fun, {fun(_Point, CRL, _Issuer, _) -> crl_signer(CRL, Chain, Ssl) end, []}},
        {update_crl, fun(Point, CRL) -> unwrap(Callback:fresh_crl(Point, CRL)) end},
        {undetermined_details, true}
    ],
    case points_and_crls(Cert, Db) of
        no_points ->
            by_issuer(Cert, Check, Db, Options);
        PointsAndCRLs ->
            case public_key:pkix_crls_validate(Cert, PointsAndCRLs, Options) of
                {bad_cert, {revocation_status_undetermined, _}} -> by_issuer(Cert, Check, Db, Options);
                Other -> Other
            end
    end.

% The internal cache's handle names Clause's CRL store.
db_info({internal, Args}) -> {clause_ssl_crls:tables(), Args};
db_info(Handle) -> Handle.

% The CRLs of the first distribution point the cache has CRLs for, paired with every point; no_points without any.
points_and_crls(Cert, {Callback, Info}) ->
    case public_key:pkix_dist_points(Cert) of
        [] ->
            no_points;
        Points ->
            Issuer = Cert#'OTPCertificate'.tbsCertificate#'OTPTBSCertificate'.issuer,
            CRLs = point_crls(Points, Issuer, Callback, Info),
            [{Point, {CRL, public_key:der_decode('CertificateList', CRL)}} || Point <- Points, CRL <- CRLs]
    end.

point_crls([], _Issuer, _Callback, _Info) ->
    [];
point_crls([Point | Rest], Issuer, Callback, Info) ->
    Found =
        try
            Callback:lookup(Point, Issuer, Info)
        catch
            error:undef -> Callback:lookup(Point, Info)
        end,
    case unwrap(Found) of
        not_available -> point_crls(Rest, Issuer, Callback, Info);
        CRLs -> CRLs
    end.

% The CRLs of the certificate's issuer, through the distribution point public_key makes of it.
by_issuer(Cert, Check, {Callback, Info}, Options) ->
    #'DistributionPoint'{distributionPoint = {fullName, Names}} = Point = public_key:pkix_dist_point(Cert),
    CRLs = lists:flatmap(
        fun
            ({directoryName, Issuer}) -> unwrap(Callback:select(Issuer, Info));
            (_) -> []
        end,
        Names
    ),
    Result = public_key:pkix_crls_validate(
        Cert, [{Point, {CRL, public_key:der_decode('CertificateList', CRL)}} || CRL <- CRLs], Options
    ),
    case Result of
        {bad_cert, {revocation_status_undetermined, _}} when Check == best_effort -> valid;
        _ -> Result
    end.

% A cache answer without its log event, which is logged.
unwrap({logger, {Level, Report, Metadata}, Value}) ->
    logger:log(Level, Report, Metadata),
    Value;
unwrap(Value) ->
    Value.

% The CRL's signer among the peer's chain and cacerts, by name and signature, with its path from a root:
% {ok, RootDER, [DER]} (the root first), else {error, unknown_ca}.
crl_signer(CRL, Chain, Ssl) ->
    Name = public_key:pkix_normalize_name(public_key:pkix_crl_issuer(CRL)),
    CaCerts =
        case maps:get(cacerts, Ssl, undefined) of
            [_ | _] = Roots -> [{Der, public_key:pkix_decode_cert(Der, otp)} || Der <- Roots];
            _ -> []
        end,
    Candidates = Chain ++ CaCerts,
    Signers = [C || {_, Otp} = C <- Candidates, subject(Otp) == Name, signed_by(CRL, Otp)],
    case Signers of
        [Signer | _] ->
            Path = issuers(Signer, Candidates, [Signer], 10),
            PathDers = [Der || {Der, _} <- Path],
            {ok, lists:last(PathDers), lists:reverse(PathDers)};
        [] ->
            {error, unknown_ca}
    end.

subject(#'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{subject = Subject}}) ->
    public_key:pkix_normalize_name(Subject).

signed_by(CRL, Otp) ->
    try
        public_key:pkix_crl_verify(CRL, Otp)
    catch
        _:_ -> false
    end.

% The certificate's issuers up to a self-signed root among the candidates, the certificate first.
issuers({_, Otp} = Cert, Candidates, Acc, Depth) ->
    Found = [C || {_, Issuer} = C <- Candidates, C =/= Cert, public_key:pkix_is_issuer(Otp, Issuer)],
    case {public_key:pkix_is_self_signed(Otp), Found} of
        {false, [Issuer | _]} when Depth > 0 -> issuers(Issuer, Candidates, [Issuer | Acc], Depth - 1);
        _ -> lists:reverse(Acc)
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% Alerts
%%--------------------------------------------------------------------------------------------------------------------

-define(HANDSHAKE_FAILURE, 40).
-define(BAD_CERTIFICATE, 42).
-define(UNSUPPORTED_CERTIFICATE, 43).
-define(CERTIFICATE_REVOKED, 44).
-define(CERTIFICATE_EXPIRED, 45).
-define(UNKNOWN_CA, 48).

%% The alert a failed validation sends and its reason, as OTP's ssl_handshake picks them.
alert({bad_cert, cert_expired}, _, _) ->
    {alert, ?CERTIFICATE_EXPIRED, undefined};
alert({bad_cert, unknown_ca}, _, _) ->
    {alert, ?UNKNOWN_CA, undefined};
alert({bad_cert, {revoked, _}}, _, _) ->
    {alert, ?CERTIFICATE_REVOKED, undefined};
alert({bad_cert, {revocation_status_undetermined, Details}}, _, _) ->
    {alert, ?BAD_CERTIFICATE, Details};
alert({bad_cert, name_not_permitted}, _, _) ->
    {alert, ?BAD_CERTIFICATE, subject_alt_name_not_permitted};
alert({bad_cert, hostname_check_failed}, ServerName, Peer) ->
    {alert, ?BAD_CERTIFICATE, hostname_reason(ServerName, Peer)};
alert({bad_cert, Reason}, _, _) when
    Reason == invalid_issuer;
    Reason == invalid_signature;
    Reason == distinguished_name_not_permitted;
    Reason == selfsigned_peer;
    Reason == policy_tree_exceeded
->
    {alert, ?BAD_CERTIFICATE, Reason};
alert({bad_cert, Reason}, _, _) when Reason == unsupported_signature; Reason == unknown_critical_extension ->
    {alert, ?UNSUPPORTED_CERTIFICATE, Reason};
alert({bad_cert, {Tag, _} = Reason}, _, _) when
    Tag == invalid_ext_keyusage; Tag == ca_invalid_ext_keyusage; Tag == key_usage_mismatch
->
    {alert, ?UNSUPPORTED_CERTIFICATE, Reason};
alert(Reason, _, _) ->
    {alert, ?HANDSHAKE_FAILURE, Reason}.

hostname_reason(ServerName, Peer) ->
    case subject_alt_names(Peer) of
        [] -> {bad_cert, {hostname_check_failed, missing_subject_altnames}};
        Names -> {bad_cert, {hostname_check_failed, {requested, ServerName}, {received, Names}}}
    end.

subject_alt_names(#'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{extensions = Extensions}}) ->
    case Extensions of
        [_ | _] -> [V || #'Extension'{extnID = ?'id-ce-subjectAltName', extnValue = Names} <- Extensions, V <- Names];
        _ -> []
    end.
