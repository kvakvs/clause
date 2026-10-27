%% OTP 29 ssl types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([active_msgs/0,
              cert_key_conf/0,
              cipher/0,
              cipher_filters/0,
              ciphers/0,
              client_option_cert/0,
              common_option_tls13/0,
              connection_info/0,
              connection_info_keys/0,
              erl_cipher_suite/0,
              error_alert/0,
              group/0,
              hash/0,
              host/0,
              kex_algo/0,
              key/0,
              keylog_info/0,
              named_curve/0,
              old_cipher_suite/0,
              prf_random/0,
              protocol_extensions/0,
              protocol_version/0,
              reason/0,
              server_option_cert/0,
              session_id/0,
              session_ticket/0,
              sign_algo/0,
              sign_scheme/0,
              signature_algs/0,
              socket/0,
              socket_option/0,
              srp_param_type/0,
              sslsocket/0,
              tls_alert/0,
              tls_client_option/0,
              tls_option/0,
              tls_server_option/0]).
-type socket() :: gen_tcp:socket() | socket:socket().
-type socket_option() ::
          gen_tcp:connect_option() |
          gen_tcp:listen_option() |
          gen_udp:option().
-type sslsocket() :: any().
-type tls_option() :: tls_client_option() | tls_server_option().
-type tls_client_option() ::
          client_option() |
          common_option() |
          socket_option() |
          transport_option().
-type tls_server_option() ::
          server_option() |
          common_option() |
          socket_option() |
          transport_option().
-type active_msgs() ::
          {ssl, sslsocket(), Data :: binary() | list()} |
          {ssl_closed, sslsocket()} |
          {ssl_error,
           sslsocket(),
           Alert :: error_alert() | (Reason :: any())} |
          {ssl_passive, sslsocket()}.
-type transport_option() ::
          {cb_info,
           {CallbackModule :: atom(),
            DataTag :: atom(),
            ClosedTag :: atom(),
            ErrTag :: atom()}} |
          {cb_info,
           {CallbackModule :: atom(),
            DataTag :: atom(),
            ClosedTag :: atom(),
            ErrTag :: atom(),
            PassiveTag :: atom()}}.
-type host() :: inet:hostname() | inet:ip_address().
-type session_id() :: binary().
-type protocol_version() :: tls_version() | dtls_version().
-type tls_version() :: 'tlsv1.2' | 'tlsv1.3' | tls_legacy_version().
-type dtls_version() :: 'dtlsv1.2' | dtls_legacy_version().
-type tls_legacy_version() :: tlsv1 | 'tlsv1.1'.
-type dtls_legacy_version() :: dtlsv1.
-type cipher() ::
          aes_256_gcm | aes_128_gcm | aes_256_ccm | aes_128_ccm |
          chacha20_poly1305 | aes_256_ccm_8 | aes_128_ccm_8 |
          aes_128_cbc | aes_256_cbc |
          legacy_cipher().
-type legacy_cipher() :: '3des_ede_cbc' | des_cbc | rc4_128.
-type hash() :: sha2() | legacy_hash().
-type sha2() :: sha512 | sha384 | sha256.
-type legacy_hash() :: sha224 | sha | md5.
-type sign_algo() :: eddsa | ecdsa | rsa | legacy_sign_algo().
-type legacy_sign_algo() :: dsa.
-type sign_scheme() ::
          eddsa_ed25519 | eddsa_ed448 | ecdsa_secp521r1_sha512 |
          ecdsa_secp384r1_sha384 | ecdsa_secp256r1_sha256 |
          ecdsa_brainpoolP512r1tls13_sha512 |
          ecdsa_brainpoolP384r1tls13_sha384 |
          ecdsa_brainpoolP256r1tls13_sha256 |
          post_quantum_schemes() |
          rsassa_pss_scheme() |
          legacy_sign_scheme().
-type rsassa_pss_scheme() ::
          rsa_pss_rsae_sha512 | rsa_pss_rsae_sha384 |
          rsa_pss_rsae_sha256 | rsa_pss_pss_sha512 |
          rsa_pss_pss_sha384 | rsa_pss_pss_sha256.
