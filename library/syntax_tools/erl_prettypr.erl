%% Clause's erl_prettypr (plan 21 LIB77E3b, docs/libraries/syntax_tools.md#erl_prettypr): erl_syntax trees as
%% prettypr documents with OTP's text. It is Erlang, since hooks are funs that call back into the layout, and the
%% document layout itself is prettypr's best/3 builtin. erl_syntax_lib's map/2 and analyze_type_application/1 are
%% private helpers here until that module is provided.
-module(erl_prettypr).
-export([
    format/1,
    format/2,
    best/1,
    best/2,
    layout/1,
    layout/2,
    get_ctxt_precedence/1,
    set_ctxt_precedence/2,
    get_ctxt_paperwidth/1,
    set_ctxt_paperwidth/2,
    get_ctxt_linewidth/1,
    set_ctxt_linewidth/2,
    get_ctxt_hook/1,
    set_ctxt_hook/2,
    get_ctxt_user/1,
    set_ctxt_user/2
]).

-define(PADDING, 2).
-define(PAPER, 80).
-define(RIBBON, 56).
-define(is_octal(C), (C >= $0 andalso C =< $7)).

% The layout context hooks receive: OTP's record, since hooks may keep or print it.
-record(ctxt, {
    prec = 0 :: integer(),
    sub_indent = 2 :: non_neg_integer(),
    break_indent = 4 :: non_neg_integer(),
    clause = undefined :: term(),
    hook = none :: none | fun(),
    paper = ?PAPER :: integer(),
    ribbon = ?RIBBON :: integer(),
    user = undefined :: term(),
    encoding = epp:default_encoding() :: epp:source_encoding(),
    empty_lines = sets:new() :: sets:set(integer())
}).

