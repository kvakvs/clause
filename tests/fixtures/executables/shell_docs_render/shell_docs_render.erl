-module(shell_docs_render).
-export([main/1]).

% shell_docs over documentation chunks the program builds (plan 21 step LIB74C): markdown and native chunks
% rendered as modules, functions, types and callbacks, with and without ANSI styles, normalized and validated.
% The module the chunks describe does not exist, so no specifications are read from object code.
main(_) ->
    Plain = #{columns => 60, ansi => false, encoding => unicode},
    Styled = #{columns => 72, ansi => true, encoding => unicode},
    Latin1 = #{columns => 50, ansi => false, encoding => latin1},
    Md = markdown_docs(),
    Native = native_docs(),
    show("module", shell_docs:render(docmod, Md, Plain)),
    show("module styled", shell_docs:render(docmod, Md, Styled)),
    show("function", shell_docs:render(docmod, fetch, Md, Plain)),
    show("function arity", shell_docs:render(docmod, fetch, 2, Md, Latin1)),
    show("equiv group", shell_docs:render(docmod, store, Md, Plain)),
    show("missing", shell_docs:render(docmod, nothing, Md, Plain)),
    show("hidden", shell_docs:render(docmod, secret, 0, Md, Plain)),
    show("types", shell_docs:render_type(docmod, Md, Plain)),
    show("type", shell_docs:render_type(docmod, key, Md, Plain)),
    show("type missing", shell_docs:render_type(docmod, other, 0, Md, Plain)),
    show("callbacks", shell_docs:render_callback(docmod, Md, Plain)),
    show("callback", shell_docs:render_callback(docmod, handle, 1, Md, Styled)),
    show("native module", shell_docs:render(nativemod, Native, Plain)),
    show("native function", shell_docs:render(nativemod, run, 1, Native, Styled)),
    show("native callbacks", shell_docs:render_callback(nativemod, Native, Plain)),
    show("text format", shell_docs:render(textmod, text_docs(), Plain)),
    io:format("tags: ~p~n", [shell_docs:supported_tags()]),
    io:format("normalize: ~p~n", [
        shell_docs:normalize(element(2, lists:keyfind(<<"en">>, 1, maps:to_list(element(5, Native)))))
    ]),
    io:format("validate: ~p~n", [shell_docs:validate(Native)]),
    io:format("invalid: ~p~n", [catch_throw(fun() -> shell_docs:validate(invalid_docs()) end)]),
    io:format("shell_docs_render: ok~n").

catch_throw(F) ->
    try
        F()
    catch
        throw:Reason -> {thrown, Reason}
    end.

show(Title, Rendered) ->
    io:format("== ~s~n~ts~n", [Title, io_lib:format("~tp", [Rendered])]),
    case Rendered of
        {error, _} -> ok;
        _ -> io:format("~ts", [Rendered])
    end.

markdown_docs() ->
    ModuleDoc =
        <<
            "Utilities for *examples* and **tests** of the `shell_docs` renderer.\n"
            "\n"
            "A second paragraph that is long enough to wrap over more than one line when the shell has only sixty columns "
            "to show it in, with a [link](https://example.org) and `m:lists` reference.\n"
            "\n"
            "## Usage\n"
            "\n"
            "- first item\n"
            "- second item with `code`\n"
            "  - nested item\n"
            "- third item\n"
            "\n"
            "1. one\n"
            "2. two\n"
            "\n"
            "```erlang\n"
            "1> docmod:fetch(a, b).\n"
            "ok\n"
            "```\n"
            "\n"
            "> A quoted note\n"
            "> over two lines.\n"
            "\n"
            "Setext heading\n"
            "--------------\n"
            "\n"
            "| Key | Value |\n"
            "|-----|-------|\n"
            "| a   | 1     |\n"
            "\n"
            "    indented code\n"
            "    more code\n"
            "\n"
            "<!-- a comment -->\n"
            "Escaped \\*stars\\* and snake_case_words stay.\n"
            "\n"
            "### Small heading\n"
            "\n"
            "Text after it.\n"
        >>,
    Entries = [
        {
            {function, fetch, 2},
            [],
            [<<"fetch(Key, Map)">>],
            #{<<"en">> => <<"Fetch `Key` from `Map`.\n\nRaises `badkey` when it is missing.">>},
            #{since => <<"OTP 27.0">>}
        },
        {
            {function, fetch, 3},
            [],
            [<<"fetch(Key, Map, Default)">>],
            #{<<"en">> => <<"Fetch `Key`, else `Default`.">>},
            #{deprecated => <<"use fetch/2 instead">>}
        },
        {{function, store, 2}, [], [<<"store(Key, Value)">>], #{<<"en">> => <<"Store a value.">>}, #{}},
        {{function, store, 3}, [], [<<"store(Key, Value, Map)">>], #{}, #{equiv => {function, store, 2}}},
        {{function, secret, 0}, [], [<<"secret()">>], hidden, #{}},
        {{type, key, 0}, [], [<<"key()">>], #{<<"en">> => <<"A key: an atom or a _binary_.">>}, #{}},
        {{callback, handle, 1}, [], [<<"handle(Event)">>], #{<<"en">> => <<"Handle an **event**; return `ok`.">>}, #{
            since => <<"OTP 28.0">>
        }}
    ],
    {docs_v1, [], erlang, <<"text/markdown">>, #{<<"en">> => ModuleDoc}, #{}, Entries}.

native_docs() ->
    ModuleDoc = [
        {p, [], [
            <<"A module documented in   the native\n  format, with ">>,
            {em, [], [<<"emphasis">>]},
            <<" and ">>,
            {code, [], [<<"code">>]},
            <<".">>
        ]},
        {h2, [], [<<"Section">>]},
        {ul, [], [{li, [], [{p, [], [<<"item one">>]}]}, {li, [], [<<"item two">>]}]},
        {dl, [], [{dt, [], [<<"Term">>]}, {dd, [], [<<"Its description.">>]}]},
        {'div', [{class, <<"note">>}], [{p, [], [<<"Mind this.">>]}]},
        {pre, [], [{code, [], [<<"\nline one\nline two  \n">>]}]},
        {blockquote, [], [<<"Quoted text.">>]}
    ],
    Entries = [
        {
            {function, run, 1},
            [],
            [<<"run(Options)">>],
            #{<<"en">> => [{p, [], [<<"Run with ">>, {b, [], [<<"options">>]}, <<".">>]}]},
            #{}
        },
        {{callback, init, 1}, [], [<<"init(Args)">>], #{<<"en">> => [{p, [], [<<"Initialise.">>]}]}, #{}}
    ],
    {docs_v1, [], erlang, <<"application/erlang+html">>, #{<<"en">> => ModuleDoc}, #{}, Entries}.

text_docs() ->
    {docs_v1, [], erlang, <<"text/plain">>, #{<<"en">> => <<"Plain text\n  kept as it is.\n">>}, #{}, []}.

invalid_docs() ->
    {docs_v1, [], erlang, <<"application/erlang+html">>, #{<<"en">> => [{ul, [], [{p, [], [<<"x">>]}]}]}, #{}, []}.