-type post_quantum_schemes() :: crypto:mldsa() | crypto:slh_dsa().
-type legacy_sign_scheme() ::
          rsa_pkcs1_sha512 | rsa_pkcs1_sha384 | rsa_pkcs1_sha256 |
          ecdsa_sha1 | rsa_pkcs1_sha1.
-type kex_algo() ::
          ecdhe_ecdsa | ecdh_ecdsa | ecdh_rsa | any | legacy_kex().
-type legacy_kex() ::
          rsa | rsa_psk | dhe_rsa | dhe_dss | srp_rsa | srp_dss |
          dhe_psk | psk | ecdh_anon | dh_anon | srp_anon.
-type erl_cipher_suite() ::
          #{key_exchange := kex_algo(),
            cipher := cipher(),
            mac := hash() | aead,
            prf := hash() | default_prf}.
-type old_cipher_suite() ::
          {kex_algo(), cipher(), hash()} |
          {kex_algo(), cipher(), hash() | aead, hash()}.
-type named_curve() ::
          x25519 | x448 | secp521r1 | brainpoolP512r1 |
          brainpoolP384r1 | secp384r1 | brainpoolP256r1 | secp256r1 |
          legacy_named_curve().
-type legacy_named_curve() ::
          sect571r1 | sect571k1 | sect409k1 | sect409r1 | sect283k1 |
          sect283r1 | secp256k1 | sect239k1 | sect233k1 | sect233r1 |
          secp224k1 | secp224r1 | sect193r1 | sect193r2 | secp192k1 |
          secp192r1 | sect163k1 | sect163r1 | sect163r2 | secp160k1 |
          secp160r1 | secp160r2.
-type group() ::
          x25519mlkem768 | secp384r1mlkem1024 | secp256r1mlkem768 |
          mlkem1024 | mlkem768 | mlkem512 | x25519 | x448 | secp256r1 |
          secp384r1 | secp521r1 | brainpoolP512r1tls13 |
          brainpoolP384r1tls13 | brainpoolP256r1tls13 | ffdhe2048 |
          ffdhe3072 | ffdhe4096 | ffdhe6144 | ffdhe8192.
-type srp_param_type() ::
          srp_8192 | srp_6144 | srp_4096 | srp_3072 | srp_2048.
-type error_alert() ::
          {tls_alert, {tls_alert(), Description :: string()}}.
-type tls_alert() ::
          close_notify | unexpected_message | bad_record_mac |
          record_overflow | handshake_failure | bad_certificate |
          unsupported_certificate | certificate_revoked |
          certificate_expired | certificate_unknown |
          illegal_parameter | unknown_ca | access_denied |
          decode_error | decrypt_error | export_restriction |
          protocol_version | insufficient_security | internal_error |
          inappropriate_fallback | user_canceled | no_renegotiation |
          unsupported_extension | certificate_unobtainable |
          unrecognized_name | bad_certificate_status_response |
          bad_certificate_hash_value | unknown_psk_identity |
          no_application_protocol.
-type reason() :: term().
-type common_option() ::
          {protocol, tls | dtls} |
          {handshake, hello | full} |
          {ciphers, cipher_suites()} |
          {signature_algs, signature_algs()} |
          {signature_algs_cert, [sign_scheme()]} |
          {keep_secrets,
           KeepSecrets ::
               boolean() |
               {keylog_hs, fun((Info :: keylog_info()) -> any())} |
               {keylog, fun((Info :: keylog_info()) -> any())}} |
          {max_handshake_size, HandshakeSize :: pos_integer()} |
          {versions, [protocol_version()]} |
          {log_level, Level :: logger:level() | none | all} |
          {hibernate_after, HibernateTimeout :: timeout()} |
          {receiver_spawn_opts,
           SpawnOpts :: [erlang:spawn_opt_option()]} |
          {sender_spawn_opts, SpawnOpts :: [erlang:spawn_opt_option()]}.
-type common_option_cert() ::
          {certs_keys, CertsKeys :: [cert_key_conf()]} |
          {depth, AllowedCertChainLen :: pos_integer()} |
          {verify_fun,
           Verify :: {Verifyfun :: fun(), InitialUserState :: any()}} |
          {cert_policy_opts,
           PolicyOpts ::
               [{policy_set, [public_key:oid()]} |
                {explicit_policy, boolean()} |
                {inhibit_policy_mapping, boolean()} |
                {inhibit_any_policy, boolean()}]} |
          {allow_any_ca_purpose, Allow :: boolean()} |
          {crl_check, Check :: boolean() | peer | best_effort} |
          {crl_cache, crl_cache_opts()} |
          {partial_chain, anchor_fun()}.