get_ctxt_precedence(#ctxt{prec = Prec}) -> Prec.

set_ctxt_precedence(Ctxt, Prec) -> Ctxt#ctxt{prec = Prec}.

get_ctxt_paperwidth(#ctxt{paper = Paper}) -> Paper.

set_ctxt_paperwidth(Ctxt, Paper) -> Ctxt#ctxt{paper = Paper}.

get_ctxt_linewidth(#ctxt{ribbon = Ribbon}) -> Ribbon.

set_ctxt_linewidth(Ctxt, Ribbon) -> Ctxt#ctxt{ribbon = Ribbon}.

get_ctxt_hook(#ctxt{hook = Hook}) -> Hook.

set_ctxt_hook(Ctxt, Hook) -> Ctxt#ctxt{hook = Hook}.

get_ctxt_user(#ctxt{user = User}) -> User.

set_ctxt_user(Ctxt, User) -> Ctxt#ctxt{user = User}.

% The text of a tree at the paper and ribbon widths of the options.
format(Node) -> format(Node, []).

format(Node, Options) ->
    prettypr:format(layout(Node, Options), paper(Options), ribbon(Options)).

% The chosen layout of a tree as a document.
best(Node) -> best(Node, []).

best(Node, Options) ->
    prettypr:best(layout(Node, Options), paper(Options), ribbon(Options)).

% The document of a tree, before a layout is chosen.
layout(Node) -> layout(Node, []).

layout(Node, Options) ->
    Ctxt = #ctxt{
        hook = proplists:get_value(hook, Options, none),
        paper = paper(Options),
        ribbon = ribbon(Options),
        user = proplists:get_value(user, Options),
        encoding = proplists:get_value(encoding, Options, epp:default_encoding()),
        empty_lines = proplists:get_value(empty_lines, Options, sets:new())
    },
    lay(Node, Ctxt).

paper(Options) -> proplists:get_value(paper, Options, ?PAPER).

ribbon(Options) -> proplists:get_value(ribbon, Options, ?RIBBON).

% A node's document, through the hook when the node has annotations.
lay(Node, #ctxt{hook = Hook} = Ctxt) ->
    case erl_syntax:get_ann(Node) =/= [] andalso Hook =/= none of
        true -> Hook(Node, Ctxt, fun lay_1/2);
        false -> lay_1(Node, Ctxt)
    end.

% A node's document with its comments around it.
lay_1(Node, Ctxt) ->
    case erl_syntax:has_comments(Node) of
        false ->
            lay_node(Node, Ctxt);
        true ->
            D = postcomments(erl_syntax:get_postcomments(Node), lay_node(Node, Ctxt)),
            precomments(erl_syntax:get_precomments(Node), D)
    end.

precomments([], D) -> D;
precomments(Comments, D) -> prettypr:above(prettypr:floating(prettypr:break(comments(Comments, false)), -1, -1), D).

postcomments([], D) -> D;
postcomments(Comments, D) -> prettypr:beside(D, prettypr:floating(prettypr:break(comments(Comments, true)), 1, 0)).

% Comment trees stacked; postcomments are padded (by 2 unless their padding says).
comments([Comment | Rest], Padded) ->
    Lines = comment_lines(erl_syntax:comment_text(Comment)),
    D =
        case Padded of
            true -> prettypr:beside(text(spaces(padding(erl_syntax:comment_padding(Comment)))), Lines);
            false -> Lines
        end,
    case Rest of
        [] -> D;
        _ -> prettypr:above(D, comments(Rest, Padded))
    end.

padding(none) -> ?PADDING;
padding(Padding) -> Padding.

comment_lines([]) ->
    prettypr:empty();
comment_lines([Line]) ->
    text([$% | Line]);
comment_lines([Line | Lines]) ->
    prettypr:above(text([$% | Line]), comment_lines(Lines)).

% The document of a node by its type; another type raises case_clause, as OTP's layout does.
lay_node(Node, Ctxt) ->
    case erl_syntax:type(Node) of
        variable ->
            text(erl_syntax:variable_literal(Node));
        atom ->
            text(erl_syntax:atom_literal(Node, Ctxt#ctxt.encoding));
        integer ->
            text(erl_syntax:integer_literal(Node));
        float ->
            text(tidy_float(erl_syntax:float_literal(Node)));
        char ->
            text(erl_syntax:char_literal(Node, Ctxt#ctxt.encoding));
        string ->
            lay_string(erl_syntax:string_literal(Node, Ctxt#ctxt.encoding), Ctxt);
        nil ->
            text("[]");
        underscore ->
            text("_");
        text ->
            text(erl_syntax:text_string(Node));
        fun_type ->
            text("fun()");
        eof_marker ->
            prettypr:empty();
        operator ->
            prettypr:floating(text(erl_syntax:operator_literal(Node)));
        tuple ->
            lay_tuple(Node, Ctxt);
        list ->
            lay_list(Node, Ctxt);
        infix_expr ->
            lay_infix(Node, Ctxt);
        prefix_expr ->
            lay_prefix(Node, Ctxt);
        application ->
            lay_application(Node, Ctxt);
        match_expr ->
            lay_match(erl_syntax:match_expr_pattern(Node), erl_syntax:match_expr_body(Node), " =", Ctxt);
        maybe_match_expr ->
            lay_maybe_match(Node, Ctxt);
        clause ->
            lay_clause(Node, Ctxt);
        function ->
            lay_function(Node, Ctxt);
        case_expr ->
            lay_case(Node, Ctxt);
        if_expr ->
            lay_keyword_clauses("if", erl_syntax:if_expr_clauses(Node), if_expr, Ctxt);
        fun_expr ->
            lay_keyword_clauses("fun", erl_syntax:fun_expr_clauses(Node), fun_expr, Ctxt);
        named_fun_expr ->
            lay_named_fun(Node, Ctxt);
        module_qualifier ->
            lay_module_qualifier(Node, Ctxt);
        maybe_expr ->
            lay_maybe(Node, Ctxt);
        arity_qualifier ->
            lay_arity_qualifier(Node, Ctxt);
        attribute ->
            lay_attribute(Node, Ctxt);
        binary ->
            lay_binary(Node, Ctxt);
        binary_field ->
            lay_binary_field(Node, Ctxt);
        block_expr ->
            lay_block(Node, Ctxt);
        catch_expr ->
            lay_catch(Node, Ctxt);
        class_qualifier ->
            lay_class_qualifier(Node, Ctxt);
        comment ->
            lay_comment(Node);
        conjunction ->
            prettypr:par(seq(erl_syntax:conjunction_body(Node), comma(), reset_prec(Ctxt)));
        disjunction ->
            prettypr:sep(seq(erl_syntax:disjunction_body(Node), floating(";"), reset_prec(Ctxt)));
        error_marker ->
            lay_error_marker(Node, Ctxt);
        warning_marker ->
            lay_warning_marker(Node, Ctxt);
        form_list ->
            lay_form_list(Node, Ctxt);
        generator ->
            lay_generator(Node, "<- ", Ctxt);
        strict_generator ->
            lay_generator(Node, "<:- ", Ctxt);
        binary_generator ->
            lay_generator(Node, "<= ", Ctxt);
        strict_binary_generator ->
            lay_generator(Node, "<:= ", Ctxt);
        map_generator ->
            lay_generator(Node, "<- ", Ctxt);
        strict_map_generator ->
            lay_generator(Node, "<:- ", Ctxt);
        zip_generator ->
            prettypr:par(seq(erl_syntax:zip_generator_body(Node), floating("&&"), reset_prec(Ctxt)));
        implicit_fun ->
            lay_implicit_fun(Node, Ctxt);
        list_comp ->
            lay_list_comp(Node, Ctxt);
        binary_comp ->
            lay_binary_comp(Node, Ctxt);
        map_comp ->
            lay_map_comp(Node, Ctxt);
        macro ->
            lay_macro(Node, Ctxt);
        parentheses ->
            parenthesized(lay(erl_syntax:parentheses_body(Node), reset_prec(Ctxt)));
        receive_expr ->
            lay_receive(Node, Ctxt);
        record_access ->
            lay_record_access(Node, Ctxt);
        record_expr ->
            lay_record_expr(Node, Ctxt);
        record_field ->
            lay_record_field(Node, Ctxt);
        record_index_expr ->
            lay_record_index(Node, Ctxt);
        map_expr ->
            lay_map_expr(Node, Ctxt);
        map_field_assoc ->
            lay_pair(erl_syntax:map_field_assoc_name(Node), "=>", erl_syntax:map_field_assoc_value(Node), Ctxt);
        map_field_exact ->
            lay_pair(erl_syntax:map_field_exact_name(Node), ":=", erl_syntax:map_field_exact_value(Node), Ctxt);
        size_qualifier ->
            lay_size_qualifier(Node, Ctxt);
        typed_record_field ->
            lay_typed_record_field(Node, Ctxt);
        try_expr ->
            lay_try(Node, Ctxt);
        annotated_type ->
            lay_annotated_type(Node, Ctxt);
        type_application ->
            lay_type_application_node(Node, Ctxt);
        bitstring_type ->
            lay_bitstring_type(Node, Ctxt);
        constrained_function_type ->
            lay_constrained_function_type(Node, Ctxt);
        function_type ->
            lay_function_type(Node, Ctxt);
        constraint ->
            lay_constraint(Node, Ctxt);
        map_type ->
            lay_map_type(Node, Ctxt);
        map_type_assoc ->
            lay_pair(erl_syntax:map_type_assoc_name(Node), "=>", erl_syntax:map_type_assoc_value(Node), Ctxt);
        map_type_exact ->
            lay_pair(erl_syntax:map_type_exact_name(Node), ":=", erl_syntax:map_type_exact_value(Node), Ctxt);
        integer_range_type ->
            lay_integer_range_type(Node, Ctxt);
        record_type ->
            lay_record_type(Node, Ctxt);
        record_type_field ->
            lay_pair(erl_syntax:record_type_field_name(Node), "::", erl_syntax:record_type_field_type(Node), Ctxt);
        tuple_type ->
            lay_tuple_type(Node, Ctxt);
        type_union ->
            lay_type_union(Node, Ctxt);
        user_type_application ->
            lay_type_application(
                erl_syntax:user_type_application_name(Node), erl_syntax:user_type_application_arguments(Node), Ctxt
            )
    end.

%% Literals and containers.

lay_tuple(Node, Ctxt) ->
    Elements = seq(erl_syntax:tuple_elements(Node), comma(), reset_prec(Ctxt)),
    enclosed("{", prettypr:sep(Elements), "}").

lay_list(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Compact = erl_syntax:compact_list(Node),
    Prefix = prettypr:sep(seq(erl_syntax:list_prefix(Compact), comma(), Ctxt1)),
    D =
        case erl_syntax:list_suffix(Compact) of
            none ->
                prettypr:beside(Prefix, floating("]"));
            Suffix ->
                Tail = prettypr:beside(floating("| "), prettypr:beside(lay(Suffix, Ctxt1), floating("]"))),
                prettypr:follow(Prefix, Tail)
        end,
    prettypr:beside(floating("["), D).

lay_binary(Node, Ctxt) ->
    Fields = seq(erl_syntax:binary_fields(Node), comma(), reset_prec(Ctxt)),
    enclosed("<<", prettypr:par(Fields), ">>").

lay_binary_field(Node, Ctxt) ->
    Ctxt1 = set_prec(Ctxt, erl_parse:max_prec()),
    Body = lay(erl_syntax:binary_field_body(Node), Ctxt1),
    Types =
        case erl_syntax:binary_field_types(Node) of
            [] -> prettypr:empty();
            Ts -> prettypr:beside(floating("/"), lay_bit_types(Ts, Ctxt1))
        end,
    prettypr:beside(Body, Types).

lay_bit_types([Type], Ctxt) ->
    lay(Type, Ctxt);
lay_bit_types([Type | Types], Ctxt) ->
    prettypr:beside(lay(Type, Ctxt), prettypr:beside(floating("-"), lay_bit_types(Types, Ctxt))).

lay_size_qualifier(Node, Ctxt) ->
    Ctxt1 = set_prec(Ctxt, erl_parse:max_prec()),
    D1 = lay(erl_syntax:size_qualifier_body(Node), Ctxt1),
    D2 = lay(erl_syntax:size_qualifier_argument(Node), Ctxt1),
    prettypr:beside(D1, prettypr:beside(text(":"), D2)).

lay_map_expr(Node, Ctxt) ->
    {PrecL, Prec, _} = erl_parse:inop_prec('#'),
    Fields = prettypr:par(seq(erl_syntax:map_expr_fields(Node), comma(), reset_prec(Ctxt))),
    D = prettypr:beside(text("#{"), prettypr:beside(Fields, floating("}"))),
    with_argument(erl_syntax:map_expr_argument(Node), D, PrecL, Prec, Ctxt).

% A record or map expression after its argument, unless it has none.
with_argument(none, D, _PrecL, Prec, Ctxt) ->
    maybe_parentheses(D, Prec, Ctxt);
with_argument(Argument, D, PrecL, Prec, Ctxt) ->
    maybe_parentheses(prettypr:beside(lay(Argument, set_prec(Ctxt, PrecL)), D), Prec, Ctxt).

% Name Separator Value, as map fields, map type fields and record type fields lay out.
lay_pair(Name, Separator, Value, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    D1 = lay(Name, Ctxt1),
    D2 = lay(Value, Ctxt1),
    prettypr:par([D1, floating(Separator), D2], Ctxt1#ctxt.break_indent).

%% Operators and calls.

lay_infix(Node, Ctxt) ->
    Operator = erl_syntax:infix_expr_operator(Node),
    {PrecL, Prec, PrecR} =
        case erl_syntax:type(Operator) of
            operator -> erl_parse:inop_prec(erl_syntax:operator_name(Operator));
            _ -> {0, 0, 0}
        end,
    D1 = lay(erl_syntax:infix_expr_left(Node), set_prec(Ctxt, PrecL)),
    D2 = lay(Operator, reset_prec(Ctxt)),
    D3 = lay(erl_syntax:infix_expr_right(Node), set_prec(Ctxt, PrecR)),
    maybe_parentheses(prettypr:par([D1, D2, D3], Ctxt#ctxt.break_indent), Prec, Ctxt).

lay_prefix(Node, Ctxt) ->
    Operator = erl_syntax:prefix_expr_operator(Node),
    {{Prec, PrecR}, Name} =
        case erl_syntax:type(Operator) of
            operator ->
                OperatorName = erl_syntax:operator_name(Operator),
                {erl_parse:preop_prec(OperatorName), OperatorName};
            _ ->
                {{0, 0}, any}
        end,
    D1 = lay(Operator, reset_prec(Ctxt)),
    D2 = lay(erl_syntax:prefix_expr_argument(Node), set_prec(Ctxt, PrecR)),
    D =
        case Name =:= '+' orelse Name =:= '-' of
            true -> prettypr:beside(D1, D2);
            false -> prettypr:par([D1, D2], Ctxt#ctxt.break_indent)
        end,
    maybe_parentheses(D, Prec, Ctxt).

lay_application(Node, Ctxt) ->
    {PrecL, Prec} = erl_parse:func_prec(),
    Operator = lay(erl_syntax:application_operator(Node), set_prec(Ctxt, PrecL)),
    Arguments = seq(erl_syntax:application_arguments(Node), comma(), reset_prec(Ctxt)),
    D = prettypr:beside(Operator, prettypr:beside(text("("), prettypr:beside(prettypr:sep(Arguments), floating(")")))),
    maybe_parentheses(D, Prec, Ctxt).

% Pattern Operator Body, as match and maybe-match expressions lay out.
lay_match(Pattern, Body, Operator, Ctxt) ->
    {PrecL, Prec, PrecR} = erl_parse:inop_prec('='),
    D1 = lay(Pattern, set_prec(Ctxt, PrecL)),
    D2 = lay(Body, set_prec(Ctxt, PrecR)),
    D = prettypr:follow(prettypr:beside(D1, floating(Operator)), D2, Ctxt#ctxt.break_indent),
    maybe_parentheses(D, Prec, Ctxt).

lay_maybe_match(Node, Ctxt) ->
    lay_match(erl_syntax:maybe_match_expr_pattern(Node), erl_syntax:maybe_match_expr_body(Node), " ?=", Ctxt).

lay_module_qualifier(Node, Ctxt) ->
    {PrecL, _Prec, PrecR} = erl_parse:inop_prec(':'),
    D1 = lay(erl_syntax:module_qualifier_argument(Node), set_prec(Ctxt, PrecL)),
    D2 = lay(erl_syntax:module_qualifier_body(Node), set_prec(Ctxt, PrecR)),
    prettypr:beside(D1, prettypr:beside(text(":"), D2)).

lay_arity_qualifier(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    D1 = lay(erl_syntax:arity_qualifier_body(Node), Ctxt1),
    D2 = lay(erl_syntax:arity_qualifier_argument(Node), Ctxt1),
    prettypr:beside(D1, prettypr:beside(text("/"), D2)).

lay_implicit_fun(Node, Ctxt) ->
    prettypr:beside(floating("fun "), lay(erl_syntax:implicit_fun_name(Node), reset_prec(Ctxt))).

lay_catch(Node, Ctxt) ->
    {Prec, PrecR} = erl_parse:preop_prec('catch'),
    D = lay(erl_syntax:catch_expr_body(Node), set_prec(Ctxt, PrecR)),
    maybe_parentheses(prettypr:follow(text("catch"), D, Ctxt#ctxt.break_indent), Prec, Ctxt).

lay_class_qualifier(Node, Ctxt) ->
    Ctxt1 = set_prec(Ctxt, erl_parse:max_prec()),
    D1 = lay(erl_syntax:class_qualifier_argument(Node), Ctxt1),
    D2 = lay(erl_syntax:class_qualifier_body(Node), Ctxt1),
    Stacktrace = erl_syntax:class_qualifier_stacktrace(Node),
    case erl_syntax:variable_name(Stacktrace) of
        '_' ->
            prettypr:beside(D1, prettypr:beside(text(":"), D2));
        _ ->
            D3 = lay(Stacktrace, Ctxt1),
            prettypr:beside(D1, prettypr:beside(prettypr:beside(text(":"), D2), prettypr:beside(text(":"), D3)))
    end.

lay_macro(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Name = erl_syntax:macro_name(Node),
    D =
        case erl_syntax:macro_arguments(Node) of
            none ->
                lay(Name, Ctxt1);
            Arguments ->
                As = seq(Arguments, comma(), set_prec(Ctxt1, erl_parse:max_prec())),
                prettypr:beside(
                    lay(Name, Ctxt1), prettypr:beside(text("("), prettypr:beside(prettypr:par(As), floating(")")))
                )
        end,
    % Macros are always parenthesized when they could bind: OTP is conservative here.
    maybe_parentheses(prettypr:beside(floating("?"), D), 0, Ctxt).

%% Records.

lay_record_access(Node, Ctxt) ->
    {PrecL, Prec, PrecR} = erl_parse:inop_prec('#'),
    D1 = lay(erl_syntax:record_access_argument(Node), set_prec(Ctxt, PrecL)),
    D2 = prettypr:beside(floating("."), lay(erl_syntax:record_access_field(Node), set_prec(Ctxt, PrecR))),
    Name = record_name(erl_syntax:record_access_type(Node), reset_prec(Ctxt)),
    D3 = prettypr:beside(prettypr:beside(floating("#"), Name), D2),
    maybe_parentheses(prettypr:beside(D1, D3), Prec, Ctxt).

lay_record_expr(Node, Ctxt) ->
    {PrecL, Prec, _} = erl_parse:inop_prec('#'),
    Ctxt1 = reset_prec(Ctxt),
    Name = record_name(erl_syntax:record_expr_type(Node), Ctxt1),
    Fields = prettypr:par(seq(erl_syntax:record_expr_fields(Node), comma(), Ctxt1)),
    D = prettypr:beside(
        prettypr:beside(floating("#"), Name), prettypr:beside(text("{"), prettypr:beside(Fields, floating("}")))
    ),
    with_argument(erl_syntax:record_expr_argument(Node), D, PrecL, Prec, Ctxt).

% A record name, Module:Name for a native record's qualified name.
record_name(Node, Ctxt) ->
    case erl_syntax:type(Node) of
        list ->
            [Module, Name] = erl_syntax:list_elements(Node),
            prettypr:beside(lay(Module, Ctxt), prettypr:beside(text(":"), lay(Name, Ctxt)));
        _ ->
            lay(Node, Ctxt)
    end.

lay_record_field(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Name = lay(erl_syntax:record_field_name(Node), Ctxt1),
    case erl_syntax:record_field_value(Node) of
        none -> Name;
        Value -> prettypr:par([Name, floating("="), lay(Value, Ctxt1)], Ctxt1#ctxt.break_indent)
    end.

lay_record_index(Node, Ctxt) ->
    {Prec, PrecR} = erl_parse:preop_prec('#'),
    D1 = lay(erl_syntax:record_index_expr_type(Node), reset_prec(Ctxt)),
    D2 = lay(erl_syntax:record_index_expr_field(Node), set_prec(Ctxt, PrecR)),
    D = prettypr:beside(prettypr:beside(floating("#"), D1), prettypr:beside(floating("."), D2)),
    maybe_parentheses(D, Prec, Ctxt).

lay_typed_record_field(Node, Ctxt) ->
    {_, Prec, _} = erl_parse:type_inop_prec('::'),
    Ctxt1 = reset_prec(Ctxt),
    D1 = lay(erl_syntax:typed_record_field_body(Node), Ctxt1),
    D2 = lay(erl_syntax:typed_record_field_type(Node), set_prec(Ctxt, Prec)),
    maybe_parentheses(prettypr:par([D1, floating("::"), D2], Ctxt1#ctxt.break_indent), Prec, Ctxt).

%% Comprehensions and generators.

lay_generator(Node, Arrow, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    [[Pattern], [Body]] = erl_syntax:subtrees(Node),
    D1 = lay(Pattern, Ctxt1),
    D2 = lay(Body, Ctxt1),
    prettypr:par([D1, prettypr:beside(text(Arrow), D2)], Ctxt1#ctxt.break_indent).

lay_list_comp(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Template = lay_template(erl_syntax:list_comp_template(Node), Ctxt1),
    Body = prettypr:par(seq(erl_syntax:list_comp_body(Node), comma(), Ctxt1)),
    comprehension("[", Template, "|| ", Body, "]").

lay_binary_comp(Node, Ctxt) ->
    Ctxt1 = set_prec(Ctxt, erl_parse:max_prec()),
    Template = lay(erl_syntax:binary_comp_template(Node), Ctxt1),
    Body = prettypr:par(seq(erl_syntax:binary_comp_body(Node), comma(), Ctxt1)),
    comprehension("<< ", Template, " || ", Body, " >>").

lay_map_comp(Node, Ctxt) ->
    Ctxt1 = set_prec(Ctxt, erl_parse:max_prec()),
    Template = lay_template(erl_syntax:map_comp_template(Node), Ctxt1),
    Body = prettypr:par(seq(erl_syntax:map_comp_body(Node), comma(), Ctxt1)),
    comprehension("#{", Template, "|| ", Body, "}").

% A comprehension template: one expression, or several separated by commas.
lay_template(Templates, Ctxt) when is_list(Templates) -> prettypr:par(seq(Templates, comma(), Ctxt));
lay_template(Template, Ctxt) -> lay(Template, Ctxt).

comprehension(Open, Template, Bars, Body, Close) ->
    Rest = prettypr:beside(floating(Bars), prettypr:beside(Body, floating(Close))),
    prettypr:beside(floating(Open), prettypr:par([Template, Rest])).

%% Clauses and the expressions made of them.

lay_clause(Node, Ctxt) ->
    Ctxt1 = (reset_prec(Ctxt))#ctxt{clause = undefined},
    Patterns = prettypr:par(seq(erl_syntax:clause_patterns(Node), comma(), Ctxt1)),
    Guard =
        case erl_syntax:clause_guard(Node) of
            none -> none;
            G -> lay(G, Ctxt1)
        end,
    Body = lay_body(erl_syntax:clause_body(Node), Ctxt1),
    case Ctxt#ctxt.clause of
        {function, Name} -> fun_clause(Name, Patterns, Guard, Body, Ctxt);
        fun_expr -> fun_clause(none, Patterns, Guard, Body, Ctxt);
        undefined -> fun_clause(none, Patterns, Guard, Body, Ctxt);
        if_expr -> clause_body(Body, if_guard(Guard), Ctxt);
        case_expr -> case_clause(Patterns, Guard, Body, Ctxt);
        maybe_expr -> case_clause(Patterns, Guard, Body, Ctxt);
        receive_expr -> case_clause(Patterns, Guard, Body, Ctxt);
        try_expr -> case_clause(Patterns, Guard, Body, Ctxt)
    end.

if_guard(none) -> text("true");
if_guard(Guard) -> Guard.

% A function or fun clause: its name (if any), the parenthesized patterns, the guard and the body.
fun_clause(none, Patterns, Guard, Body, Ctxt) ->
    case_clause(parenthesized(Patterns), Guard, Body, Ctxt);
fun_clause(Name, Patterns, Guard, Body, Ctxt) ->
    case_clause(prettypr:beside(Name, parenthesized(Patterns)), Guard, Body, Ctxt).

case_clause(Head, Guard, Body, Ctxt) ->
    clause_body(Body, guarded(Guard, Head, Ctxt), Ctxt).

guarded(none, Head, _Ctxt) ->
    Head;
guarded(Guard, Head, Ctxt) ->
    prettypr:par([Head, prettypr:follow(text("when"), Guard, Ctxt#ctxt.sub_indent)], Ctxt#ctxt.break_indent).

% Head -> followed by the body, indented.
clause_body(Body, Head, Ctxt) ->
    prettypr:sep([prettypr:beside(Head, floating(" ->")), prettypr:nest(Ctxt#ctxt.break_indent, Body)]).

% The expressions of a clause body, one per line, with an empty line where empty_lines has the source keep one.
lay_body([], _Ctxt) ->
    prettypr:empty();
lay_body([Expression], Ctxt) ->
    lay(Expression, Ctxt);
lay_body([Expression | Rest], Ctxt) ->
    First = prettypr:beside(lay(Expression, Ctxt), comma()),
    Next = lay_body(Rest, Ctxt),
    case empty_line_after(Expression, Rest, Ctxt) of
        true -> prettypr:above(prettypr:above(First, text("")), Next);
        false -> prettypr:above(First, Next)
    end.

% Whether an empty line of the source follows an expression, before the next one.
empty_line_after(Expression, Rest, #ctxt{empty_lines = EmptyLines}) ->
    try
        Line = line(Expression),
        case Rest of
            [] -> sets:is_element(Line + 1, EmptyLines);
            [Next | _] -> line(Next) - Line >= 2 andalso sets:is_element(Line + 1, EmptyLines)
        end
    catch
        error:badarith -> false
    end.

line(Node) -> erl_anno:line(erl_syntax:get_pos(Node)).

% Clauses one above the other, separated by semicolons, in the clause style of Kind.
lay_clauses(Clauses, Kind, Ctxt) ->
    vertical(seq(Clauses, floating(";"), Ctxt#ctxt{clause = Kind})).

lay_function(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Name = lay(erl_syntax:function_name(Node), Ctxt1),
    Clauses = lay_clauses(erl_syntax:function_clauses(Node), {function, Name}, Ctxt1),
    prettypr:beside(Clauses, floating(".")).

lay_case(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Indent = Ctxt1#ctxt.break_indent,
    Argument = lay(erl_syntax:case_expr_argument(Node), Ctxt1),
    Clauses = lay_clauses(erl_syntax:case_expr_clauses(Node), case_expr, Ctxt1),
    Head = prettypr:par([prettypr:follow(text("case"), Argument, Indent), text("of")], Indent),
    prettypr:sep([Head, prettypr:nest(Indent, Clauses), text("end")]).

% if and fun: the keyword followed by the clauses, then end.
lay_keyword_clauses(Keyword, Clauses, Kind, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    D = lay_clauses(Clauses, Kind, Ctxt1),
    prettypr:sep([prettypr:follow(text(Keyword), D, Ctxt1#ctxt.break_indent), text("end")]).

lay_named_fun(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Name = lay(erl_syntax:named_fun_expr_name(Node), Ctxt1),
    D = lay_clauses(erl_syntax:named_fun_expr_clauses(Node), {function, Name}, Ctxt1),
    prettypr:sep([prettypr:follow(text("fun"), D, Ctxt1#ctxt.break_indent), text("end")]).

lay_block(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Body = seq(erl_syntax:block_expr_body(Node), comma(), Ctxt1),
    prettypr:sep([text("begin"), prettypr:nest(Ctxt1#ctxt.break_indent, prettypr:sep(Body)), text("end")]).

lay_maybe(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Indent = Ctxt1#ctxt.break_indent,
    Body = vertical(seq(erl_syntax:maybe_expr_body(Node), comma(), Ctxt1)),
    [Last | Rest] =
        case erl_syntax:maybe_expr_else(Node) of
            none ->
                [text("end")];
            Else ->
                Clauses = lay_clauses(erl_syntax:else_expr_clauses(Else), maybe_expr, Ctxt1),
                [text("else"), prettypr:nest(Indent, Clauses), text("end")]
        end,
    prettypr:sep([prettypr:par([text("maybe"), prettypr:nest(Indent, Body), Last]) | Rest]).

lay_receive(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Indent = Ctxt1#ctxt.break_indent,
    Clauses = lay_clauses(erl_syntax:receive_expr_clauses(Node), receive_expr, Ctxt1),
    D =
        case erl_syntax:receive_expr_timeout(Node) of
            none ->
                Clauses;
            Timeout ->
                After = lay(Timeout, Ctxt1),
                Action = prettypr:sep(seq(erl_syntax:receive_expr_action(Node), comma(), Ctxt1)),
                prettypr:sep([Clauses, prettypr:follow(floating("after"), clause_body(Action, After, Ctxt1), Indent)])
        end,
    prettypr:sep([text("receive"), prettypr:nest(Indent, D), text("end")]).

lay_try(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Indent = Ctxt1#ctxt.break_indent,
    Body = prettypr:sep(seq(erl_syntax:try_expr_body(Node), comma(), Ctxt1)),
    Parts0 = [text("end")],
    Parts1 =
        case erl_syntax:try_expr_after(Node) of
            [] -> Parts0;
            After -> [text("after"), prettypr:nest(Indent, prettypr:sep(seq(After, comma(), Ctxt1))) | Parts0]
        end,
    Parts2 = try_section("catch", erl_syntax:try_expr_handlers(Node), Parts1, Ctxt1),
    [First | Rest] = try_section("of", erl_syntax:try_expr_clauses(Node), Parts2, Ctxt1),
    prettypr:sep([prettypr:par([prettypr:follow(text("try"), Body, Indent), First]) | Rest]).

% The of or catch section of a try in front of the later ones, when it has clauses.
try_section(_Keyword, [], Later, _Ctxt) ->
    Later;
try_section(Keyword, Clauses, Later, Ctxt) ->
    [text(Keyword), prettypr:nest(Ctxt#ctxt.break_indent, lay_clauses(Clauses, try_expr, Ctxt)) | Later].

%% Forms, attributes, comments and markers.

% Forms one above the other with an empty line between them.
lay_form_list(Node, Ctxt) ->
    vertical_sep(text(""), seq(erl_syntax:form_list_elements(Node), none, reset_prec(Ctxt))).

lay_comment(Node) ->
    Lines = comment_lines(erl_syntax:comment_text(Node)),
    case erl_syntax:comment_padding(Node) of
        none -> prettypr:floating(prettypr:break(Lines));
        Padding -> prettypr:floating(prettypr:break(prettypr:beside(text(spaces(Padding)), Lines)))
    end.

lay_error_marker(Node, Ctxt) ->
    Info = lay_error_info(erl_syntax:error_marker_info(Node), reset_prec(Ctxt)),
    prettypr:beside(text("** "), prettypr:beside(Info, text(" **"))).

lay_warning_marker(Node, Ctxt) ->
    prettypr:beside(text("%% WARNING: "), lay_error_info(erl_syntax:warning_marker_info(Node), reset_prec(Ctxt))).

% An error descriptor as its module formats it ("Line: " in front for a positive line), any other term as code.
lay_error_info({Line, Module, Descriptor} = Info, Ctxt) when is_integer(Line), is_atom(Module) ->
    case formatted_error(Module, Descriptor) of
        {ok, Text} when Line > 0 -> prettypr:beside(text(io_lib:format("~w: ", [Line])), text(Text));
        {ok, Text} -> text(Text);
        error -> lay(erl_syntax:abstract(Info), Ctxt)
    end;
lay_error_info(Info, Ctxt) ->
    lay(erl_syntax:abstract(Info), Ctxt).

formatted_error(Module, Descriptor) ->
    try Module:format_error(Descriptor) of
        Text when is_list(Text) -> {ok, Text};
        _ -> error
    catch
        _:_ -> error
    end.

lay_attribute(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    Arguments = erl_syntax:attribute_arguments(Node),
    Name =
        case erl_syntax:attribute_name(Node) of
            {atom, _, 'if'} -> erl_syntax:variable('if');
            {atom, Anno, native_record} -> {atom, Anno, record};
            Other -> Other
        end,
    D =
        case attribute_kind(Node) of
            spec -> lay_spec(Name, Arguments, Ctxt1);
            type -> lay_type_attribute(Name, Arguments, Ctxt1);
            function_names -> lay_function_names(Name, Arguments, Ctxt1);
            native_record -> lay_native_record(Arguments, Ctxt1);
            _ when Arguments =:= none -> lay(Name, Ctxt1);
            _ -> lay_attribute_call(Name, Arguments, Ctxt1)
        end,
    prettypr:beside(floating("-"), prettypr:beside(D, floating("."))).

% How an attribute lays out, by its literal name.
attribute_kind(Node) ->
    Name = erl_syntax:attribute_name(Node),
    try erl_syntax:concrete(Name) of
        Kind when Kind =:= spec; Kind =:= callback -> spec;
        Kind when Kind =:= type; Kind =:= opaque -> type;
        Kind when Kind =:= export_type; Kind =:= optional_callbacks -> function_names;
        native_record -> native_record;
        _ -> other
    catch
        _:_ -> other
    end.

lay_attribute_call(Name, Arguments, Ctxt) ->
    D = prettypr:sep(seq(Arguments, text(","), Ctxt)),
    prettypr:beside(lay(Name, Ctxt), prettypr:beside(text("("), prettypr:beside(D, floating(")")))).

% -spec and -callback: the function name, then its function types as clauses.
lay_spec(Attribute, [Spec], Ctxt) ->
    [Function, Types] = erl_syntax:tuple_elements(Spec),
    Name =
        case erl_syntax:type(Function) of
            tuple ->
                case erl_syntax:tuple_elements(Function) of
                    [F, _] -> F;
                    [M, F, _] -> erl_syntax:module_qualifier(M, F);
                    _ -> Function
                end;
            _ ->
                Function
        end,
    Clauses = lay_clauses(erl_syntax:concrete(dodge_macros(Types)), spec, Ctxt),
    prettypr:beside(prettypr:follow(lay(Attribute, Ctxt), lay(Name, Ctxt), Ctxt#ctxt.break_indent), Clauses).

% -type and -opaque: Name(Variables) :: Type.
lay_type_attribute(Attribute, [Definition], Ctxt) ->
    [Name, Type, Variables] = erl_syntax:tuple_elements(Definition),
    TypeName = dodge_macros(Name),
    Body = dodge_macros(Type),
    Parameters = erl_syntax:concrete(dodge_macros(Variables)),
    Head = lay_type_application(TypeName, Parameters, Ctxt),
    D = lay(erl_syntax:concrete(Body), Ctxt),
    Left = prettypr:follow(lay(Attribute, Ctxt), prettypr:beside(Head, floating(" :: ")), Ctxt#ctxt.break_indent),
    prettypr:beside(Left, D).

% -export_type and -optional_callbacks: the function names as a list of arity qualifiers.
lay_function_names(Attribute, [Names], Ctxt) ->
    Qualifiers = [
        erl_syntax:arity_qualifier(erl_syntax:atom(N), erl_syntax:integer(A))
     || {N, A} <- erl_syntax:concrete(dodge_macros(Names))
    ],
    List = lay(erl_syntax:list(Qualifiers), Ctxt),
    prettypr:beside(lay(Attribute, Ctxt), prettypr:beside(text("("), prettypr:beside(List, floating(")")))).

lay_native_record([Name, Definition], Ctxt) ->
    D = prettypr:sep(seq([Definition], text(","), Ctxt)),
    prettypr:beside(text("record #"), prettypr:beside(lay(Name, Ctxt), D)).

% Macros in type attributes as the atoms '?NAME', so concrete/1 accepts the types.
dodge_macros(Tree) ->
    map_tree(
        fun(Node) ->
            case erl_syntax:type(Node) of
                macro ->
                    Name = erl_syntax:variable_name(erl_syntax:macro_name(Node)),
                    erl_syntax:atom(list_to_atom("?" ++ atom_to_list(Name)));
                _ ->
                    Node
            end
        end,
        Tree
    ).

% erl_syntax_lib:map/2: Fun applied to every node, bottom-up.
map_tree(Fun, Tree) ->
    case erl_syntax:subtrees(Tree) of
        [] ->
            Fun(Tree);
        Groups ->
            Mapped = erl_syntax:make_tree(erl_syntax:type(Tree), [[map_tree(Fun, T) || T <- G] || G <- Groups]),
            Fun(erl_syntax:copy_attrs(Tree, Mapped))
    end.

%% Types.

lay_annotated_type(Node, Ctxt) ->
    {_, Prec, _} = erl_parse:type_inop_prec('::'),
    D1 = lay(erl_syntax:annotated_type_name(Node), reset_prec(Ctxt)),
    D2 = lay(erl_syntax:annotated_type_body(Node), set_prec(Ctxt, Prec)),
    maybe_parentheses(prettypr:follow(prettypr:beside(D1, floating(" ::")), D2, Ctxt#ctxt.break_indent), Prec, Ctxt).

% A type application, [] and [T] and [T, ...] for the list types.
lay_type_application_node(Node, Ctxt) ->
    Name = erl_syntax:type_application_name(Node),
    Arguments = erl_syntax:type_application_arguments(Node),
    case type_application_name(Node) of
        {nil, 0} ->
            text("[]");
        {list, 1} ->
            [Element] = Arguments,
            prettypr:beside(text("["), prettypr:beside(lay(Element, reset_prec(Ctxt)), text("]")));
        {nonempty_list, 1} ->
            [Element] = Arguments,
            prettypr:beside(text("["), prettypr:beside(lay(Element, reset_prec(Ctxt)), text(", ...]")));
        _ ->
            lay_type_application(Name, Arguments, Ctxt)
    end.

% erl_syntax_lib:analyze_type_application/1 of a type application: {Name, Arity} or {Module, {Name, Arity}};
% throws syntax_error for a name that is none.
type_application_name(Node) ->
    Arity = length(erl_syntax:type_application_arguments(Node)),
    Name =
        try
            type_name(erl_syntax:type_application_name(Node))
        catch
            _:_ -> throw(syntax_error)
        end,
    with_arity(Arity, Name).

type_name(Node) ->
    case erl_syntax:type(Node) of
        atom ->
            erl_syntax:atom_value(Node);
        arity_qualifier ->
            Arity = erl_syntax:arity_qualifier_argument(Node),
            Name = erl_syntax:arity_qualifier_body(Node),
            case erl_syntax:type(Arity) =:= integer andalso erl_syntax:type(Name) =:= atom of
                true -> with_arity(erl_syntax:integer_value(Arity), erl_syntax:atom_value(Name));
                false -> throw(syntax_error)
            end;
        module_qualifier ->
            Module = erl_syntax:module_qualifier_argument(Node),
            case erl_syntax:type(Module) of
                atom -> {erl_syntax:atom_value(Module), type_name(erl_syntax:module_qualifier_body(Node))};
                _ -> throw(syntax_error)
            end;
        _ ->
            throw(syntax_error)
    end.

with_arity(Arity, {Module, Name}) -> {Module, with_arity(Arity, Name)};
with_arity(Arity, Name) when is_atom(Name) -> {Name, Arity};
with_arity(Arity, Arity) -> Arity;
with_arity(_Arity, Name) -> Name.

lay_type_application(Name, Arguments, Ctxt) ->
    {PrecL, Prec} = erl_parse:func_prec(),
    D1 = lay(Name, set_prec(Ctxt, PrecL)),
    As = seq(Arguments, comma(), reset_prec(Ctxt)),
    D = prettypr:beside(D1, prettypr:beside(text("("), prettypr:beside(prettypr:par(As), floating(")")))),
    maybe_parentheses(D, Prec, Ctxt).

lay_bitstring_type(Node, Ctxt) ->
    Ctxt1 = set_prec(Ctxt, erl_parse:max_prec()),
    M = erl_syntax:bitstring_type_m(Node),
    N = erl_syntax:bitstring_type_n(Node),
    Parts =
        [prettypr:beside(text("_:"), lay(M, Ctxt1)) || not is_zero(M)] ++
            [prettypr:beside(text("_:_*"), lay(N, Ctxt1)) || not is_zero(N)],
    enclosed("<<", prettypr:par(separated(Parts, comma())), ">>").

is_zero(Node) -> erl_syntax:type(Node) =:= integer andalso erl_syntax:integer_value(Node) =:= 0.

lay_constrained_function_type(Node, Ctxt) ->
    Ctxt1 = reset_prec(Ctxt),
    D1 = lay(erl_syntax:constrained_function_type_body(Node), Ctxt1),
    D2 = lay(erl_syntax:constrained_function_type_argument(Node), Ctxt1#ctxt{clause = undefined}),
    prettypr:beside(D1, prettypr:beside(floating(" when "), D2)).

% A function type: fun((Arguments) -> Return), without fun( ) in a spec.
lay_function_type(Node, Ctxt) ->
    {Before, After} =
        case Ctxt#ctxt.clause of
            spec -> {"", ""};
            _ -> {"fun(", ")"}
        end,
    Ctxt1 = (reset_prec(Ctxt))#ctxt{clause = undefined},
    D1 =
        case erl_syntax:function_type_arguments(Node) of
            any_arity ->
                text("(...)");
            Arguments ->
                As = seq(Arguments, comma(), Ctxt1),
                prettypr:beside(text("("), prettypr:beside(prettypr:par(As), floating(")")))
        end,
    D2 = lay(erl_syntax:function_type_return(Node), Ctxt1),
    Arrow = prettypr:beside(floating(" -> "), prettypr:beside(D2, floating(After))),
    prettypr:beside(floating(Before), prettypr:beside(D1, Arrow)).

% A constraint: Var :: Type for is_subtype, else as a type application.
lay_constraint(Node, Ctxt) ->
    Name = erl_syntax:constraint_argument(Node),
    Arguments = erl_syntax:constraint_body(Node),
    case is_subtype(Name, Arguments) of
        true ->
            [Variable, Type] = Arguments,
            {PrecL, Prec, PrecR} = erl_parse:type_inop_prec('::'),
            D1 = lay(Variable, set_prec(Ctxt, PrecL)),
            D2 = lay(Type, set_prec(Ctxt, PrecR)),
            D = prettypr:follow(prettypr:beside(D1, floating(" ::")), D2, Ctxt#ctxt.break_indent),
            maybe_parentheses(D, Prec, Ctxt);
        false ->
            lay_type_application(Name, Arguments, Ctxt)
    end.

is_subtype(Name, [Variable, _]) -> erl_syntax:is_atom(Name, is_subtype) andalso erl_syntax:type(Variable) =:= variable;
is_subtype(_, _) -> false.

lay_map_type(Node, Ctxt) ->
    case erl_syntax:map_type_fields(Node) of
        any_size ->
            text("map()");
        Fields ->
            D = enclosed("#{", prettypr:par(seq(Fields, comma(), reset_prec(Ctxt))), "}"),
            {Prec, _PrecR} = erl_parse:type_preop_prec('#'),
            maybe_parentheses(D, Prec, Ctxt)
    end.

lay_integer_range_type(Node, Ctxt) ->
    {PrecL, Prec, PrecR} = erl_parse:type_inop_prec('..'),
    D1 = lay(erl_syntax:integer_range_type_low(Node), set_prec(Ctxt, PrecL)),
    D2 = lay(erl_syntax:integer_range_type_high(Node), set_prec(Ctxt, PrecR)),
    maybe_parentheses(prettypr:beside(D1, prettypr:beside(text(".."), D2)), Prec, Ctxt).

lay_record_type(Node, Ctxt) ->
    {Prec, _PrecR} = erl_parse:type_preop_prec('#'),
    Name = prettypr:beside(text("#"), lay(erl_syntax:record_type_name(Node), reset_prec(Ctxt))),
    Fields = seq(erl_syntax:record_type_fields(Node), comma(), reset_prec(Ctxt)),
    D = prettypr:beside(Name, prettypr:beside(text("{"), prettypr:beside(prettypr:par(Fields), floating("}")))),
    maybe_parentheses(D, Prec, Ctxt).

lay_tuple_type(Node, Ctxt) ->
    case erl_syntax:tuple_type_elements(Node) of
        any_size -> text("tuple()");
        Elements -> enclosed("{", prettypr:sep(seq(Elements, comma(), reset_prec(Ctxt))), "}")
    end.

lay_type_union(Node, Ctxt) ->
    {_, Prec, PrecR} = erl_parse:type_inop_prec('|'),
    D = prettypr:sep(seq(erl_syntax:type_union_types(Node), floating(" |"), set_prec(Ctxt, PrecR))),
    maybe_parentheses(D, Prec, Ctxt).

%% Strings and floats.

% A string literal, split into several literals one above the other when longer than 2/3 of the ribbon.
lay_string(String, Ctxt) ->
    Width = (Ctxt#ctxt.ribbon * 2) div 3,
    lay_string(String, length(String), Width).

lay_string(String, Length, Width) when Length > Width, Width > 0 ->
    case split_string(String, Width - 1, Length, []) of
        {_, ""} -> text(String);
        {First, Rest} -> prettypr:above(text(First ++ "\""), lay_string([$" | Rest], Length - Width + 1, Width))
    end;
lay_string(String, _Length, _Width) ->
    text(String).

% The text of a string literal up to a split point: a blank, \t or \n once N characters are taken (or anything
% 10 characters later) while 5 are left; escape sequences are never split.
split_string([$\s | Rest], N, L, Taken) when N =< 0, L >= 5 -> {lists:reverse([$\s | Taken]), Rest};
split_string([$\t | Rest], N, L, Taken) when N =< 0, L >= 5 -> {lists:reverse([$t, $\\ | Taken]), Rest};
split_string([$\n | Rest], N, L, Taken) when N =< 0, L >= 5 -> {lists:reverse([$n, $\\ | Taken]), Rest};
split_string([$\\ | Rest], N, L, Taken) -> split_escape(Rest, N - 1, L - 1, [$\\ | Taken]);
split_string(Rest, N, L, Taken) when N =< -10, L >= 5 -> {lists:reverse(Taken), Rest};
split_string([C | Rest], N, L, Taken) -> split_string(Rest, N - 1, L - 1, [C | Taken]);
split_string([], _N, _L, Taken) -> {lists:reverse(Taken), ""}.

% The rest of an escape sequence after its backslash: ^X, x{...}, up to three octal digits, or one character.
split_escape([$^, C | Rest], N, L, Taken) ->
    split_string(Rest, N - 2, L - 2, [C, $^ | Taken]);
split_escape([$x, ${ | Rest], N, L, Taken) ->
    split_hex(Rest, N - 2, L - 2, [${, $x | Taken]);
split_escape([D1, D2, D3 | Rest], N, L, Taken) when ?is_octal(D1), ?is_octal(D2), ?is_octal(D3) ->
    split_string(Rest, N - 3, L - 3, [D3, D2, D1 | Taken]);
split_escape([D1, D2 | Rest], N, L, Taken) when ?is_octal(D1), ?is_octal(D2) ->
    split_string(Rest, N - 2, L - 2, [D2, D1 | Taken]);
split_escape([C | Rest], N, L, Taken) ->
    split_string(Rest, N - 1, L - 1, [C | Taken]).

split_hex([$} | Rest], N, L, Taken) ->
    split_string(Rest, N - 1, L - 1, [$} | Taken]);
split_hex([C | Rest], N, L, Taken) when C >= $0, C =< $9; C >= $a, C =< $z; C >= $A, C =< $Z ->
    split_hex(Rest, N - 1, L - 1, [C | Taken]).

% float_to_list/1's text shortened as OTP prints floats: the mantissa cut at three zeros after its first decimal,
% the exponent without its leading zeros and + sign written out.
tidy_float([$., C | Rest]) -> [$., C | tidy_mantissa(Rest)];
tidy_float([$e | _] = Exponent) -> tidy_exponent(Exponent);
tidy_float([C | Rest]) -> [C | tidy_float(Rest)];
tidy_float([]) -> [].

tidy_mantissa([$0, $0, $0 | Rest]) -> tidy_exponent(Rest);
tidy_mantissa([$e | _] = Exponent) -> tidy_exponent(Exponent);
tidy_mantissa([C | Rest]) -> [C | tidy_mantissa(Rest)];
tidy_mantissa([]) -> [].

tidy_exponent([$e, Sign, $0]) when Sign =:= $+; Sign =:= $- -> [];
tidy_exponent([$e, Sign, $0 | Rest]) when Sign =:= $+; Sign =:= $- -> tidy_exponent([$e, Sign | Rest]);
tidy_exponent([$e, Sign | _] = Exponent) when Sign =:= $+; Sign =:= $- -> Exponent;
tidy_exponent([$e | Rest]) -> tidy_exponent([$e, $+ | Rest]);
tidy_exponent([_ | Rest]) -> tidy_exponent(Rest);
tidy_exponent([]) -> [].

%% Document helpers.

text(String) -> prettypr:text(String).

floating(String) -> prettypr:floating(prettypr:text(String)).

comma() -> floating(",").

enclosed(Open, D, Close) -> prettypr:beside(floating(Open), prettypr:beside(D, floating(Close))).

parenthesized(D) -> enclosed("(", D, ")").

maybe_parentheses(D, Prec, #ctxt{prec = Outer}) when Outer > Prec -> parenthesized(D);
maybe_parentheses(D, _Prec, _Ctxt) -> D.

set_prec(Ctxt, Prec) -> Ctxt#ctxt{prec = Prec}.

reset_prec(Ctxt) -> set_prec(Ctxt, 0).

% The documents of nodes, each but the last followed by Separator; [empty()] for no nodes.
seq(Nodes, Separator, Ctxt) -> separated([lay(Node, Ctxt) || Node <- Nodes], Separator).

separated([], _Separator) -> [prettypr:empty()];
separated([D], _Separator) -> [D];
separated([D | Ds], none) -> [D | separated(Ds, none)];
separated([D | Ds], Separator) -> [prettypr:beside(D, Separator) | separated(Ds, Separator)].

vertical([D]) -> D;
vertical([D | Ds]) -> prettypr:above(D, vertical(Ds));
vertical([]) -> [].

vertical_sep(_Separator, [D]) -> D;
vertical_sep(Separator, [D | Ds]) -> prettypr:above(prettypr:above(D, Separator), vertical_sep(Separator, Ds));
vertical_sep(_Separator, []) -> [].

spaces(N) when N > 0 -> lists:duplicate(N, $\s);
spaces(_) -> [].
