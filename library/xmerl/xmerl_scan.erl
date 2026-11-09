%% OTP's xmerl_scan: an XML document or element scanned from a string into xmerl's records (plan 21 step MIM3,
%% docs/libraries/xmerl.md#xmerl_scan), as OTP 29 scans it without a DTD: its text nodes, positions, languages,
%% namespaces and xmlbase, its fatal errors and their reports. Hooks, rules, fetching, validation and entity
%% declarations are declined.
-module(xmerl_scan).
-include("include/xmerl.hrl").
-include("xmerl_scan.types.hrl").
-export([string/1, string/2]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    accumulate_whitespace/4,
    cont_state/1,
    cont_state/2,
    event_state/1,
    event_state/2,
    fetch_state/1,
    fetch_state/2,
    file/1,
    file/2,
    hook_state/1,
    hook_state/2,
    rules_state/1,
    rules_state/2,
    user_state/1,
    user_state/2
]).

-define(XML_NAMESPACE, 'http://www.w3.org/XML/1998/namespace').
% Whitespace as one guard expression, which combines with other guards (xmerl.hrl's ?whitespace is a guard
% sequence).
-define(WS(C), (C =:= $\s orelse C =:= $\t orelse C =:= $\n orelse C =:= $\r)).

% The scan: the whole input (for error positions), the options, and the xmlbase with its one cached value.
-record(scan, {
    input,
    utf8 = true,
    space = preserve,
    comments = true,
    quiet = false,
    conformant = false,
    document = false,
    base,
    cache
}).

% An element being scanned: its name, parents, language and namespace, for its content and attributes.
-record(open, {name, parents, language, namespace}).

string(Text) ->
    string(Text, []).