-type common_option_pre_tls13() ::
          {eccs, NamedCurves :: [named_curve()]} |
          {secure_renegotiate, SecureRenegotiate :: true} |
          {user_lookup_fun, {Lookupfun :: fun(), UserState :: any()}}.
-type common_option_tls13() ::
          {supported_groups, [group()]} |
          {key_update_at, KeyUpdateAt :: pos_integer()}.
-type common_option_legacy() ::
          {cert,
           Cert :: public_key:der_encoded() | [public_key:der_encoded()]} |
          {certfile, CertPem :: file:filename()} |
          {key, Key :: key()} |
          {keyfile, KeyPem :: file:filename()} |
          {password, KeyPemPasswd :: iodata() | fun(() -> iodata())} |
          {log_alert, LogAlert :: boolean()} |
          {padding_check, PaddingCheck :: boolean()} |
          {beast_mitigation, one_n_minus_one | zero_n | disabled} |
          {ssl_imp, Imp :: new | old}.
-type key() ::
          {'RSAPrivateKey' | 'DSAPrivateKey' | 'ECPrivateKey' |
           'PrivateKeyInfo',
           public_key:der_encoded()} |
          #{algorithm := sign_algo(),
            engine := crypto:engine_ref(),
            key_id := crypto:key_id(),
            password => crypto:password()} |
          #{algorithm := sign_algo(),
            sign_fun := fun(),
            sign_opts => list(),
            encrypt_fun => fun(),
            encrypt_opts => list()}.
-type cert_key_conf() ::
          #{cert =>
                public_key:der_encoded() | [public_key:der_encoded()],
            key => key(),
            certfile => file:filename(),
            keyfile => file:filename(),
            password => iodata() | fun(() -> iodata())}.
-type cipher_suites() :: ciphers().
-type ciphers() :: [erl_cipher_suite()] | string().
-type cipher_filters() ::
          [{key_exchange | cipher | mac | prf,
            fun((kex_algo() | cipher() | hash() | aead | default_prf) ->
                    true | false)}].
-type crl_cache_opts() ::
          {Module :: atom(),
           {DbHandle :: internal | term(), Args :: list()}}.
-type anchor_fun() :: fun().
-type signature_algs() :: [{hash(), sign_algo()} | sign_scheme()].
-type client_option() ::
          client_option_cert() |
          common_option_cert() |
          {psk_groups, [group()]} |
          {alpn_advertised_protocols,
           AppProtocols :: [AppProto :: binary()]} |
          {max_fragment_length,
           MaxLen :: undefined | 512 | 1024 | 2048 | 4096} |
          client_option_tls13() |
          common_option_tls13() |
          client_option_pre_tls13() |
          common_option_pre_tls13() |
          common_option_dtls() |
          client_option_legacy() |
          common_option_legacy().
-type client_option_cert() ::
          {cacerts,
           CACerts ::
               [public_key:der_encoded()] | [public_key:combined_cert()]} |
          {cacertfile, CACertFile :: file:filename()} |
          {server_name_indication, SNI :: inet:hostname() | disable} |
          {customize_hostname_check, HostNameCheckOpts :: list()} |
          {certificate_authorities, boolean()} |
          {stapling, Stapling :: staple | no_staple | map()}.
-nominal session_ticket() :: #{sni := inet:hostname()}.
-type client_option_tls13() ::
          {session_tickets, SessionTickets :: disabled | manual | auto} |
          {use_ticket, Tickets :: [session_ticket()]} |
          {early_data, binary()} |
          {middlebox_comp_mode, MiddleBoxMode :: boolean()}.
-type client_option_pre_tls13() ::
          {reuse_session,
           SessionRef ::
               session_id() | {session_id(), SessionData :: binary()}} |
          {reuse_sessions, Reuse :: boolean() | save} |
          {psk_identity, PskID :: string()} |
          {srp_identity,
           SrpID :: {Username :: string(), Password :: string()}} |
          {fallback, LegacyFallback :: boolean()}.
