-module(clause_shell_docs_markdown).
-export([parse_md/1, parse_md/2]).

% Markdown documentation as shell_docs chunk elements (plan 21 step LIB74C, docs/library.md#interactive-shell),
% reproducing the conversion OTP's shell uses for text/markdown chunks: ATX and setext headings, paragraphs,
% bullet and numbered lists, block quotes, fenced and indented code, tables (kept as preformatted text), HTML
% comments, and inline code, emphasis (* or _ is i, ** or __ em), escapes and links reduced to their text.

-define(IS_BULLET(X), (X =:= $* orelse X =:= $- orelse X =:= $+)).
-define(IS_FORMAT(X), (X =:= $* orelse X =:= $_)).
-define(IS_PUNCT(X), (X =:= $. orelse X =:= $, orelse X =:= $( orelse X =:= $) orelse X =:= $: orelse X =:= $;)).
-define(IS_BREAK(X), (X =:= $\s orelse X =:= $\n orelse X =:= <<>>)).
-define(IS_SEPARATOR(X), (?IS_BREAK(X) orelse ?IS_PUNCT(X))).
-define(IS_ESCAPED(C),
    (C >= $! andalso C =< $/ orelse C >= $: andalso C =< $@ orelse C >= $[ andalso C =< $` orelse
        C >= ${ andalso C =< $~)
).
% A character next to a format character inside a word.
-define(IS_WORD(C, F),
    (C =/= F andalso C =/= $\s andalso C =/= $\n andalso not ?IS_FORMAT(C) andalso not ?IS_PUNCT(C))
).
-define(IS_CODE_PREFIX(X), (X =:= $t orelse X =:= $m orelse X =:= $e orelse X =:= $c)).

%% The chunk elements of a markdown text.
parse_md(Doc) when is_binary(Doc) ->
    parse_md(Doc, #{}).

%% The same; allow_html => true accepts lines that start with an HTML tag (else they exit).
parse_md(Doc, Opts) ->
    Unreferenced = re:replace(Doc, "\\[([^\\]]*?)\\]\\[(.*?)\\]", "\\1", [{return, binary}, global]),
    Lines = binary:split(Unreferenced, [<<"\r\n">>, <<"\n">>], [global]),
    format_lines(lines(Lines, [], Opts), sets:new()).

% Loose text as inline-formatted paragraphs, except in headings and preformatted text; empty elements dropped.
format_lines([], _Within) ->
    [];
format_lines([{Tag, Attrs, Content} | Rest], Within) ->
    case format_lines(Content, sets:add_element(Tag, Within)) of
        [] -> format_lines(Rest, Within);
        Formatted -> [{Tag, Attrs, Formatted} | format_lines(Rest, Within)]
    end;
format_lines([Bin | Rest], Within) when is_binary(Bin) ->
    case sets:is_disjoint(sets:from_list([h1, h2, h3, h4, h5, h6, pre]), Within) of
        true ->
            case inline({p, [], [strip_leading(Bin)]}) of
                {p, [], []} -> format_lines(Rest, Within);
                P -> [P | format_lines(Rest, Within)]
            end;
        false ->
            [Bin | format_lines(Rest, Within)]
    end.

strip_leading(<<$\s, Line/binary>>) -> strip_leading(Line);
strip_leading(Line) -> Line.

%% --- Blocks ---

% The blocks of lines after Block (a paragraph text being joined, or blocks already made).
lines([], Block, _Opts) ->
    Block;
lines([<<"    ", _/binary>> | _] = Lines, Block, Opts) ->
    Block ++ indented_code(Lines, [], Opts);
lines([Line | Rest], Block, Opts) ->
    {Stripped, Spaces} = strip_spaces(Line, 0, infinity),
    case list_kind(Stripped) of
        p ->
            kind_block([Stripped | Rest], Block, Opts);
        Kind ->
            {Content, After} = list(Kind, [Line | Rest], Spaces, Block, Opts),
            Content ++ lines(After, [], Opts)
    end.

list_kind(<<B, $\s, _/binary>>) when ?IS_BULLET(B) -> ul;
list_kind(<<_, $., $\s, _/binary>>) -> ol;
list_kind(_) -> p.

kind_block([<<" ", Line/binary>> | Rest], Block, Opts) ->
    kind_block([Line | Rest], Block, Opts);
kind_block([<<"#", _/binary>> = Line | Rest], Block, Opts) ->
    case heading_level(Line, 0) of
        {Level, Text} when Level =< 6 -> Block ++ [inline({heading(Level), [], [Text]}) | lines(Rest, [], Opts)];
        _ -> paragraph([Line | Rest], Block, Opts)
    end;
kind_block([<<">", _/binary>> | _] = Lines, Block, Opts) ->
    Block ++ quote(Lines, [], Opts);
kind_block([<<"```", Language/binary>> | Rest], Block, Opts) ->
    Block ++ fenced_code(Rest, [], Language, Opts);
kind_block([<<>> | Rest], Block, Opts) ->
    Block ++ lines(Rest, [], Opts);
kind_block([<<"<!--", Line/binary>> | Rest], Block, Opts) ->
    Block ++ lines(after_comment([Line | Rest]), [], Opts);
kind_block([<<$|, _/binary>> | _] = Lines, Block, Opts) ->
    case table(Lines) of
        {Table, After} -> Block ++ [Table | lines(After, [], Opts)];
        error -> paragraph(Lines, Block, Opts)
    end;
kind_block([Line | _] = Lines, Block, Opts) ->
    case not maps:get(allow_html, Opts, false) andalso is_html(Line) of
        true -> exit({html_not_supported, {"Inline HTML is not supported in shell_docs markdown parsing", Line}});
        false -> paragraph(Lines, Block, Opts)
    end.

% "#", "##", ... followed by a space: the level and the heading text.
heading_level(<<"#", Rest/binary>>, Level) -> heading_level(Rest, Level + 1);
heading_level(<<" ", Text/binary>>, Level) when Level > 0 -> {Level, Text};
heading_level(_, _) -> none.

heading(1) -> h1;
heading(2) -> h2;
heading(3) -> h3;
heading(4) -> h4;
heading(5) -> h5;
heading(6) -> h6.

% A paragraph line joined to the one before it; a line of = or - under a single line makes it a heading.
paragraph(Lines, Block, Opts) ->
    case join(Lines, Block) of
        {[_ | _] = Rest, [Text] = Joined} -> setext(Rest, Text, Joined, Opts);
        {Rest, Joined} -> lines(Rest, Joined, Opts)
    end.

join([<<>>], Block) ->
    {[], Block};
join([P | Rest], []) ->
    {Rest, [P]};
join([<<$\s, P/binary>> | Rest], [Bin | _] = Block) when is_binary(Bin) -> join([P | Rest], Block);
join([P | Rest], [Bin | Block]) when is_binary(Bin) ->
    case binary:last(Bin) of
        $\s -> {Rest, [<<Bin/binary, P/binary>> | Block]};
        _ -> {Rest, [<<Bin/binary, $\s, P/binary>> | Block]}
    end.

setext([Line | Remaining] = Rest, Text, Joined, Opts) ->
    case strip_spaces(Line, 0, infinity) of
        {Stripped, Spaces} when Spaces =:= 4; Stripped =:= <<>> ->
            lines(Rest, Joined, Opts);
        {<<Symbol, _/binary>> = Stripped, _} when Symbol =:= $=; Symbol =:= $- ->
            case repeated(Stripped, Symbol) of
                true ->
                    Prefix =
                        case Symbol of
                            $= -> <<"# ">>;
                            $- -> <<"## ">>
                        end,
                    lines([<<Prefix/binary, Text/binary>>], [], Opts) ++ lines(Remaining, [], Opts);
                false ->
                    lines(Rest, Joined, Opts)
            end;
        _ ->
            lines(Rest, Joined, Opts)
    end.

repeated(<<>>, _) -> true;
repeated(<<C, Rest/binary>>, C) -> repeated(Rest, C);
repeated(_, _) -> false.

strip_spaces(<<" ", Rest/binary>>, Count, Max) when Max =:= infinity; Count < Max -> strip_spaces(Rest, Count + 1, Max);
strip_spaces(Rest, Count, _) -> {Rest, Count}.

% Lines of a block quote ("> " or ">" markers, also nested ones), parsed as blocks of their own.
quote([<<">">> | Rest], Lines, Opts) -> quote(Rest, Lines, Opts);
quote([<<"> ", Line/binary>> | Rest], Lines, Opts) -> quote(Rest, [Line | Lines], Opts);
quote([<<">> ", Line/binary>> | Rest], Lines, Opts) -> quote([<<"> ", Line/binary>> | Rest], Lines, Opts);
quote([<<">", Line/binary>> | Rest], Lines, Opts) -> quote([<<"> ", Line/binary>> | Rest], Lines, Opts);
quote(Rest, Lines, Opts) -> [{blockquote, [], lines(lists:reverse(Lines), [], Opts)} | lines(Rest, [], Opts)].

% Lines indented by four spaces as code.
indented_code([<<"    ", Line/binary>> | Rest], Lines, Opts) -> indented_code(Rest, [Line | Lines], Opts);
indented_code(Rest, Lines, Opts) -> [code(Lines, []) | lines(Rest, [], Opts)].

% Lines up to a closing fence as code, its language from the opening fence.
fenced_code(Lines, Code, Language, Opts) ->
    case Lines of
        [] ->
            [fence_code(Code, Language)];
        [<<"```">> | Rest] ->
            [fence_code(Code, Language) | lines(Rest, [], Opts)];
        [Line | Rest] ->
            case strip_spaces(Line, 0, infinity) of
                {<<"```", After/binary>>, _} ->
                    case strip_spaces(After, 0, infinity) of
                        {<<>>, _} -> fenced_code([<<"```">> | Rest], Code, Language, Opts);
                        _ -> fenced_code(Rest, [Line | Code], Language, Opts)
                    end;
                _ ->
                    fenced_code(Rest, [Line | Code], Language, Opts)
            end
    end.

fence_code(Code, Leading) ->
    case string:trim(hd(binary:split(Leading, [<<"\t">>, <<" ">>]))) of
        <<>> -> code(Code, []);
        Language -> code(Code, [{class, <<"language-", Language/binary>>}])
    end.

% Code lines (reversed) as preformatted code, each line ending in a newline.
code([], Attrs) ->
    {pre, [], [{code, Attrs, [<<"\n">>]}]};
code(Lines, Attrs) ->
    Text = lists:foldl(
        fun(Line, Acc) ->
            Clean = re:replace(Line, "(\n|\r\n)", <<>>, [{return, binary}, global]),
            <<Clean/binary, "\n", Acc/binary>>
        end,
        <<>>,
        Lines
    ),
    {pre, [], [{code, Attrs, [Text]}]}.

% The lines after an HTML comment's end.
after_comment([]) ->
    [];
after_comment([Line | Rest]) ->
    case binary:split(Line, <<"-->">>) of
        [_] ->
            case after_comment(Rest) of
                [] -> error(missing_close_comment);
                After -> After
            end;
        [_, Text] ->
            [Text | Rest]
    end.

is_html(Line) ->
    case re:run(Line, "^</?([^>]+)>", [{capture, all_but_first, list}]) of
        {match, [Tag]} -> lists:member(Tag, html_tags());
        nomatch -> false
    end.

html_tags() ->
    [
        "header",
        "footer",
        "main",
        "section",
        "article",
        "nav",
        "aside",
        "h1",
        "h2",
        "h3",
        "h4",
        "h5",
        "h6",
        "p",
        "hr",
        "pre",
        "blockquote",
        "ol",
        "ul",
        "li",
        "dl",
        "dt",
        "dd",
        "figure",
        "figcaption",
        "div",
        "a",
        "em",
        "strong",
        "small",
        "cite",
        "code",
        "sub",
        "sup",
        "i",
        "b",
        "u",
        "mark",
        "span",
        "br",
        "img",
        "iframe",
        "video",
        "audio",
        "source",
        "canvas",
        "svg",
        "table",
        "thead",
        "tbody",
        "tr",
        "td",
        "th",
        "form",
        "label",
        "input",
        "button",
        "select",
        "option",
        "textarea",
        "fieldset",
        "legend",
        "progress",
        "details",
        "summary",
        "script",
        "noscript"
    ].

%% --- Tables ---

% A header row, a delimiter row with as many fields and the body rows (up to a blank line or another kind of line)
% as preformatted table text; error when the lines are no table.
table([Header, Delimiter | Rest]) ->
    case {fields(Header), delimiter(Delimiter)} of
        {{true, N}, {true, N}} ->
            case body(Rest, N) of
                {Rows, After} ->
                    Text = [<<Line/binary, "\n">> || Line <- [Header, Delimiter | Rows]],
                    {{pre, [], [{code, [{class, <<"table">>}], Text}]}, After};
                false ->
                    error
            end;
        _ ->
            error
    end;
table(_) ->
    error.

% {true, Fields} of a row between bars; false for any other line.
fields(Line) ->
    Packed = re:replace(Line, "(\\s)", <<>>, [{return, binary}, global]),
    case binary:first(Packed) =:= $| andalso binary:last(Packed) =:= $| of
        true ->
            Escaped = length(binary:matches(Line, <<"\\|">>)),
            {true, length(binary:matches(Line, <<"|">>)) - Escaped - 1};
        false ->
            false
    end.

delimiter(<<$|, Line/binary>>) ->
    delimiter(re:replace(Line, "(\\s|:)", <<>>, [{return, binary}, global]), 0);
delimiter(_) ->
    false.

delimiter(<<>>, Count) -> {true, Count};
delimiter(<<$|, Line/binary>>, Count) -> delimiter(Line, Count + 1);
delimiter(<<$-, Line/binary>>, Count) -> delimiter(Line, Count);
delimiter(_, _) -> false.

body([], _) ->
    {[], []};
body([<<>> | Rest], _) ->
    {[], Rest};
body([<<$|, _/binary>> = Line | Rest], N) ->
    case fields(Line) of
        {true, N} ->
            case body(Rest, N) of
                {Rows, After} -> {[Line | Rows], After};
                false -> false
            end;
        _ ->
            false
    end;
body(Rest, _) ->
    {[], Rest}.

%% --- Lists ---

% A list starting at Lines: its items, each parsed as blocks, and the lines after it. Spaces is the indentation of
% the first item; Block what came before.
list(Kind, Lines, Spaces, Block, Opts) ->
    {Items, After} = items(Kind, Lines, Spaces, Block, Opts),
    {Block ++ [{Kind, [], lists:reverse(Items)}], After}.

items(_Kind, [], _Spaces, Acc, _Opts) ->
    {lists:reverse(Acc), []};
items(Kind, Lines, Spaces, Acc, Opts) ->
    {Content, Rest, Done} = item(Kind, Lines, Spaces, Acc),
    Items =
        case Content =:= [] orelse Content =:= Acc of
            true -> [];
            false -> [{li, [], lines(lists:reverse(Content), [], Opts)} | Acc]
        end,
    case Done of
        true ->
            {Items, Rest};
        false ->
            {Later, After} = items(Kind, Rest, Spaces, [], Opts),
            {Later ++ Items, After}
    end.

% The lines of the current item (reversed), the lines after it and whether the list ends there.
item(_Kind, [], _Spaces, Acc) ->
    {Acc, [], true};
item(Kind, [Line | Rest], Spaces, Acc) ->
    {Stripped, Indent} = strip_spaces(Line, 0, infinity),
    case next(Kind, Stripped, Rest, Spaces, Indent) of
        next ->
            {Kept, _} = strip_spaces(Line, 0, min(Indent, 2)),
            item(Kind, Rest, Spaces, [Kept | Acc]);
        done ->
            {Acc, [Line | Rest], true};
        list ->
            {Acc, [item_text(Stripped) | Rest], false}
    end.

% Whether a line starts the next item, continues the current one or ends the list (another list kind as indented).
next(Kind, Line, Rest, Spaces, Indent) ->
    State = line_kind(Line, Rest, Spaces, Indent),
    Other = list_kind(Line),
    case Kind =:= Other orelse Other =:= p of
        false when Indent =< Spaces -> done;
        _ -> State
    end.

line_kind(<<B>>, _, _, _) when ?IS_BULLET(B) -> list;
line_kind(<<B, $\s, _/binary>>, _, Spaces, Indent) when ?IS_BULLET(B), Indent =< Spaces -> list;
line_kind(<<_, $., $\s, _/binary>>, _, Spaces, Indent) when Indent =< Spaces -> list;
line_kind(<<>>, [<<$\s, _/binary>> | _], _, _) -> next;
line_kind(<<>>, _, _, _) -> done;
line_kind(_, _, _, _) -> next.

% An item's text without its marker.
item_text(<<B>>) when ?IS_BULLET(B) -> <<>>;
item_text(<<B, $\s, Text/binary>>) when ?IS_BULLET(B) -> item_text(Text);
item_text(<<_, $., Text/binary>>) -> item_text(Text);
item_text(<<_, $\s, Text/binary>>) -> item_text(Text);
item_text(Line) -> Line.

%% --- Inline formatting ---

% An element with its text formatted: links reduced to their text, code, emphasis and escapes.
inline({Tag, [], Lines}) ->
    Formatted = lists:foldr(
        fun(Bin, Acc) ->
            case links(Bin) of
                <<>> -> Acc;
                Text -> formats(Text, [], []) ++ Acc
            end
        end,
        [],
        Lines
    ),
    {Tag, [], lists:reverse(Formatted)}.

links(Bin) ->
    Options = [{return, binary}, global],
    R1 = re:replace(Bin, "\\[([^\\]]*?)\\]\\((.*?)\\)", "\\1", Options),
    R2 = re:replace(R1, "\\[(.*)\\]\\((.*?)\\)", "\\1", Options),
    R3 = re:replace(R2, "{:[^}]*}", <<>>, Options),
    re:replace(R3, "\\[([^\\]]*?)\\]: (.*?)", "", Options).

% The formatted text (reversed, text merged) of a line; Fs the format open, Buffer what came before.
formats(Text, Fs, Buffer) ->
    case format(Text, Fs, Buffer) of
        {not_closed, Result} -> Result;
        {ok, Result} -> Result;
        {Continuation, Result} -> formats(Continuation, [], Result)
    end.

% One step over the text: code, an escape, a format closing (after a punctuation mark, at a separator) or opening
% (after a separator), a format character inside a word, or a plain character.
format(<<$`, Rest/binary>>, Fs, Buffer) ->
    {Code, After} = code_span(Rest),
    format(After, Fs, merge(Code, Buffer));
format(<<$\\, C, Rest/binary>>, Fs, Buffer) when ?IS_ESCAPED(C) ->
    format(Rest, Fs, merge([<<C>>], Buffer));
format(<<C, F, F, C2, Rest/binary>>, [<<F>>, <<F>>] = Fs, Buffer) when
    ?IS_FORMAT(F), C =/= F, not ?IS_FORMAT(C), ?IS_PUNCT(C), C2 =/= F, not ?IS_FORMAT(C2), ?IS_SEPARATOR(C2)
->
    close(<<C2, Rest/binary>>, Fs, merge([<<C>>], Buffer));
format(<<C, F, C2, Rest/binary>>, [<<F>>] = Fs, Buffer) when
    ?IS_FORMAT(F), C =/= F, not ?IS_FORMAT(C), ?IS_PUNCT(C), C2 =/= F, not ?IS_FORMAT(C2), ?IS_SEPARATOR(C2)
->
    close(<<C2, Rest/binary>>, Fs, merge([<<C>>], Buffer));
format(<<C, F, C2, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F), ?IS_WORD(C, F), ?IS_WORD(C2, F) ->
    format(Rest, Fs, merge([<<C, F, C2>>], Buffer));
format(<<C, F, F, C2, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F), ?IS_WORD(C, F), ?IS_WORD(C2, F) ->
    format(Rest, Fs, merge([<<C, F, C2>>], Buffer));
format(Text, [<<F>>, <<F>>] = Fs, Buffer) when ?IS_FORMAT(F) ->
    double_close(Text, F, Fs, Buffer);
format(Text, [<<F>>] = Fs, Buffer) when ?IS_FORMAT(F) ->
    single_close(Text, F, Fs, Buffer);
format(Text, Fs, Buffer) ->
    open(Text, Fs, Buffer).

% A double format closes at two format characters (with what follows), or after a separator.
double_close(<<F, F>>, F, Fs, Buffer) ->
    close(<<>>, Fs, Buffer);
double_close(<<F, F, Rest/binary>>, F, Fs, Buffer) ->
    close(Rest, Fs, Buffer);
double_close(<<S, F, F>>, F, Fs, Buffer) when ?IS_SEPARATOR(S) ->
    close(<<>>, Fs, merge([<<S>>], Buffer));
double_close(<<S, F, F, Rest/binary>>, F, Fs, Buffer) when ?IS_SEPARATOR(S) ->
    close(Rest, Fs, merge([<<S>>], Buffer));
double_close(Text, _F, Fs, Buffer) ->
    open(Text, Fs, Buffer).

% A single format closes at its character (with what follows), or after a separator.
single_close(<<F>>, F, Fs, Buffer) ->
    close(<<>>, Fs, Buffer);
single_close(<<F, Rest/binary>>, F, Fs, Buffer) ->
    close(Rest, Fs, Buffer);
single_close(<<S, F>>, F, Fs, Buffer) when ?IS_SEPARATOR(S) ->
    close(<<>>, Fs, merge([<<S>>], Buffer));
single_close(<<S, F, Rest/binary>>, F, Fs, Buffer) when ?IS_SEPARATOR(S) ->
    close(Rest, Fs, merge([<<S>>], Buffer));
single_close(Text, _F, Fs, Buffer) ->
    open(Text, Fs, Buffer).

% Format characters opening a format, after a separator, a line break or anywhere; else a plain character.
open(<<F, F>>, [<<G>>, <<G>>] = Fs, Buffer) when ?IS_FORMAT(F), F =/= G ->
    format(<<>>, Fs, merge([<<F>>, <<F>>], Buffer));
open(<<S, F, F, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F), ?IS_SEPARATOR(S) ->
    open_format(Rest, [<<F>>, <<F>>], Fs, merge([<<S>>], Buffer));
open(<<$\r, $\n, F, F, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F) ->
    open_format(Rest, [<<F>>, <<F>>], Fs, merge([<<"\r\n">>], Buffer));
open(<<F, F, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F) ->
    open_format(Rest, [<<F>>, <<F>>], Fs, Buffer);
open(<<F>>, [<<G>>] = Fs, Buffer) when ?IS_FORMAT(F), F =/= G ->
    format(<<>>, Fs, merge([<<F>>], Buffer));
open(<<S, F, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F), ?IS_SEPARATOR(S) ->
    open_format(Rest, [<<F>>], Fs, merge([<<S>>], Buffer));
open(<<$\r, $\n, F, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F) ->
    open_format(Rest, [<<F>>], Fs, merge([<<"\r\n">>], Buffer));
open(<<F, Rest/binary>>, Fs, Buffer) when ?IS_FORMAT(F) ->
    open_format(Rest, [<<F>>], Fs, Buffer);
open(<<C, Rest/binary>>, Fs, Buffer) ->
    format(Rest, Fs, merge([<<C>>], Buffer));
open(<<>>, [], Buffer) ->
    {ok, Buffer};
open(<<>>, _Fs, Buffer) ->
    {not_closed, Buffer}.

% Formatted text up to the format's closing; unclosed, its characters are plain text.
open_format(Rest, New, Previous, Buffer) ->
    case format(Rest, New, []) of
        {not_closed, _} -> format(Rest, Previous, merge(New, Buffer));
        {ok, Inner} -> {ok, merge(Inner, Buffer)};
        {Continuation, Inner} -> format(Continuation, Previous, merge(Inner, Buffer))
    end.

close(Continuation, Fs, Buffer) ->
    Content = lists:reverse(Buffer),
    Element =
        case Fs of
            [_, _] -> {em, [], Content};
            [<<"`">>] -> {code, [], Content};
            [_] -> {i, [], Content}
        end,
    {Continuation, [Element]}.

% Code up to the next backquote (a t:, m:, e: or c: reference prefix dropped); unclosed, its text is plain.
code_span(<<P, $:, Rest/binary>>) when ?IS_CODE_PREFIX(P) -> code_span(Rest, []);
code_span(Text) -> code_span(Text, []).

code_span(<<$\\, $`, Rest/binary>>, Buffer) -> code_span(Rest, merge([<<$`>>], Buffer));
code_span(<<$`, Rest/binary>>, Buffer) -> {[{code, [], Buffer}], Rest};
code_span(<<C, Rest/binary>>, Buffer) -> code_span(Rest, merge([<<C>>], Buffer));
code_span(<<>>, Buffer) -> {Buffer, <<>>}.

% New elements (reversed) put before the buffer (reversed), adjacent texts joined in reading order.
merge(New, Buffer) ->
    lists:foldr(fun compact/2, [], New ++ Buffer).

compact(X, []) -> [X];
compact(X, Acc) when is_tuple(X) -> [X | Acc];
compact(X, [Last | _] = Acc) when is_tuple(Last) -> [X | Acc];
compact(X, [Last | Before]) -> [<<Last/binary, X/binary>> | Before].
