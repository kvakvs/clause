%% eldap's records, as OTP 29's eldap.hrl defines them (plan 21 step MIM2, observed from a module including it: record
%% definitions from its debug information, macros from epp; docs/libraries/eldap.md).

-ifndef(_ELDAP_HRL).
-define(_ELDAP_HRL, 1).

-record(eldap_search, {
    base = [],
    filter = [],
    size_limit = 0,
    scope = wholeSubtree,
    deref = derefAlways,
    attributes = [],
    types_only = false,
    timeout = 0
}).

-record(eldap_search_result, {entries = [], referrals = [], controls = []}).

-record(eldap_entry, {object_name = "", attributes = []}).

-endif.