-type common_option_dtls() ::
          {use_srtp,
           UseSrtp ::
               #{protection_profiles := [binary()], mki => binary()}}.
-type client_option_legacy() ::
          {verify, Verify :: verify_peer | verify_none} |
          {client_preferred_next_protocols,
           NextAppProtocols ::
               {Precedence :: server | client,
                ClientPrefs :: [AppProto :: binary()]} |
               {Precedence :: server | client,
                ClientPrefs :: [AppProto :: binary()],
                Default :: (AppProto :: binary())}}.
-type server_option() ::
          server_option_cert() |
          common_option_cert() |
          {honor_cipher_order, HonorServerCipherOrder :: boolean()} |
          {alpn_preferred_protocols, AppProtocols :: [binary()]} |
          {sni_hosts,
           SNIHosts ::
               [{inet:hostname(), [server_option() | common_option()]}]} |
          {sni_fun,
           SNIFun ::
               fun((string()) ->
                       [server_option() | common_option()] |
                       unrecognized | undefined)} |
          server_option_pre_tls13() |
          common_option_pre_tls13() |
          server_option_tls13() |
          common_option_tls13() |
          common_option_dtls() |
          server_option_legacy() |
          common_option_legacy().
-type server_option_cert() ::
          {cacerts,
           CACerts ::
               [public_key:der_encoded()] | [public_key:combined_cert()]} |
          {cacertfile, CACertFile :: file:filename()} |
          {verify, Verify :: verify_none | verify_peer} |
          {certificate_authorities, ServerCertAuth :: boolean()}.
-type server_option_pre_tls13() ::
          {client_renegotiation, ClientRengotiation :: boolean()} |
          {reuse_sessions, ReuseSessions :: boolean()} |
          {reuse_session, ReuseSession :: fun()} |
          {honor_ecc_order, HonorServerECCOrder :: boolean()} |
          {dh, DHDer :: public_key:der_encoded()} |
          {dhfile, DhFile :: file:filename()} |
          {psk_identity, PSKHint :: string()}.
-type server_option_tls13() ::
          {session_tickets,
           SessionTickets ::
               disabled | stateful | stateless | stateful_with_cert |
               stateless_with_cert} |
          {stateless_tickets_seed, TicketSeed :: binary()} |
          {anti_replay,
           '10k' | '100k' |
           {BloomFilterWindowSize :: pos_integer(),
            BloomFilterHashFunctions :: pos_integer(),
            BloomFilterBits :: pos_integer()}} |
          {cookie, Cookie :: boolean()} |
          {early_data, EarlyData :: enabled | disabled}.
-type server_option_legacy() ::
          {fail_if_no_peer_cert, FailNoPeerCert :: boolean()} |
          {next_protocols_advertised, NextAppProtocols :: [binary()]}.
-type prf_random() :: client_random | server_random.
-type protocol_extensions() ::
          #{renegotiation_info => binary(),
            signature_algs => signature_algs(),
            alpn => binary(),
            srp => binary(),
            next_protocol => binary(),
            max_frag_enum => 1..4,
            ec_point_formats => [0..2],
            elliptic_curves => [public_key:oid()],
            sni => inet:hostname()}.
-type connection_info() ::
          [{protocol, protocol_version()} |
           {session_resumption, boolean()} |
           {selected_cipher_suite, erl_cipher_suite()} |
           {selected_group, group()} |
           {sni_hostname, term()} |
           {ciphers, [erl_cipher_suite()]}] |
          connection_info_pre_tls13() |
          security_info().
-type security_info() ::
          [{client_random, binary()} |
           {server_random, binary()} |
           {master_secret, binary()} |
           {keylog, [keylog_item()]}].
-type keylog_item() :: unicode:chardata().
-type keylog_info() ::
          #{items => [keylog_item()], client_random => binary()}.
-type connection_info_pre_tls13() ::
          [{session_id, session_id()} |
           {session_data, binary()} |
           {ecc, {named_curve, term()}} |
           {srp_username, term()}].
-type connection_info_keys() ::
          [protocol | selected_cipher_suite | selected_group |
           sni_hostname | session_resumption | ciphers | client_random |
           server_random | master_secret | keylog | session_id |
           session_data | ecc | srp_username].
