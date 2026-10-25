-module(shell_docs).
-export([
    extract_type_specs/1,
    get_callback_doc/3,
    get_doc/1,
    get_doc/3,
    get_type_doc/3,
    normalize/1,
    render/2,
    render/3,
    render/4,
    render/5,
    render_callback/2,
    render_callback/3,
    render_callback/4,
    render_callback/5,
    render_type/2,
    render_type/3,
    render_type/4,
    render_type/5,
    supported_tags/0,
    validate/1
]).
-include("shell_docs.types.hrl").
-include_lib("kernel/include/eep48.hrl").

% EEP-48 documentation rendered for a shell (plan 21 step LIB74C, docs/library.md#interactive-shell), written from
% OTP's documentation and observed output: chunks of format application/erlang+html, text/markdown (converted by
% clause_shell_docs_markdown) or other text formats are normalized, then laid out as text with OTP's indentation,
% word wrapping, bullets and ANSI styles. Linked Clause code has no documentation (code:get_doc/1), so the chunks
% come from the caller.

-define(INLINE, [i, b, em, strong, code, a]).
-define(BLOCK, [p, 'div', pre, blockquote, br, ul, ol, li, dl, dt, dd, h1, h2, h3, h4, h5, h6]).
-define(IS_INLINE(E), (E =:= a orelse E =:= code orelse E =:= i orelse E =:= em orelse E =:= b orelse E =:= strong)).

% How a chunk is rendered: the documentation, its module, the output encoding, io_ansi's options and the width.
-record(config, {docs, module, encoding, ansi, columns}).

%% The tags a chunk of format application/erlang+html may use.
supported_tags() ->
    [a, p, 'div', blockquote, br, h1, h2, h3, h4, h5, h6, i, b, em, strong, pre, code, ul, ol, li, dl, dt, dd].

%% --- Validation ---

%% Check a module's (or a chunk's) application/erlang+html documentation; throws the first problem.
validate(Module) when is_atom(Module) ->
    {ok, Doc} = code:get_doc(Module),
    validate(Doc);
