%% Clause's dist.hrl (plan 30 step DIST4b2a, docs/distribution.md#protocol): the distribution capability flags with
%% OTP's macro names and values (erl_dist_protocol's distribution flags), for carriers and dist_util.
-define(DFLAG_PUBLISHED, 16#01).
-define(DFLAG_ATOM_CACHE, 16#02).
-define(DFLAG_EXTENDED_REFERENCES, 16#04).
-define(DFLAG_DIST_MONITOR, 16#08).
-define(DFLAG_FUN_TAGS, 16#10).
-define(DFLAG_DIST_MONITOR_NAME, 16#20).
-define(DFLAG_HIDDEN_ATOM_CACHE, 16#40).
-define(DFLAG_NEW_FUN_TAGS, 16#80).
-define(DFLAG_EXTENDED_PIDS_PORTS, 16#100).
-define(DFLAG_EXPORT_PTR_TAG, 16#200).
-define(DFLAG_BIT_BINARIES, 16#400).
-define(DFLAG_NEW_FLOATS, 16#800).
-define(DFLAG_UNICODE_IO, 16#1000).
-define(DFLAG_DIST_HDR_ATOM_CACHE, 16#2000).
-define(DFLAG_SMALL_ATOM_TAGS, 16#4000).
-define(DFLAG_UTF8_ATOMS, 16#10000).
-define(DFLAG_MAP_TAG, 16#20000).
-define(DFLAG_BIG_CREATION, 16#40000).
-define(DFLAG_SEND_SENDER, 16#80000).
-define(DFLAG_BIG_SEQTRACE_LABELS, 16#100000).
-define(DFLAG_EXIT_PAYLOAD, 16#400000).
-define(DFLAG_FRAGMENTS, 16#800000).
-define(DFLAG_HANDSHAKE_23, 16#1000000).
-define(DFLAG_UNLINK_ID, 16#2000000).
-define(DFLAG_MANDATORY_25_DIGEST, 16#4000000).
-define(DFLAG_RESERVED, 16#f8000000).
-define(DFLAG_SPAWN, (16#01 bsl 32)).
-define(DFLAG_NAME_ME, (16#02 bsl 32)).
-define(DFLAG_V4_NC, (16#04 bsl 32)).
-define(DFLAG_ALIAS, (16#08 bsl 32)).
-define(DFLAG_ALTACT_SIG, (16#20 bsl 32)).
-define(DFLAG_NATIVE_RECORDS, (16#40 bsl 32)).

% The flags mandatory since OTP 25, which DFLAG_MANDATORY_25_DIGEST stands for.
-define(MANDATORY_DFLAGS_25,
    (?DFLAG_EXTENDED_REFERENCES bor ?DFLAG_FUN_TAGS bor ?DFLAG_EXTENDED_PIDS_PORTS bor ?DFLAG_NEW_FUN_TAGS bor
        ?DFLAG_EXPORT_PTR_TAG bor ?DFLAG_BIT_BINARIES bor ?DFLAG_NEW_FLOATS bor ?DFLAG_UTF8_ATOMS bor
        ?DFLAG_MAP_TAG bor ?DFLAG_BIG_CREATION bor ?DFLAG_HANDSHAKE_23)
).

% The flags mandatory since OTP 26.
-define(MANDATORY_DFLAGS_26, (?DFLAG_V4_NC bor ?DFLAG_UNLINK_ID)).

% Every mandatory flag.
-define(DFLAGS_MANDATORY, (?MANDATORY_DFLAGS_25 bor ?MANDATORY_DFLAGS_26)).

% The handshake protocol versions: 6 since OTP 23, the only one OTP 25 and later speak.
-define(ERL_DIST_VER_6, 6).
-define(ERL_DIST_VER_LOW, ?ERL_DIST_VER_6).
-define(ERL_DIST_VER_HIGH, ?ERL_DIST_VER_6).
