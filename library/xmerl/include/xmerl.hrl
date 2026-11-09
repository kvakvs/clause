%% xmerl's records and macros, as OTP 29's xmerl.hrl defines them (plan 21 step MIM3, observed from a module including
%% it: record definitions from its debug information, macros from epp; docs/libraries/xmerl.md).

-record(xmlDecl, {vsn, encoding, standalone, attributes}).

-record(xmlAttribute, {
    name,
    expanded_name = [],
    nsinfo = [],
    namespace = [],
    parents = [],
    pos,
    language = [],
    value,
    normalized
}).

-record(xmlNamespace, {default = [], nodes = []}).

-record(xmlNsNode, {parents = [], pos, prefix, uri = []}).

-record(xmlElement, {
    name,
    expanded_name = [],
    nsinfo = [],
    namespace = #xmlNamespace{},
    parents = [],
    pos,
    attributes = [],
    content = [],
    language = "",
    xmlbase = "",
    elementdef = undeclared
}).

-record(xmlText, {parents = [], pos, language = [], value, type = text}).

-record(xmlComment, {parents = [], pos, language = [], value}).

-record(xmlPI, {name, parents = [], pos, value}).

-record(xmlDocument, {content}).

-record(xmlContext, {
    axis_type = forward,
    context_node,
    context_position = 1,
    nodeset = [],
    bindings = [],
    functions = [],
    namespace = [],
    whole_document
}).

-record(xmlNode, {type = element, node, parents = [], pos = 1}).

-record(xmlObj, {type, value}).

-record(xmerl_fun_states, {event, hook, rules, fetch, cont}).

-record(xmerl_scanner, {
    encoding = undefined,
    standalone = no,
    environment = prolog,
    declarations = [],
    doctype_name,
    doctype_DTD = internal,
    comments = true,
    document = false,
    default_attrs = false,
    rules,
    keep_rules = false,
    namespace_conformant = false,
    xmlbase,
    xmlbase_cache,
    fetch_path = [],
    filename = file_name_unknown,
    validation = off,
    schemaLocation = [],
    space = preserve,
    event_fun,
    hook_fun,
    acc_fun,
    fetch_fun,
    close_fun,
    continuation_fun,
    rules_read_fun,
    rules_write_fun,
    rules_delete_fun,
    user_state,
    fun_states = #xmerl_fun_states{},
    entity_references = [],
    text_decl = false,
    quiet = false,
    col = 1,
    line = 1,
    common_data = [],
    allow_entities = false
}).

-record(xmerl_event, {event, line, col, pos, data}).

-define(space, 32).
-define(cr, 13).
-define(lf, 10).
-define(tab, 9).
-define(whitespace(H), H == ?space; H == ?cr; H == ?lf; H == ?tab).
-define(non_ascii(H), list(H), hd(H) >= 128; integer(H), H >= 128).
-define(non_character(H1, H2), H1 == 255, H2 == 254; H1 == 255, H2 == 255).
-define(strip1, {_, T1, S1} = strip(T, S)).
-define(strip2, {_, T2, S2} = strip(T1, S1)).
-define(strip3, {_, T3, S3} = strip(T2, S2)).
-define(strip4, {_, T4, S4} = strip(T3, S3)).
-define(strip5, {_, T5, S5} = strip(T4, S4)).
-define(strip6, {_, T6, S6} = strip(T5, S5)).
-define(strip7, {_, T7, S7} = strip(T6, S6)).
-define(strip8, {_, T8, S8} = strip(T7, S7)).
-define(strip9, {_, T9, S9} = strip(T8, S8)).
-define(strip10, {_, T10, S10} = strip(T9, S9)).
-define(condstrip1, {_, T1, S1} = condstrip(T, S, false)).
-define(condstrip2, {_, T2, S2} = condstrip(T1, S1, false)).
-define(condstrip3, {_, T3, S3} = condstrip(T2, S2, false)).
-define(condstrip4, {_, T4, S4} = condstrip(T3, S3, false)).
-define(bump_col(N),
    ?dbg("bump_col(~p), US = ~p~n", [N, S0#xmerl_scanner.user_state]),
    S = S0#xmerl_scanner{col = S0#xmerl_scanner.col + N}
).