validate(#docs_v1{format = ?NATIVE_FORMAT, module_doc = ModuleDoc, docs = Docs}) ->
    _ = validate_docs(ModuleDoc),
    lists:foreach(
        fun({_, _, Signature, Doc, _}) ->
            case lists:all(fun erlang:is_binary/1, Signature) of
                false -> throw({invalid_signature, Signature});
                true -> ok
            end,
            validate_docs(Doc)
        end,
        Docs
    ),
    ok.

validate_docs(hidden) -> ok;
validate_docs(none) -> ok;
validate_docs(#{} = Docs) -> maps:foreach(fun(_, Doc) -> validate_docs(Doc, []) end, Docs).

validate_docs([H | T], Path) when is_tuple(H) ->
    _ = validate_docs(H, Path),
    validate_docs(T, Path);
validate_docs({br, Attr, Content} = Br, Path) ->
    case Attr =:= [] andalso Content =:= [] of
        true -> ok;
        false -> throw({content_to_allowed_in_br, Br, Path})
    end;
validate_docs({Tag, Attr, Content}, Path) ->
    validate_parent(Tag, Path),
    validate_nesting(Tag, Path),
    case lists:member(Tag, supported_tags()) of
        false -> throw({invalid_tag, Tag, Path});
        true -> ok
    end,
    case lists:all(fun({Key, Val}) -> is_atom(Key) andalso is_binary(Val) end, Attr) of
        true -> ok;
        false -> throw({invalid_attribute, {Tag, Attr}})
    end,
    validate_docs(Content, [Tag | Path]);
validate_docs([Chars | T], Path) when is_binary(Chars) ->
    validate_docs(T, Path);
validate_docs([], _) ->
    ok.

% Lists hold items only, definition lists terms and descriptions only.
validate_parent(Tag, [Parent | _] = Path) when Tag =/= li, Parent =:= ul orelse Parent =:= ol ->
    throw({only_li_allowed_within_ul_or_ol, Tag, Path});
validate_parent(Tag, [dl | _] = Path) when Tag =/= dd, Tag =/= dt ->
    throw({only_dd_or_dt_allowed_within_dl, Tag, Path});
validate_parent(_, _) ->
    ok.

% No paragraph in a paragraph, no block in preformatted text or a heading, no block in an inline element.
validate_nesting(Tag, Path) ->
    case Tag =:= p andalso lists:member(p, Path) of
        true -> throw({nested_p_not_allowed, Tag, Path});
        false -> ok
    end,
    Raw = lists:member(pre, Path) orelse lists:any(fun(H) -> lists:member(H, Path) end, [h1, h2, h3, h4, h5, h6]),
    case Raw andalso not ?IS_INLINE(Tag) of
        true -> throw({cannot_put_block_tag_within_pre, Tag, Path});
        false -> ok
    end,
    case lists:member(Tag, ?BLOCK) andalso lists:any(fun(P) -> ?IS_INLINE(P) end, Path) of
        true -> throw({cannot_put_inline_tag_outside_block, Tag, Path});
        false -> ok
    end.

%% --- Normalization ---

%% Collapse white space, trim around blocks, wrap loose inline content in paragraphs and contents in lists.
normalize(Docs) ->
    Trimmed = normalize_trim(Docs, true),
    Space = normalize_space(Trimmed),
    Paragraph = normalize_paragraph(Space),
    normalize_list(Paragraph).

% Every run of white space as one space outside preformatted text; code at the end of pre loses trailing space.
normalize_trim(Bin, true) when is_binary(Bin) ->
    NoSpace = re:replace(Bin, "[^\\S\n]*\n+[^\\S\n]*", "\n", [unicode, global]),
    NoTab = re:replace(NoSpace, "\t", " ", [unicode, global]),
    NoNewLine = re:replace(NoTab, "\\v", " ", [unicode, global]),
    re:replace(NoNewLine, "\\s+", " ", [unicode, global, {return, binary}]);
normalize_trim(Bin, false) when is_binary(Bin) ->
    Bin;
normalize_trim([{code, Attr, Content} | T], false) ->
    Trimmed =
        case lists:reverse(normalize_trim(Content, false)) of
            [Bin | Rest] when is_binary(Bin) -> lists:reverse([string:trim(Bin, trailing) | Rest]);
            Else -> lists:reverse(Else)
        end,
    [{code, Attr, Trimmed} | normalize_trim(T, false)];
normalize_trim([{pre, Attr, Content} | T], Trim) ->
    [{pre, Attr, normalize_trim(Content, false)} | normalize_trim(T, Trim)];
normalize_trim([{Tag, Attr, Content} | T], Trim) ->
    [{Tag, Attr, normalize_trim(Content, Trim)} | normalize_trim(T, Trim)];
normalize_trim([<<>> | T], Trim) ->
    normalize_trim(T, Trim);
normalize_trim([B1, B2 | T], Trim) when is_binary(B1), is_binary(B2) ->
    normalize_trim([<<B1/binary, B2/binary>> | T], Trim);
normalize_trim([H | T], Trim) ->
    [normalize_trim(H, Trim) | normalize_trim(T, Trim)];
normalize_trim([], _) ->
    [].

% Preformatted text loses its first and last newline; runs of inline content their outer and doubled spaces.
normalize_space([{pre, Attr, Content} | T]) ->
    [{pre, Attr, trim_first_and_last(Content, $\n)} | normalize_space(T)];
normalize_space([{Block, Attr, Content} | T]) when not ?IS_INLINE(Block) ->
    [{Block, Attr, normalize_space(Content)} | normalize_space(T)];
normalize_space([]) ->
    [];
normalize_space(Elems) ->
    {Inline, T} = lists:splitwith(
        fun(E) -> is_binary(E) orelse (is_tuple(E) andalso ?IS_INLINE(element(1, E))) end, Elems
    ),
    trim_inline(Inline) ++ normalize_space(T).

trim_inline(Content) ->
    {NewContent, _} = trim_inline(Content, false),
    trim_first_and_last(NewContent, $\s).

% Drop a space after a space across inline elements (TrimSpace: the text before ended with one) and empty elements
% but anchors.
trim_inline([Bin | T], false) when is_binary(Bin) ->
    EndsInSpace = binary:last(Bin) =:= $\s,
    case trim_inline(T, EndsInSpace) of
        {[B2 | NewT], State} when is_binary(B2) -> {[<<Bin/binary, B2/binary>> | NewT], State};
        {NewT, State} -> {[Bin | NewT], State}
    end;
trim_inline([<<" ">> | T], true) ->
    trim_inline(T, true);
trim_inline([<<" ", Bin/binary>> | T], true) ->
    trim_inline([Bin | T], true);
trim_inline([Bin | T], true) when is_binary(Bin) ->
    trim_inline([Bin | T], false);
trim_inline([{Elem, Attr, Content} | T], TrimSpace) ->
    {NewContent, ContentTrim} = trim_inline(Content, TrimSpace),
    {NewT, State} = trim_inline(T, ContentTrim),
    Anchor = Elem =:= a andalso proplists:is_defined(id, Attr),
    case NewContent =:= [] andalso not Anchor of
        true -> {NewT, State};
        false -> {[{Elem, Attr, NewContent} | NewT], State}
    end;
trim_inline([], TrimSpace) ->
    {[], TrimSpace}.

trim_first_and_last(Content, What) ->
    {FirstTrimmed, _} = trim_first(Content, What),
    {LastTrimmed, _} = trim_last(FirstTrimmed, What),
    LastTrimmed.

% Remove What from the start of the first text; true once some text was found.
trim_first([Bin | T], What) when is_binary(Bin) ->
    case Bin of
        <<What>> -> {T, true};
        <<What, Rest/binary>> -> {[Rest | T], true};
        _ -> {[Bin | T], true}
    end;
trim_first([{Elem, Attr, Content} = Tag | T], What) ->
    case trim_first(Content, What) of
        {[], true} ->
            {T, true};
        {NewContent, true} ->
            {[{Elem, Attr, NewContent} | T], true};
        {Content, false} ->
            {NewT, State} = trim_first(T, What),
            {[Tag | NewT], State}
    end;
trim_first([], _) ->
    {[], false}.

% Remove What from the end of the last text; true once some text was found.
trim_last([Bin | T], What) when is_binary(Bin) ->
    case trim_last(T, What) of
        {NewT, true} ->
            {[Bin | NewT], true};
        {T, false} ->
            Size = byte_size(Bin) - 1,
            case Bin of
                <<What>> -> {T, true};
                <<Text:Size/binary, What>> -> {[Text | T], true};
                _ -> {[Bin | T], true}
            end
    end;
trim_last([{Elem, Attr, Content} = Tag | T], What) ->
    case trim_last(T, What) of
        {NewT, true} ->
            {[Tag | NewT], true};
        {T, false} ->
            case trim_last(Content, What) of
                {[], true} -> {[], true};
                {NewContent, State} -> {[{Elem, Attr, NewContent} | T], State}
            end
    end;
trim_last([], _) ->
    {[], false}.

% Inline content between blocks as a paragraph.
normalize_paragraph([{Tag, _, _} = Block | T]) when not ?IS_INLINE(Tag) ->
    [Block | normalize_paragraph(T)];
normalize_paragraph([{_, _, []} = Empty | T]) ->
    [Empty | normalize_paragraph(T)];
normalize_paragraph([]) ->
    [];
normalize_paragraph(Elems) ->
    {Inline, Rest} = lists:splitwith(
        fun(E) -> is_binary(E) orelse (?IS_INLINE(element(1, E)) andalso element(3, E) =/= []) end, Elems
    ),
    [{p, [], Inline} | normalize_paragraph(Rest)].

% A binary content as a list of one.
normalize_list([{Tag, Attr, Content} | T]) when is_binary(Content) -> [{Tag, Attr, [Content]} | normalize_list(T)];
normalize_list([{Tag, Attr, Content} | T]) -> [{Tag, Attr, normalize_list(Content)} | normalize_list(T)];
normalize_list([Content | T]) when is_binary(Content) -> [Content | normalize_list(T)];
normalize_list([]) -> [].

%% --- Documentation of loaded modules ---

%% A module's documentation as chunk elements.
get_doc(Module) ->
    {ok, #docs_v1{module_doc = ModuleDoc} = D} = code:get_doc(Module),
    local_doc(Module, ModuleDoc, D).

%% The entries of a function, type or callback with their documentation as chunk elements.
get_doc(Module, Function, Arity) -> entries(Module, {function, Function, Arity}).

get_type_doc(Module, Type, Arity) -> entries(Module, {type, Type, Arity}).

get_callback_doc(Module, Callback, Arity) -> entries(Module, {callback, Callback, Arity}).

entries(Module, KNA) ->
    {ok, #docs_v1{docs = Docs} = D} = code:get_doc(Module),
    [{F, A, S, local_doc(F, Dc, D), M} || {F, A, S, Dc, M} <- filter(KNA, Docs)].

%% The specs, types and callbacks of a module's abstract code; none for linked Clause modules.
extract_type_specs(Module) ->
    Path =
        case {get({?MODULE, nospecs}), code:which(Module)} of
            {undefined, preloaded} -> filename:join([code:lib_dir(erts), "ebin", atom_to_list(Module) ++ ".beam"]);
            {undefined, Which} -> Which;
            {Other, _} -> Other
        end,
    try beam_lib:chunks(Path, [abstract_code]) of
        {ok, {Module, [{abstract_code, {raw_abstract_v1, AST}}]}} ->
            lists:foldl(fun exported_spec/2, #{type => #{}, function => #{}, callback => #{}}, AST);
        _ ->
            #{}
    catch
        _:_ -> #{}
    end.

exported_spec({attribute, _, Kind, {Name, _, Args}} = Spec, #{type := Types} = Acc) when
    Kind =:= type; Kind =:= opaque
->
    Acc#{type => Types#{{Name, length(Args)} => Spec}};
exported_spec({attribute, _, spec, {{Name, Arity}, _}} = Spec, #{function := Functions} = Acc) ->
    Acc#{function => Functions#{{Name, Arity} => Spec}};
exported_spec({attribute, Anno, spec, {{_, Name, Arity}, Args}}, Acc) ->
    exported_spec({attribute, Anno, spec, {{Name, Arity}, Args}}, Acc);
exported_spec({attribute, _, callback, {{Name, Arity}, _}} = Spec, #{callback := Callbacks} = Acc) ->
    Acc#{function => Callbacks#{{Name, Arity} => Spec}};
exported_spec(_, Acc) ->
    Acc.

%% --- Rendering ---

%% A module's documentation.
render(Module, #docs_v1{} = D) when is_atom(Module) ->
    render(Module, D, #{}).

render(Module, #docs_v1{module_doc = ModuleDoc} = D, Config) when is_atom(Module), is_map(Config) ->
    Header = [{h2, [], [<<"\t", (atom_to_binary(Module))/binary>>]}],
    headers_and_docs([Header], local_doc(Module, ModuleDoc, D), D, Config#{module => Module});
render(Module, Function, #docs_v1{} = D) ->
    render(Module, Function, D, #{}).

%% A function's documentation, of every arity or of one.
render(Module, Function, D, Config) when is_atom(Module), is_atom(Function), is_map(Config) ->
    renderer(Config, {Module, function, Function, none}, D);
render(Module, Function, Arity, D) ->
    renderer(#{}, {Module, function, Function, Arity}, D).

render(Module, Function, Arity, #docs_v1{} = D, Config) when
    is_atom(Module), is_atom(Function), is_integer(Arity), is_map(Config)
->
    renderer(Config, {Module, function, Function, Arity}, D).

%% A module's types (a listing) or one type's documentation.
render_type(Module, D) -> render_type(Module, D, #{}).

render_type(Module, #docs_v1{} = D, Config) -> signature_listing(Module, type, D, Config);
render_type(Module, Type, #docs_v1{} = D) -> render_type(Module, Type, D, #{}).

render_type(Module, Type, #docs_v1{} = D, Config) -> renderer(Config, {Module, type, Type, none}, D);
render_type(Module, Type, Arity, #docs_v1{} = D) -> renderer(#{}, {Module, type, Type, Arity}, D).

render_type(Module, Type, Arity, #docs_v1{} = D, Config) -> renderer(Config, {Module, type, Type, Arity}, D).

%% A module's callbacks (a listing) or one callback's documentation.
render_callback(Module, D) -> render_callback(Module, D, #{}).

render_callback(Module, Callback, #docs_v1{} = D) -> render_callback(Module, Callback, D, #{});
render_callback(Module, D, Config) -> signature_listing(Module, callback, D, Config).

render_callback(Module, Callback, Arity, #docs_v1{} = D) -> renderer(#{}, {Module, callback, Callback, Arity}, D);
render_callback(Module, Callback, #docs_v1{} = D, Config) -> renderer(Config, {Module, callback, Callback, none}, D).

render_callback(Module, Callback, Arity, #docs_v1{} = D, Config) ->
    renderer(Config, {Module, callback, Callback, Arity}, D).

renderer(Config0, {Module, Kind, Name, Arity}, #docs_v1{docs = Docs} = D) ->
    Config = Config0#{module => Module},
    Entries = filter({Kind, Name, Arity}, Docs),
    case Kind of
        function -> render_function(Entries, D, Config);
        _ -> render_entries(Entries, D, Config)
    end.

filter({Kind, Name, none}, Docs) -> [E || {{K, F, _}, _, _, _, _} = E <- Docs, K =:= Kind, F =:= Name];
filter({Kind, Name, Arity}, Docs) -> [E || {{K, F, A}, _, _, _, _} = E <- Docs, K =:= Kind, F =:= Name, A =:= Arity].

% A function's entries, those with the same equiv metadata rendered under their common documentation.
render_function([], _D, _Config) ->
    {error, function_missing};
render_function(Entries, #docs_v1{docs = Docs} = D, Config0) ->
    Config = init_config(D, Config0),
    Specs = extract_type_specs(Config#config.module),
    Groups = lists:foldr(
        fun(E, Acc) -> group(E, Specs, Acc) end,
        [],
        lists:sort(fun(A, B) -> element(1, A) =< element(1, B) end, Entries)
    ),
    [
        headers_and_docs(Signatures, local_doc(Group, group_doc(Group, Members, Docs), D), Config)
     || {Group, Members, Signatures} <- Groups
    ].

group({_, _, _, _, #{equiv := Group}} = E, Specs, Acc) ->
    case lists:keytake(Group, 1, Acc) of
        false ->
            [{Group, [E], signature(E, Specs)} | Acc];
        {value, {Group, Members, Signatures}, Rest} ->
            [{Group, [E | Members], signature(E, Specs) ++ Signatures} | Rest]
    end;
group({Group, _, _, _, _} = E, Specs, Acc) ->
    [{Group, [E], signature(E, Specs)} | Acc].

% The first member's documentation, else the group's own entry's.
group_doc(Group, Members, Docs) ->
    case lists:search(fun({_, _, _, Doc, _}) -> Doc =/= #{} end, Members) of
        {value, {_, _, _, Doc, _}} ->
            Doc;
        false ->
            case lists:keyfind(Group, 1, Docs) of
                false -> none;
                {_, _, _, Doc, _} -> Doc
            end
    end.

% A type's or callback's entries (OTP answers type_missing for a missing callback too).
render_entries([], _D, _Config) ->
    {error, type_missing};
render_entries(Entries, D, Config) ->
    C = init_config(D, Config),
    Specs = extract_type_specs(C#config.module),
    [headers_and_docs(signature(E, Specs), local_doc(F, Docs, D), C) || {F, _, _, Docs, _} = E <- Entries].

% The headers of an entry: the signature metadata's specs, a spec of the abstract code, or the signature lines.
signature({_, _, _, _, #{signature := Specs} = Meta}, _) ->
    lists:map(fun render_ast/1, Specs) ++ [render_meta(Meta)];
signature({{Type, F, A}, _, Sigs, _, Meta}, Specs) ->
    case maps:find({F, A}, maps:get(Type, Specs, #{})) of
        {ok, Spec} -> [render_ast(Spec) | render_meta(Meta)];
        % Indented by two no-break spaces, as OTP's.
        error -> [{h2, [], [<<"\x{A0}\x{A0}"/utf8, Sig/binary>>]} || Sig <- Sigs] ++ [render_meta(Meta)]
    end.

render_ast(AST) ->
    Text = erl_pp:attribute(AST, [{encoding, unicode}]),
    Spec =
        case AST of
            {_, _, opaque, _} -> hd(string:split(Text, "::"));
            _ -> Text
        end,
    {pre, [], [{strong, [], unicode:characters_to_binary(string:trim(Spec, trailing, "\n"))}]}.

render_meta(Meta) ->
    case meta(Meta) of
        [] -> [];
        Items -> [[{dl, [], Items}]]
    end.

meta(#{since := Vsn} = M) ->
    [{dt, [], <<"Since">>}, {dd, [], [unicode:characters_to_binary(Vsn)]} | meta(maps:remove(since, M))];
meta(#{deprecated := Depr} = M) ->
    [{dt, [], <<"Deprecated">>}, {dd, [], [unicode:characters_to_binary(Depr)]} | meta(maps:remove(deprecated, M))];
meta(_) ->
    [].

% The types or callbacks a module documents, or that it has none.
signature_listing(Module, Kind, D, Config0) ->
    Config = (init_config(D, Config0))#config{module = Module},
    #docs_v1{docs = Docs, module_doc = ModuleDoc} = D,
    Slogan = [{h2, [], [<<"\t", (atom_to_binary(Module))/binary>>]}, {br, [], []}],
    Name = atom_to_binary(Kind),
    case [E || {{K, _, _}, _, _, _, _} = E <- Docs, K =:= Kind, is_map(ModuleDoc)] of
        [] ->
            render_docs(Slogan ++ [<<"There are no ", Name/binary, "s in this module">>], 0, Config);
        Entries ->
            Specs = extract_type_specs(Module),
            Headers = lists:flatmap(fun(E) -> [{br, [], []}, signature(E, Specs)] end, Entries),
            Listing = [{p, [], [<<"These ", Name/binary, "s are documented in this module:">>]}, {br, [], []}, Headers],
            render_docs(Slogan ++ Listing, 0, Config)
    end.

% The documentation of Missing (a module, {F, A} or {Kind, F, A}) as normalized chunk elements.
local_doc(Missing, Docs, D) when is_atom(Missing) ->
    local_doc(atom_to_binary(Missing), Docs, D);
local_doc({F, A}, Docs, D) ->
    local_doc(unicode:characters_to_binary(io_lib:format("~tp/~p", [F, A])), Docs, D);
local_doc({_, F, A}, Docs, D) ->
    local_doc({F, A}, Docs, D);
local_doc(_, #{<<"en">> := Docs}, D) ->
    normalize_format(Docs, D);
local_doc(_, Docs, D) when is_map(Docs), map_size(Docs) > 0 ->
    normalize_format(maps:get(hd(maps:keys(Docs)), Docs), D);
local_doc(Missing, hidden, _) ->
    [
        {p, [], [
            <<"The documentation for ">>,
            Missing,
            <<" is hidden. This probably means that it is internal and not to be used by other applications.">>
        ]}
    ];
local_doc(Missing, None, _) when None =:= none; None =:= #{} ->
    [{p, [], [<<"There is no documentation for ">>, Missing]}].

normalize_format(Docs, #docs_v1{format = ?NATIVE_FORMAT}) ->
    normalize(Docs);
normalize_format(Docs, #docs_v1{format = <<"text/markdown">>}) when is_binary(Docs) ->
    normalize(clause_shell_docs_markdown:parse_md(Docs));
normalize_format(Docs, #docs_v1{format = <<"text/", _/binary>>}) when is_binary(Docs) -> [{pre, [], [Docs]}].

% The headers, each after a line break, then the documentation indented by two.
headers_and_docs(Headers, Contents, D, Config) ->
    headers_and_docs(Headers, Contents, init_config(D, Config)).

headers_and_docs(Headers, Contents, #config{} = Config) ->
    unicode:characters_to_list([
        "\n",
        render_docs(lists:flatmap(fun(Header) -> [{br, [], []}, Header] end, Headers), 0, Config),
        "\n",
        render_docs(Contents, 2, Config)
    ]).

init_config(D, #config{} = Config) ->
    Config#config{docs = D};
init_config(D, Config) when is_map(Config) ->
    Ansi =
        case maps:get(ansi, Config, undefined) of
            undefined ->
                case application:get_env(kernel, shell_docs_ansi) of
                    {ok, Enabled} when is_boolean(Enabled) -> [{enabled, Enabled}];
                    _ -> []
                end;
            Enabled when is_boolean(Enabled) ->
                [{enabled, Enabled}]
        end,
    #config{
        docs = D,
        encoding = maps:get(encoding, Config, proplists:get_value(encoding, io:getopts(), latin1)),
        ansi = Ansi,
        columns = maps:get(columns, Config, columns()),
        module = maps:get(module, Config, undefined)
    }.

columns() ->
    case application:get_env(stdlib, shell_docs_columns) of
        {ok, C} when is_integer(C), C > 0 ->
            C;
        _ ->
            case io:columns() of
                {ok, C} -> C;
                _ -> 80
            end
    end.

% Lay the elements out from indentation Ind and write the styles they carry through io_ansi.
render_docs(Contents, Ind, #config{} = Config) ->
    put(ansi, []),
    try
        {Doc, _} = trimnl(docs(Contents, [], 0, Ind, Config)),
        io_ansi:format(Doc, [], [{reset, false} | Config#config.ansi])
    after
        erase(ansi)
    end.

%% --- Layout ---

% {Chardata, Pos} of elements laid out from column Pos with indentation Ind; Stack holds the enclosing tags.
docs(Elems, Stack, Pos, Ind, C) when is_list(Elems) ->
    lists:mapfoldl(fun(E, P) -> docs(E, Stack, P, Ind, C) end, Pos, Elems);
docs(Elem, Stack, Pos, Ind, C) ->
    layout(Elem, Stack, Pos, Ind, C).

layout({a, _, Content}, S, Pos, Ind, C) ->
    docs(Content, S, Pos, Ind, C);
layout({h1, _, Content}, S, 0, _Ind, C) ->
    double_nl(layout({code, [], [{strong, [], Content}]}, S, 0, 0, C));
layout({h2, _, Content}, S, 0, _Ind, C) ->
    double_nl(layout({strong, [], Content}, S, 0, 0, C));
layout({H, _, Content}, S, Pos, _Ind, C) when Pos =< 2, H =:= h3 orelse H =:= h4 orelse H =:= h5 orelse H =:= h6 ->
    double_nl(layout({code, [], Content}, S, Pos, 2, C));
layout({pre, _, _} = E, S, Pos, Ind, C) when Pos > Ind ->
    {Docs, NewPos} = layout(E, S, 0, Ind, C),
    {["\n\n", Docs], NewPos};
layout({Elem, _, _} = E, S, Pos, Ind, C) when Pos > Ind, not ?IS_INLINE(Elem) ->
    {Docs, NewPos} = layout(E, S, 0, Ind, C),
    {["\n", Docs], NewPos};
layout({'div', [{class, What}], Content}, S, Pos, Ind, C) ->
    {Docs, _} = docs(Content, ['div' | S], 0, Ind + 2, C),
    double_nl([pad(Ind - Pos), string:titlecase(What), ":\n", Docs]);
layout({blockquote, _, Content}, S, _Pos, Ind, C) ->
    double_nl(docs(Content, ['div' | S], 0, Ind + 2, C));
layout({Tag, _, Content}, S, Pos, Ind, C) when Tag =:= p; Tag =:= 'div' ->
    double_nl(docs(Content, [Tag | S], Pos, Ind, C));
layout(Elem, S, Pos, Ind, C) when Pos < Ind ->
    {Docs, NewPos} = layout(Elem, S, Ind, Ind, C),
    {[pad(Ind - Pos), Docs], NewPos};
layout(Elem, S, Pos, Ind, C) ->
    inline(Elem, S, Pos, Ind, C).

% Elements at or past the indentation: styles, preformatted text, lists, definitions and words.
inline({code, _, Content}, [pre | _] = S, Pos, Ind, C) ->
    docs(Content, [code | S], Pos, Ind, C);
inline({code, _, Content}, S, Pos, Ind, C) ->
    styled(underline, Content, [code | S], {Pos, Ind}, C);
inline({em, Attr, Content}, S, Pos, Ind, C) ->
    inline({i, Attr, Content}, S, Pos, Ind, C);
inline({i, _, Content}, S, Pos, Ind, C) ->
    docs(Content, S, Pos, Ind, C);
inline({br, [], []}, _S, Pos, _Ind, _C) ->
    {"", Pos};
inline({strong, Attr, Content}, S, Pos, Ind, C) ->
    inline({b, Attr, Content}, S, Pos, Ind, C);
inline({b, _, Content}, S, Pos, Ind, C) ->
    styled(bold, Content, S, {Pos, Ind}, C);
inline({pre, _, Content}, S, Pos, Ind, C) ->
    double_nl(docs(Content, [pre | S], Pos, Ind + 2, C));
inline({ul, [{class, <<"types">>}], Content}, S, _Pos, Ind, C) ->
    {Docs, _} = docs(Content, [types | S], 0, Ind + 2, C),
    double_nl(["Types:\n", Docs]);
inline({li, Attr, Content}, [types | _] = S, Pos, Ind, C) ->
    single_nl(type_item(Attr, Content, S, {Pos, Ind}, C));
inline({List, [], Content}, S, Pos, Ind, C) when List =:= ul; List =:= ol ->
    docs(Content, [l | S], Pos, Ind, C);
inline({li, [], Content}, [l | _] = S, Pos, Ind, C) ->
    Bullet = bullet(S, C#config.encoding),
    Len = string:length(Bullet),
    {Docs, _} = docs(Content, [li | S], Pos + Len, Ind + Len, C),
    double_nl([Bullet, Docs]);
inline({dl, _, Content}, S, Pos, Ind, C) ->
    docs(Content, [dl | S], Pos, Ind, C);
inline({dt, Attr, Content}, [dl | _] = S, Pos, Ind, C) ->
    Since =
        case Attr of
            [{since, Vsn}] -> ["     (since ", unicode:characters_to_list(Vsn), $)];
            [] -> []
        end,
    Underline = sansi(underline),
    {Docs, _} = docs(Content, [li | S], Pos, Ind, C),
    {[Underline, Docs, ransi(underline), $:, Since, $\n], 0};
inline({dd, _, Content}, [dl | _] = S, Pos, Ind, C) ->
    double_nl(docs(Content, [li | S], Pos, Ind + 2, C));
inline(B, S, Pos, Ind, C) when is_binary(B) ->
    case lists:member(pre, S) of
        true ->
            Pre = string:replace(B, "\n", [nlpad(Ind)], all),
            {Pre, Pos + lastline(Pre)};
        false ->
            words(binary:split(B, [<<" ">>], [global]), S, Pos, Ind, [[]], C)
    end;
inline({Tag, Attr, Content}, S, Pos, Ind, C) ->
    case lists:member(Tag, supported_tags()) of
        true -> throw({unhandled_element, Tag, Attr, Content});
        false -> docs(Content, S, Pos, Ind, C)
    end.

% Content between an ANSI style's start and its end.
styled(Style, Content, S, {Pos, Ind}, C) ->
    Start = sansi(Style),
    {Docs, NewPos} = docs(Content, S, Pos, Ind, C),
    {[Start, Docs, ransi(Style)], NewPos}.

% An item of a types list: a named type's signature from the metadata, a description, or its content.
type_item(Attr, Content, S, {Pos, Ind}, C) ->
    case {proplists:get_value(name, Attr), proplists:get_value(class, Attr)} of
        {undefined, Class} when Class =:= undefined; Class =:= <<"type">> ->
            docs(Content, [type | S], Pos, Ind, C);
        {_, <<"description">>} ->
            docs(Content, [type | S], Pos, Ind + 2, C);
        {Name, _} ->
            case type_signature(binary_to_atom(Name), C) of
                undefined when Content =:= [] -> {["-type ", Name, "() :: term()."], 0};
                undefined -> docs(Content, [type | S], Pos, Ind, C);
                Type -> {Type, 0}
            end
    end.

type_signature(Name, #config{docs = #docs_v1{metadata = #{types := AllTypes}}}) ->
    case [Type || {TName, _} = Type <- maps:keys(AllTypes), TName =:= Name] of
        [] -> undefined;
        Types -> [erl_pp:attribute(maps:get(Type, AllTypes)) || Type <- Types]
    end.

% Words filling lines of Columns - 10 - Ind characters; a single punctuation character never starts a line.
words(Words, [_, types | S], Pos, Ind, Acc, C) ->
    words(Words, S, Pos, Ind + 2, Acc, C);
words([Unicode | T], S, Pos, Ind, Acc, #config{columns = Cols} = C) when is_binary(Unicode) ->
    Word = translate(Unicode, C),
    Len = string:length(Word),
    NewPos = Len + Pos,
    Punct = re:run(Word, "^\\W$", [unicode]) =/= nomatch,
    case NewPos > (Cols - 10 - Ind) andalso Word =/= <<>> andalso not Punct of
        true ->
            words(T, S, Len + Ind + 1, Ind, [[[nlpad(Ind), Word]] | Acc], C);
        false ->
            [Line | Lines] = Acc,
            words(T, S, NewPos + 1, Ind, [[Word | Line] | Lines], C)
    end;
words([], _S, Pos, _Ind, Acc, _C) ->
    {[lists:join($\s, lists:reverse(Line)) || Line <- lists:reverse(Acc)], Pos}.

% Latin-1 output shows a no-break space as a space.
translate(Word, #config{encoding = unicode}) -> Word;
translate(Word, #config{encoding = latin1}) -> string:replace(Word, [160], " ", all).

bullet(_S, latin1) ->
    <<" * ">>;
bullet(S, unicode) ->
    case length([l || l <- S]) of
        Level when Level > 4 -> bullet(S, latin1);
        Level -> lists:nth(Level, [<<" • "/utf8>>, <<" ￮ "/utf8>>, <<" ◼ "/utf8>>, <<" ◻ "/utf8>>])
    end.

% Spaces that interrupt the current style, after Extra (a newline).
pad(N) -> pad(N, "").

nlpad(N) -> pad(N, "\n").

pad(N, Extra) ->
    Pad = lists:duplicate(N, " "),
    case ansi() of
        reset -> [Extra, Pad];
        Ansi -> [reset, Extra, Pad, Ansi]
    end.

% The characters after the last newline of laid out text.
lastline(Text) ->
    lastline(lists:reverse(segments(Text)), 0).

lastline([Seg | T], Size) when is_atom(Seg); is_tuple(Seg) ->
    lastline(T, Size);
lastline([Seg | T], Size) ->
    case string:find(Seg, "\n", trailing) of
        nomatch -> lastline(T, string:length(Seg) + Size);
        Match -> string:length(tl(string:next_codepoint(Match))) + Size
    end;
lastline([], Size) ->
    Size.

% Text ending with exactly one or two newlines; the position starts a line.
double_nl({Chars, _Pos}) -> double_nl(Chars);
double_nl(Chars) -> {[[trim_newlines(Chars), "\n"], "\n"], 0}.

single_nl({Chars, _Pos}) -> {[trim_newlines(Chars), "\n"], 0}.

trimnl(Laid) -> single_nl(Laid).

% Trailing newlines of the last text, unless a style ends the text.
trim_newlines(Chars) -> trim_last_segment(segments(Chars)).

trim_last_segment([H]) when is_atom(H); is_tuple(H) -> [H];
trim_last_segment([H]) -> [string:trim([H], trailing, "\n")];
trim_last_segment([H | T]) -> [H | trim_last_segment(T)];
trim_last_segment([]) -> [].

% Laid out text as binaries between its style atoms.
segments(Chars) -> segments(lists:flatten(Chars), []).

segments([], Acc) ->
    lists:reverse(Acc);
segments(L, Acc) ->
    case lists:splitwith(fun(X) -> not (is_atom(X) orelse is_tuple(X)) end, L) of
        {Str, []} -> lists:reverse([unicode:characters_to_binary(Str) | Acc]);
        {[], [Ansi | T]} -> segments(T, [Ansi | Acc]);
        {Str, [Ansi | T]} -> segments(T, [Ansi, unicode:characters_to_binary(Str) | Acc])
    end.

% The styles in effect, kept in the process dictionary while a text is laid out: reset when none.
sansi(Style) ->
    put(ansi, [Style | get(ansi)]),
    ansi().

ransi(Style) ->
    put(ansi, proplists:delete(Style, get(ansi))),
    ansi().

ansi() ->
    case lists:usort(get(ansi)) of
        [] -> reset;
        Styles -> Styles
    end.
