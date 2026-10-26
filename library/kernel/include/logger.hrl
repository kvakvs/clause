%% Clause's logger macros (plan 21 step LIB23, docs/libraries/library.md): the ?LOG_* macros of OTP's kernel/include/logger.hrl,
%% written for Clause. Each checks logger:allow/2 for the calling module first, so arguments of a dropped level are
%% not evaluated, and adds the call's location (mfa, file, line) to the metadata.
-ifndef(LOGGER_HRL).
-define(LOGGER_HRL, true).

-define(LOG_EMERGENCY(A), ?DO_LOG(emergency, [A])).
-define(LOG_EMERGENCY(A, B), ?DO_LOG(emergency, [A, B])).
-define(LOG_EMERGENCY(A, B, C), ?DO_LOG(emergency, [A, B, C])).

-define(LOG_ALERT(A), ?DO_LOG(alert, [A])).
-define(LOG_ALERT(A, B), ?DO_LOG(alert, [A, B])).
-define(LOG_ALERT(A, B, C), ?DO_LOG(alert, [A, B, C])).

-define(LOG_CRITICAL(A), ?DO_LOG(critical, [A])).
-define(LOG_CRITICAL(A, B), ?DO_LOG(critical, [A, B])).
-define(LOG_CRITICAL(A, B, C), ?DO_LOG(critical, [A, B, C])).

-define(LOG_ERROR(A), ?DO_LOG(error, [A])).
-define(LOG_ERROR(A, B), ?DO_LOG(error, [A, B])).
-define(LOG_ERROR(A, B, C), ?DO_LOG(error, [A, B, C])).

-define(LOG_WARNING(A), ?DO_LOG(warning, [A])).
-define(LOG_WARNING(A, B), ?DO_LOG(warning, [A, B])).
-define(LOG_WARNING(A, B, C), ?DO_LOG(warning, [A, B, C])).

-define(LOG_NOTICE(A), ?DO_LOG(notice, [A])).
-define(LOG_NOTICE(A, B), ?DO_LOG(notice, [A, B])).
-define(LOG_NOTICE(A, B, C), ?DO_LOG(notice, [A, B, C])).

-define(LOG_INFO(A), ?DO_LOG(info, [A])).
-define(LOG_INFO(A, B), ?DO_LOG(info, [A, B])).
-define(LOG_INFO(A, B, C), ?DO_LOG(info, [A, B, C])).

-define(LOG_DEBUG(A), ?DO_LOG(debug, [A])).
-define(LOG_DEBUG(A, B), ?DO_LOG(debug, [A, B])).
-define(LOG_DEBUG(A, B, C), ?DO_LOG(debug, [A, B, C])).

-define(LOG(Level, A), ?DO_LOG(Level, [A])).
-define(LOG(Level, A, B), ?DO_LOG(Level, [A, B])).
-define(LOG(Level, A, B, C), ?DO_LOG(Level, [A, B, C])).

%% The location metadata of the macro's call site.
-define(LOCATION, #{mfa => {?MODULE, ?FUNCTION_NAME, ?FUNCTION_ARITY}, line => ?LINE, file => ?FILE}).

-define(DO_LOG(Level, Args),
    case logger:allow(Level, ?MODULE) of
        true -> erlang:apply(logger, macro_log, [?LOCATION, Level | Args]);
        false -> ok
    end
).

-endif.