string(Text, Options) when is_list(Text) ->
    S0 = lists:foldl(fun option/2, #scan{input = Text}, Options),
    S = base(S0),
    {T1, Encoding} = declaration(Text, S),
    S1 = S#scan{utf8 = utf8(Encoding, S)},
    {Prolog, T2} = misc(T1, [], S1),
    T3 = doctype(T2, S1),
    {Prolog2, T4} = misc(T3, Prolog, S1),
    {Root, _, T5} = element(
        T4, #open{name = undefined, parents = [], language = [], namespace = #xmlNamespace{}}, length(Prolog2) + 1, S1
    ),
    {Epilog, T6} = misc(T5, [], S1),
    result(Prolog2, Root, Epilog, T6, S1).

result(Prolog, Root, Epilog, Rest, #scan{document = true}) ->
    Content = number(Prolog ++ [Root] ++ Epilog, 1),
    {#xmlDocument{content = Content}, Rest};
result(_, Root, _, Rest, _) ->
    {Root, Rest}.

% Top-level comments and processing instructions take their positions in the document.
number([], _) -> [];
number([#xmlElement{} = E | Rest], N) -> [E#xmlElement{pos = N} | number(Rest, N + 1)];
number([#xmlComment{} = C | Rest], N) -> [C#xmlComment{pos = N} | number(Rest, N + 1)];
number([#xmlPI{} = P | Rest], N) -> [P#xmlPI{pos = N} | number(Rest, N + 1)].

option({space, Space}, S) when Space =:= preserve; Space =:= normalize -> S#scan{space = Space};
option({comments, Comments}, S) when is_boolean(Comments) -> S#scan{comments = Comments};
option({quiet, Quiet}, S) when is_boolean(Quiet) -> S#scan{quiet = Quiet};
option({xmlbase, Base}, S) ->
    S#scan{base = Base};
option({namespace_conformant, Conformant}, S) when is_boolean(Conformant) -> S#scan{conformant = Conformant};
option({document, Document}, S) when is_boolean(Document) -> S#scan{document = Document};
option({encoding, Encoding}, S) ->
    S#scan{utf8 = Encoding};
option({validation, off}, S) ->
    S;
option({Name, _} = Option, _) when
    Name =:= acc_fun;
    Name =:= hook_fun;
    Name =:= event_fun;
    Name =:= fetch_fun;
    Name =:= fetch_path;
    Name =:= close_fun;
    Name =:= continuation_fun;
    Name =:= rules;
    Name =:= user_state;
    Name =:= validation;
    Name =:= allow_entities;
    Name =:= default_attrs;
    Name =:= schemaLocation;
    Name =:= line;
    Name =:= text_decl;
    Name =:= keep_rules
->
    erlang:error({not_implemented, {option, Option}});
option(Option, S) ->
    erlang:error(function_clause, [Option, S]).

% The initial xmlbase: the option's, or the current directory.
base(#scan{base = undefined} = S) ->
    {ok, Cwd} = file:get_cwd(),
    S#scan{base = Cwd};
base(S) ->
    S.

% The encoding the option or the XML declaration names decides whether bytes are UTF-8.
utf8(Encoding, #scan{utf8 = true}) -> utf8_name(Encoding);
utf8(_, #scan{utf8 = Option}) -> utf8_name(Option).

utf8_name(undefined) -> true;
utf8_name(true) -> true;
utf8_name(Name) -> not lists:member(string:lowercase(Name), ["iso-8859-1", "iso_8859-1", "latin1", "iso-latin-1"]).

%% --- The XML declaration ---

% <?xml version=... encoding=... standalone=...?> at the very start; its encoding name.
declaration("<?xml" ++ [C | T], S) when ?WS(C) ->
    {Attributes, T1} = declaration_attributes(T, [], S),
    Encoding = proplists:get_value(encoding, Attributes),
    is_list(Encoding) andalso string:lowercase(Encoding) =:= "utf-16" andalso
        fatal(expected_element_start_tag, 3981, T1, S),
    {T1, Encoding};
declaration(T, _) ->
    {T, undefined}.

declaration_attributes("?>" ++ T, Acc, _) ->
    {Acc, T};
declaration_attributes([C | T], Acc, S) when ?WS(C) ->
    declaration_attributes(T, Acc, S);
declaration_attributes([], _, S) ->
    fatal(unexpected_end, 4192, [], S);
declaration_attributes(T, Acc, S) ->
    {Name, T1} = name(T, S),
    {Value, T2} = declaration_value(skip(T1), S),
    declaration_attributes(T2, [{list_to_atom(Name), Value} | Acc], S).

declaration_value("=" ++ T, _) ->
    [Q | T1] = skip(T),
    {Value, [Q | T2]} = lists:splitwith(fun(C) -> C =/= Q end, T1),
    {Value, T2};
declaration_value(T, S) ->
    fatal(assignment_expected, 2999, T, S).

%% --- Comments, processing instructions and the document type outside the root ---

% Whitespace, comments and processing instructions before or after the root, the kept ones in order.
misc([C | T], Acc, S) when ?WS(C) ->
    misc(T, Acc, S);
misc("<!--" ++ T, Acc, S) ->
    {Comment, T1} = comment(T, [], S),
    misc(T1, Acc ++ top_comment(Comment, S), S);
misc("<?" ++ T, Acc, S) ->
    {PI, T1} = pi(T, [], S),
    misc(T1, Acc ++ [PI], S);
misc(T, Acc, _) ->
    {Acc, T}.

top_comment(Value, #scan{comments = true}) -> [#xmlComment{value = Value}];
top_comment(_, _) -> [].

% <!DOCTYPE Name ExternalID? [internal subset]?>: markup declarations are skipped, entity declarations refused.
doctype("<!DOCTYPE" ++ T, S) ->
    {_, T1} = name(skip(T), S),
    {System, T2} = external_id(skip(T1)),
    external_dtd(System, S),
    T3 = internal_subset(skip(T2), S),
    case skip(T3) of
        ">" ++ T4 -> T4;
        T4 -> fatal(unexpected_end, 4192, T4, S)
    end;
doctype(T, _) ->
    T.

external_id("SYSTEM" ++ T) ->
    literal(skip(T));
external_id("PUBLIC" ++ T) ->
    {_, T1} = literal(skip(T)),
    literal(skip(T1));
external_id(T) ->
    {undefined, T}.

literal([Q | T]) when Q =:= $"; Q =:= $' ->
    {Value, [Q | T1]} = lists:splitwith(fun(C) -> C =/= Q end, T),
    {Value, T1}.

% An external DTD is read from the xmlbase as OTP's would be; a missing one fails as OTP's fetch does.
external_dtd(undefined, _) ->
    ok;
external_dtd(System, #scan{base = Base}) ->
    case file:read_file_info(filename:join(Base, System)) of
        {ok, _} -> ok;
        Error -> erlang:error({case_clause, Error})
    end.

internal_subset("[" ++ T, S) ->
    declarations(T, S);
internal_subset(T, _) ->
    T.

declarations("]" ++ T, _) ->
    T;
declarations("<!ENTITY" ++ T, S) ->
    fatal({error, entities_not_allowed}, 1754, T, S);
declarations("<!--" ++ T, S) ->
    {_, T1} = comment(T, [], S#scan{comments = false}),
    declarations(T1, S);
declarations("<" ++ T, S) ->
    declarations(declaration_end(T, S), S);
declarations([_ | T], S) ->
    declarations(T, S);
declarations([], S) ->
    fatal(unexpected_end, 4192, [], S).

% The rest after a markup declaration's '>', quoted values passed over.
declaration_end(">" ++ T, _) ->
    T;
declaration_end([Q | T], S) when Q =:= $"; Q =:= $' ->
    {_, [Q | T1]} = lists:splitwith(fun(C) -> C =/= Q end, T),
    declaration_end(T1, S);
declaration_end([_ | T], S) ->
    declaration_end(T, S);
declaration_end([], S) ->
    fatal(unexpected_end, 4192, [], S).

%% --- Elements ---

% An element at T (its '<' next), child of Parent at position Pos: {Element, Scan, Rest}.
element("<" ++ T, Parent, Pos, S) ->
    {Name, T1} = name(T, S),
    {Attributes0, T2} = attributes(T1, [], S),
    unique(Attributes0, T2, S),
    Self = open(list_to_atom(Name), Pos, Attributes0, Parent, S),
    {Base, S1} = enter(Attributes0, S),
    Attributes = [
        attribute(A, I, Self, Parent, S)
     || {A, I} <- lists:zip(Attributes0, lists:seq(1, length(Attributes0)))
    ],
    Element = #xmlElement{
        name = Self#open.name,
        expanded_name = expanded(Name, Self#open.namespace, T2, S),
        nsinfo = nsinfo(Name),
        namespace = Self#open.namespace,
        parents = Parent#open.parents,
        pos = Pos,
        attributes = Attributes,
        language = Self#open.language,
        xmlbase = Base
    },
    {Content, T3} = element_content(T2, Self, Name, S1),
    {Element#xmlElement{content = Content}, S1#scan{base = S1#scan.cache}, T3};
element(T, _, _, S) ->
    fatal(expected_element_start_tag, 3981, T, S).

element_content("/>" ++ T, _, _, _) ->
    {[], T};
element_content(">" ++ T, Self, Name, S) ->
    content(T, Self, Name, {[], 1, false}, S).

% The element's own scope: its name and position among the parents, its language and namespace.
open(Atom, Pos, Attributes, Parent, _) ->
    Language =
        case lists:keyfind("xml:lang", 1, Attributes) of
            {_, Lang} -> Lang;
            false -> Parent#open.language
        end,
    #open{
        name = Atom,
        parents = [{Atom, Pos} | Parent#open.parents],
        language = Language,
        namespace = lists:foldl(fun declare/2, Parent#open.namespace, Attributes)
    }.

declare({"xmlns", URI}, NS) ->
    NS#xmlNamespace{default = list_to_atom(URI)};
declare({"xmlns:" ++ Prefix, URI}, #xmlNamespace{nodes = Nodes} = NS) ->
    NS#xmlNamespace{nodes = lists:keystore(Prefix, 1, Nodes, {Prefix, list_to_atom(URI)})};
declare(_, NS) ->
    NS.

% An element with attributes caches the xmlbase before applying its xml:base; closing any element restores the
% cache (OTP's single cache).
enter([], #scan{base = Base} = S) ->
    {Base, S};
enter(Attributes, #scan{base = Base} = S) ->
    New =
        case lists:keyfind("xml:base", 1, Attributes) of
            {_, Value} -> filename:join(Base, Value);
            false -> Base
        end,
    {New, S#scan{base = New, cache = Base}}.

attribute({Name, Value, Normalized}, Pos, Self, Parent, S) ->
    attribute({Name, Value}, Pos, Self, Parent, S, Normalized);
attribute({Name, Value}, Pos, Self, Parent, S) ->
    attribute({Name, Value}, Pos, Self, Parent, S, false).

attribute({Name, Value}, Pos, Self, Parent, #scan{conformant = Conformant} = S, Normalized) ->
    #xmlAttribute{
        name = list_to_atom(Name),
        expanded_name = attribute_expanded(Name, Self#open.namespace, S),
        nsinfo = nsinfo(Name),
        namespace = conformant_value(Conformant, Self#open.namespace),
        parents = Self#open.parents,
        pos = Pos,
        language = Parent#open.language,
        value = Value,
        normalized = Normalized
    }.

conformant_value(true, Value) -> Value;
conformant_value(false, _) -> [].

nsinfo(Name) ->
    case string:split(Name, ":") of
        [Prefix, Local] -> {Prefix, Local};
        _ -> []
    end.

% The element's expanded name: {URI, Local} under namespace_conformant, else its name.
expanded(Name, _, _, #scan{conformant = false}) ->
    list_to_atom(Name);
expanded(Name, #xmlNamespace{default = Default} = NS, T, S) ->
    case nsinfo(Name) of
        {Prefix, Local} -> {uri(Prefix, NS, T, S), list_to_atom(Local)};
        [] when Default =:= [] -> list_to_atom(Name);
        [] -> {Default, list_to_atom(Name)}
    end.

attribute_expanded(_, _, #scan{conformant = false}) ->
    [];
attribute_expanded("xmlns", _, _) ->
    xmlns;
attribute_expanded("xmlns:" ++ Prefix, _, _) ->
    {"xmlns", Prefix};
attribute_expanded(Name, NS, S) ->
    case nsinfo(Name) of
        {Prefix, Local} -> {uri(Prefix, NS, [], S), list_to_atom(Local)};
        [] -> list_to_atom(Name)
    end.

uri("xml", _, _, _) ->
    ?XML_NAMESPACE;
uri(Prefix, #xmlNamespace{nodes = Nodes}, T, S) ->
    case lists:keyfind(Prefix, 1, Nodes) of
        {_, URI} -> URI;
        false -> fatal({namespace_prefix_not_declared, Prefix}, 2406, T, S)
    end.

unique(Attributes, T, S) ->
    Names = [element(1, A) || A <- Attributes],
    case Names -- lists:usort(Names) of
        [] -> ok;
        [Name | _] -> fatal({error, {unique_att_spec_required, list_to_atom(Name)}}, 3481, T, S)
    end.

%% --- Attributes ---

% The attributes up to '>' or '/>', each {Name, Value} or {Name, Value, true} once normalized.
attributes(T0, Acc, S) ->
    case skip_counted(T0) of
        {_, ">" ++ _ = T} -> {lists:reverse(Acc), T};
        {_, "/>" ++ _ = T} -> {lists:reverse(Acc), T};
        {_, []} -> fatal(unexpected_end, 4192, [], S);
        {_, "/"} -> fatal(unexpected_end, 4192, [], S);
        {0, T} when Acc =/= [] -> fatal({whitespace_required_between_attributes}, 3521, T, S);
        {_, T} -> attributes_next(T, Acc, S)
    end.

attributes_next(T, Acc, S) ->
    {Name, T1} = name(T, S),
    case skip(T1) of
        "=" ++ T2 ->
            {Value, T3} = attribute_value(skip(T2), S),
            attributes(T3, [setelement(1, Value, Name) | Acc], S);
        T2 ->
            fatal(assignment_expected, 2999, T2, S)
    end.

attribute_value([Q | T], S) when Q =:= $"; Q =:= $' ->
    value(T, Q, [], false, false, S);
attribute_value(T, S) ->
    erlang:error(function_clause, [T, S]).

% The value up to its quote Q: whitespace becomes a space; after a whitespace character reference, whitespace is
% dropped until other text (and the value counts as normalized), as OTP's.
value([Q | T], Q, Acc, _, Normalized, _) ->
    {value_result(lists:reverse(Acc), Normalized), T};
value([], _, _, _, _, S) ->
    fatal(unexpected_end, 4192, [], S);
value("<" ++ T, _, _, _, _, S) ->
    fatal(unexpected_char, 2501, T, S);
value([C | T], Q, Acc, true, _, S) when ?WS(C) ->
    value(T, Q, Acc, true, true, S);
value([C | T], Q, Acc, _, Normalized, S) when ?WS(C) ->
    value(T, Q, [$\s | Acc], false, Normalized, S);
value("&" ++ T, Q, Acc, AfterRef, Normalized, S) ->
    case reference(T, S) of
        {char, C, T1} when ?WS(C), AfterRef -> value(T1, Q, Acc, true, true, S);
        {char, C, T1} when ?WS(C) -> value(T1, Q, [C | Acc], true, Normalized, S);
        {_, C, T1} -> value(T1, Q, [C | Acc], false, Normalized, S)
    end;
value(T, Q, Acc, _, Normalized, S) ->
    {C, T1} = char(T, S),
    value(T1, Q, [C | Acc], false, Normalized, S).

value_result(Value, true) -> {undefined, Value, true};
value_result(Value, false) -> {undefined, Value}.

%% --- Content ---

% The content of Self up to its end tag. Acc is {Nodes (reversed), next position, whether a non-text node came}:
% until one comes, text pieces merge into one node.
content("</" ++ T, _, Name, {Nodes, _, _}, S) ->
    {EndName, T1} = name(T, S),
    case skip(T1) of
        ">" ++ T2 when EndName =:= Name ->
            {lists:reverse(Nodes), T2};
        ">" ++ _ ->
            Reason = {endtag_does_not_match, {was, list_to_atom(EndName), should_have_been, list_to_atom(Name)}},
            fatal(Reason, 2626, T1, S);
        T2 ->
            fatal(unexpected_end, 4192, T2, S)
    end;
content("<!--" ++ T, Self, Name, Acc, S) ->
    {Value, T1} = comment(T, [], S),
    content(skip(T1), Self, Name, add_comment(Value, Self, Acc, S), S);
content("<?" ++ T, Self, Name, {Nodes, Pos, _}, S) ->
    {PI, T1} = pi(T, Self#open.parents, S),
    content(T1, Self, Name, {[PI#xmlPI{pos = Pos} | Nodes], Pos + 1, true}, S);
content("<![CDATA[" ++ T, Self, Name, Acc, S) ->
    {Value, T1} = cdata(T, [], S),
    content(T1, Self, Name, add_text(Value, cdata, Self, Acc), S);
content("<" ++ _ = T, Self, Name, {Nodes, Pos, _}, S) ->
    {Child, S1, T1} = element(T, Self, Pos, S),
    content(T1, Self, Name, {[Child | Nodes], Pos + 1, true}, S1);
content([], _, _, _, S) ->
    fatal(unexpected_end, 4192, [], S);
content(T, Self, Name, Acc, S) ->
    {Value, T1} = text(T, S),
    content(T1, Self, Name, add_text(Value, text, Self, Acc), S).

add_comment(Value, Self, {Nodes, Pos, _}, #scan{comments = true}) ->
    {[#xmlComment{parents = Self#open.parents, pos = Pos, value = Value} | Nodes], Pos + 1, true};
add_comment(_, _, Acc, _) ->
    Acc.

% A text piece: merged into the text node before it until a non-text node came, else a node of its own.
add_text(Value, _, _, {[#xmlText{value = Before} = Text | Nodes], Pos, false}) ->
    {[Text#xmlText{value = Before ++ Value} | Nodes], Pos, false};
add_text(Value, Type, Self, {Nodes, Pos, Seen}) ->
    {[#xmlText{parents = Self#open.parents, pos = Pos, value = Value, type = Type} | Nodes], Pos + 1, Seen}.

% A text piece: a reference with the text after it, or text up to markup or a reference.
text("&" ++ T, S) ->
    {_, C, T1} = reference(T, S),
    {Rest, T2} = run(T1, [], S),
    {text_char(C, S) ++ Rest, T2};
text(T, S) ->
    run(T, [], S).

text_char(C, #scan{space = normalize}) when ?WS(C) -> " ";
text_char(13, _) -> "\n";
text_char(C, _) -> [C].

run([], Acc, S) ->
    {space(lists:reverse(Acc), S), []};
run("<" ++ _ = T, Acc, S) ->
    {space(lists:reverse(Acc), S), T};
run("&" ++ _ = T, Acc, S) ->
    {space(lists:reverse(Acc), S), T};
run("]]>" ++ T, _, S) ->
    fatal(unexpected_cdata_end, 2762, T, S);
run("\r\n" ++ T, Acc, S) ->
    run(T, [$\n | Acc], S);
run("\r" ++ T, Acc, S) ->
    run(T, [$\n | Acc], S);
run(T, Acc, S) ->
    {C, T1} = char(T, S),
    run(T1, [C | Acc], S).

% Under space normalize, each run of whitespace becomes one space.
space(Text, #scan{space = preserve}) -> Text;
space(Text, #scan{space = normalize}) -> collapse(Text).

collapse([C | T]) when ?WS(C) ->
    [$\s | collapse(lists:dropwhile(fun is_whitespace/1, T))];
collapse([C | T]) ->
    [C | collapse(T)];
collapse([]) ->
    [].

is_whitespace(C) when ?WS(C) -> true;
is_whitespace(_) -> false.

cdata("]]>" ++ T, Acc, _) ->
    {lists:reverse(Acc), T};
cdata([], _, S) ->
    fatal(unexpected_end, 4192, [], S);
cdata(T, Acc, S) ->
    {C, T1} = char(T, S),
    cdata(T1, [C | Acc], S).

% A comment's text up to "-->"; "--" inside it is an error.
comment("-->" ++ T, Acc, _) ->
    {lists:reverse(Acc), T};
comment("--" ++ [C | _], _, S) ->
    fatal({invalid_comment, [$-, $-, C]}, 3949, [], S);
comment([], _, S) ->
    fatal(unexpected_end, 4192, [], S);
comment(T, Acc, S) ->
    {C, T1} = char(T, S),
    comment(T1, [C | Acc], S).

% A processing instruction after "<?": its target (not xml) and its data after whitespace.
pi(T, Parents, S) ->
    {Target, T1} = name(T, S),
    string:lowercase(Target) =:= "xml" andalso fatal({invalid_target_name, lists:sublist(T1, 10)}, 1183, T1, S),
    case T1 of
        "?>" ++ T2 ->
            {#xmlPI{name = list_to_atom(Target), parents = Parents, value = []}, T2};
        [C | T2] when ?WS(C) ->
            {Value, T3} = pi_data(skip(T2), [], S),
            {#xmlPI{name = list_to_atom(Target), parents = Parents, value = Value}, T3};
        [C | _] ->
            fatal({expected_whitespace_OR_end_of_PI, {char, C}}, 1212, T1, S);
        [] ->
            fatal(unexpected_end, 4192, [], S)
    end.

pi_data("?>" ++ T, Acc, _) ->
    {lists:reverse(Acc), T};
pi_data([], _, S) ->
    fatal(unexpected_end, 4192, [], S);
pi_data(T, Acc, S) ->
    {C, T1} = char(T, S),
    pi_data(T1, [C | Acc], S).

%% --- References, names and characters ---

% After '&': {char, C, Rest} for a character reference, {entity, C, Rest} for a predefined entity.
reference("#x" ++ T, S) ->
    {Digits, T1} = lists:splitwith(fun(C) -> C =/= $; end, T),
    char_reference(list_to_integer(Digits, 16), T1, S);
reference("#" ++ T, S) ->
    case lists:splitwith(fun(C) -> C >= $0 andalso C =< $9 end, T) of
        {[], _} -> fatal(invalid_char_ref, 2842, T, S);
        {Digits, T1} -> char_reference(list_to_integer(Digits), T1, S)
    end;
reference(T, S) ->
    {Name, T1} =
        try
            name(T, S#scan{quiet = S#scan.quiet})
        catch
            exit:{fatal, _} -> fatal(error_scanning_entity_ref, 2849, T, S)
        end,
    case {entity(Name), T1} of
        {undefined, _} ->
            report({unknown_entity_ref, list_to_atom(Name)}, 2937, S),
            fatal(error_scanning_entity_ref, 2849, T1, S);
        {C, ";" ++ T2} ->
            {entity, C, T2};
        _ ->
            fatal(error_scanning_entity_ref, 2849, T1, S)
    end.

char_reference(C, ";" ++ T, S) ->
    legal(C) orelse fatal({error, {wfc_Legal_Character, C}}, 3505, T, S),
    {char, C, T};
char_reference(_, T, S) ->
    fatal(invalid_char_ref, 2842, T, S).

entity("amp") -> $&;
entity("lt") -> $<;
entity("gt") -> $>;
entity("quot") -> $";
entity("apos") -> $';
entity(_) -> undefined.

% A name at T: its characters and the rest; {invalid_name, T} when none starts there.
name([C | _] = T, S) ->
    case name_start(C) of
        true -> name_chars(T, [], S);
        false -> fatal({invalid_name, lists:sublist(T, 6)}, 3074, T, S)
    end;
name([], S) ->
    fatal(unexpected_end, 4192, [], S).

name_chars([C | _] = T, Acc, S) when C >= 16#80 ->
    {D, T1} = char(T, S),
    name_chars(T1, [D | Acc], S);
name_chars([C | T], Acc, S) ->
    case name_start(C) orelse (C >= $0 andalso C =< $9) orelse C =:= $. orelse C =:= $- of
        true -> name_chars(T, [C | Acc], S);
        false -> {lists:reverse(Acc), [C | T]}
    end;
name_chars([], Acc, _) ->
    {lists:reverse(Acc), []}.

name_start(C) ->
    (C >= $a andalso C =< $z) orelse (C >= $A andalso C =< $Z) orelse C =:= $_ orelse C =:= $: orelse C >= 16#80.

% One character: a UTF-8 sequence decoded unless the encoding is Latin-1; illegal characters are errors.
char([B | T], #scan{utf8 = true} = S) when B >= 16#80 ->
    case decode([B | T]) of
        {ok, C, T1} -> legal_char(C, T1, S);
        error -> fatal({error, {wfc_Legal_Character, {error, {bad_character, B}}}}, 3498, T, S)
    end;
char([C | T], S) ->
    legal_char(C, T, S).

legal_char(C, T, S) ->
    legal(C) orelse fatal({error, {wfc_Legal_Character, C}}, 3498, T, S),
    {C, T}.

decode([B0, B1 | T]) when B0 >= 16#C2, B0 < 16#E0, B1 band 16#C0 =:= 16#80 ->
    {ok, ((B0 band 16#1F) bsl 6) bor (B1 band 16#3F), T};
decode([B0, B1, B2 | T]) when B0 >= 16#E0, B0 < 16#F0, B1 band 16#C0 =:= 16#80, B2 band 16#C0 =:= 16#80 ->
    {ok, ((B0 band 16#0F) bsl 12) bor ((B1 band 16#3F) bsl 6) bor (B2 band 16#3F), T};
decode([B0, B1, B2, B3 | T]) when
    B0 >= 16#F0, B0 < 16#F5, B1 band 16#C0 =:= 16#80, B2 band 16#C0 =:= 16#80, B3 band 16#C0 =:= 16#80
->
    {ok, ((B0 band 16#07) bsl 18) bor ((B1 band 16#3F) bsl 12) bor ((B2 band 16#3F) bsl 6) bor (B3 band 16#3F), T};
decode(_) ->
    error.

legal(C) when C =:= 16#9; C =:= 16#A; C =:= 16#D -> true;
legal(C) when C >= 16#20, C =< 16#D7FF -> true;
legal(C) when C >= 16#E000, C =< 16#FFFD -> true;
legal(C) when C >= 16#10000, C =< 16#10FFFF -> true;
legal(_) -> false.

skip([C | T]) when ?WS(C) -> skip(T);
skip(T) -> T.

skip_counted(T) ->
    Rest = skip(T),
    {length(T) - length(Rest), Rest}.

%% --- Errors ---

% A fatal error, reported as OTP reports it (Line is OTP's source line, part of its report) unless quiet; the
% position is where the scan was.
fatal(Reason, Line, Rest, #scan{input = Input} = S) ->
    report(Reason, Line, S),
    {Row, Col} = position(lists:sublist(Input, max(0, length(Input) - length(Rest)))),
    exit({fatal, {Reason, {file, file_name_unknown}, {line, Row}, {col, Col}}}).

report(_, _, #scan{quiet = true}) ->
    ok;
report(Reason, Line, _) ->
    error_logger:error_msg("~w- fatal: ~p~n", [Line, Reason]).

position(Scanned) ->
    Lines = string:split(Scanned, "\n", all),
    {length(Lines), length(lists:last(Lines)) + 1}.

accumulate_whitespace(_, _, _, _) -> erlang:error({not_implemented, accumulate_whitespace, 4}).
cont_state(_) -> erlang:error({not_implemented, cont_state, 1}).
cont_state(_, _) -> erlang:error({not_implemented, cont_state, 2}).
event_state(_) -> erlang:error({not_implemented, event_state, 1}).
event_state(_, _) -> erlang:error({not_implemented, event_state, 2}).
fetch_state(_) -> erlang:error({not_implemented, fetch_state, 1}).
fetch_state(_, _) -> erlang:error({not_implemented, fetch_state, 2}).
file(_) -> erlang:error({not_implemented, file, 1}).
file(_, _) -> erlang:error({not_implemented, file, 2}).
hook_state(_) -> erlang:error({not_implemented, hook_state, 1}).
hook_state(_, _) -> erlang:error({not_implemented, hook_state, 2}).
rules_state(_) -> erlang:error({not_implemented, rules_state, 1}).
rules_state(_, _) -> erlang:error({not_implemented, rules_state, 2}).
user_state(_) -> erlang:error({not_implemented, user_state, 1}).
user_state(_, _) -> erlang:error({not_implemented, user_state, 2}).
