%% Clause's epp (plan 21 step LIB34, docs/library.md): the Erlang preprocessor, written from OTP's documentation and
%% observed behavior. An open source is a server process answering scan_erl_form requests one form at a time:
%% -define/-undef, macro expansion with OTP's errors (arguments, overloading, circular and predefined macros,
%% ??Arg, ?FUNCTION_NAME, ?LINE), -include and -include_lib through the include path, -ifdef/-ifndef/-if/-elif/
%% -else/-endif, -file, -feature, -error/-warning and -doc/-moduledoc {file, Name}. Files are read whole and decoded
%% by their coding comment (UTF-8 by default); each step of the server yields the replies of one or more requests,
%% queued until asked for.
-module(epp).
-export([
    close/1,
    default_encoding/0,
    encoding_to_string/1,
    format_error/1,
    interpret_file_attribute/1,
    macro_defs/1,
    normalize_typed_record_fields/1,
    open/1,
    open/2,
    open/3,
    parse_erl_form/1,
    parse_file/1,
    parse_file/2,
    parse_file/3,
    read_encoding/1,
    read_encoding/2,
    read_encoding_from_binary/1,
    read_encoding_from_binary/2,
    restore_typed_record_fields/1,
    scan_erl_form/1,
    scan_file/1,
    scan_file/2,
    set_encoding/1,
    set_encoding/2
]).

-include("epp.types.hrl").

-define(DEFAULT_ENCODING, utf8).
%% Nesting of included files.
-define(MAX_INCLUDE_DEPTH, 8).

%% The state of one source: the file (closed on leaving it unless pre-opened) and its decoded source, the location
%% and the line delta -file set, its name as entered and as -file renamed it, the open conditionals, the states of
%% the including files, the include path, the macros and their uses (for circularity), the default encoding, whether
%% -feature may still come (prefix), the scanner options, the enabled features, whether else is reserved, the tokens
%% of the current form for ?FUNCTION_NAME (or its {Name, Arity}), deterministic -file names, and how the next request
%% resumes: scanning, skipping conditionals Sis ({skip, Sis}) or leaving the file.
-record(epp, {
    file,
    pre_opened = false,
    src = {[], valid},
    location = 1,
    delta = 0,
    name = "",
    name2 = "",
    istk = [],
    sstk = [],
    path = [],
    macs = #{},
    uses = #{},
    default_encoding = ?DEFAULT_ENCODING,
    in_prefix = true,
    scan_opts = [],
    features = [],
    else_reserved = false,
    fname = [],
    deterministic = false,
    resume = scan
}).

%% API

open(Name, Path) -> open(Name, Path, []).

open(Name, Path, Macros) -> open([{name, Name}, {includes, Path}, {macros, Macros}]).

open(Options) ->
    case proplists:get_value(name, Options) of
        undefined ->
            erlang:error(badarg);
        Name ->
            Self = self(),
            Epp = spawn(fun() -> server(Self, Name, Options) end),
            Extra = proplists:get_bool(extra, Options),
            case request(Epp) of
                {ok, Pid, Encoding} when Extra -> {ok, Pid, [{encoding, Encoding}]};
                {ok, Pid, _} -> {ok, Pid};
                Other -> Other
            end
    end.

close(Epp) ->
    Ref = erlang:monitor(process, Epp),
    Result = request(Epp, close),
    receive
        {'DOWN', Ref, _, _, _} -> ok
    end,
    Result.

scan_erl_form(Epp) -> request(Epp, scan_erl_form).

parse_erl_form(Epp) ->
    case request(Epp, scan_erl_form) of
        {ok, Tokens} -> erl_parse:parse_form(Tokens);
        Other -> Other
    end.

macro_defs(Epp) -> request(Epp, macro_defs).

scan_file(Name, Options) ->
    case open([{name, Name}, extra | Options]) of
        {ok, Epp, Extra} ->
            Forms = scan_file(Epp),
            close(Epp),
            {ok, Forms, Extra};
        {error, E} ->
            {error, E}
    end.

scan_file(Epp) ->
    case scan_erl_form(Epp) of
        {ok, Tokens} -> [Tokens | scan_file(Epp)];
        {error, E} -> [{error, E} | scan_file(Epp)];
        {eof, Location} -> [{eof, Location}]
    end.

parse_file(Name, Path, Macros) -> parse_file(Name, [{includes, Path}, {macros, Macros}]).

parse_file(Name, Options) ->
    case open([{name, Name} | Options]) of
        {ok, Epp} ->
            Forms = parse_file(Epp),
            close(Epp),
            {ok, Forms};
        {ok, Epp, Extra} ->
            Forms = parse_file(Epp),
            Epp ! {get_features, self()},
            Features =
                receive
                    {features, F} -> F
                end,
            close(Epp),
            {ok, Forms, [{features, Features} | Extra]};
        {error, E} ->
            {error, E}
    end.

parse_file(Epp) ->
    case parse_erl_form(Epp) of
        {ok, Form} -> [Form | parse_file(Epp)];
        {error, E} -> [{error, E} | parse_file(Epp)];
        {warning, W} -> [{warning, W} | parse_file(Epp)];
        {eof, Location} -> [{eof, Location}]
    end.

default_encoding() -> ?DEFAULT_ENCODING.

encoding_to_string(latin1) -> "coding: latin-1";
encoding_to_string(utf8) -> "coding: utf-8".

read_encoding(Name) -> read_encoding(Name, []).

read_encoding(Name, Options) ->
    InComment = proplists:get_value(in_comment_only, Options, true),
    case file:open(Name, [read, binary]) of
        {ok, Fd} ->
            try
                encoding_of(Fd, InComment)
            after
                ok = file:close(Fd)
            end;
        _ ->
            none
    end.

read_encoding_from_binary(Bin) -> read_encoding_from_binary(Bin, []).

read_encoding_from_binary(Bin, Options) -> clause_epp:read_encoding_from_binary(Bin, Options).

set_encoding(File) -> set_encoding(File, ?DEFAULT_ENCODING).

%% The encoding a device's coding comment names, or none; the device is set to it (or Default) where its I/O server
%% takes the option.
set_encoding(File, Default) ->
    Encoding = encoding_of(File, true),
    Set =
        case Encoding of
            none -> Default;
            _ -> Encoding
        end,
    try io:setopts(File, [{encoding, Set}]) of
        _ -> ok
    catch
        error:{not_implemented, _, _} -> ok
    end,
    Encoding.

%% The coding comment within a device's first 512 bytes, its position kept.
encoding_of(File, InComment) ->
    {ok, Position} = file:position(File, cur),
    Head =
        case file:read(File, 512) of
            {ok, Data} -> iolist_to_binary(Data);
            _ -> <<>>
        end,
    {ok, Position} = file:position(File, Position),
    clause_epp:read_encoding_from_binary(Head, [{in_comment_only, InComment}]).

format_error(Error) ->
    case error_text(Error) of
        {Format, Args} when is_list(Args) -> io_lib:format(Format, Args);
        Bin when is_binary(Bin) -> unicode:characters_to_list(Bin);
        List when is_list(List) -> List
    end.

error_text(cannot_parse) ->
    <<"cannot parse file, giving up">>;
error_text({bad, W}) ->
    {<<"badly formed '~s'">>, [W]};
error_text({duplicated_argument, Arg}) ->
    {<<"argument '~ts' already used">>, [Arg]};
error_text(missing_parenthesis) ->
    {<<"badly formed define: missing closing right parenthesis">>, []};
error_text(missing_comma) ->
    <<"badly formed define: missing comma">>;
error_text(premature_end) ->
    <<"premature end">>;
error_text({call, What}) ->
    {<<"illegal macro call '~ts'">>, [What]};
error_text({undefined, M, none}) ->
    {<<"undefined macro '~ts'">>, [M]};
error_text({undefined, M, A}) ->
    {<<"undefined macro '~ts/~p'">>, [M, A]};
error_text({depth, What}) ->
    {<<"~s too deep">>, [What]};
error_text({mismatch, M}) ->
    {<<"argument mismatch for macro '~ts'">>, [M]};
error_text({arg_error, M}) ->
    {<<"badly formed argument for macro '~ts'">>, [M]};
error_text({redefine, M}) ->
    {<<"redefining macro '~ts'">>, [M]};
error_text({redefine_predef, M}) ->
    {<<"redefining predefined macro '~s'">>, [M]};
error_text({circular, M, none}) ->
    {<<"circular macro '~ts'">>, [M]};
error_text({circular, M, A}) ->
    {<<"circular macro '~ts/~p'">>, [M, A]};
error_text({include, W, F}) ->
    {<<"can't find include ~s \"~ts\"">>, [W, F]};
error_text({Tag, invalid, Alternative}) when Tag =:= moduledoc; Tag =:= doc ->
    {<<"invalid ~s tag, only ~s allowed">>, [Tag, Alternative]};
error_text({Tag, W, Filename}) when Tag =:= moduledoc; Tag =:= doc ->
    {<<"can't find ~s ~s \"~ts\"">>, [Tag, W, Filename]};
error_text({illegal, How, What}) ->
    {<<"~s '-~s'">>, [How, What]};
error_text({illegal_function, Macro}) ->
    {<<"?~s can only be used within a function">>, [Macro]};
error_text({illegal_function_usage, Macro}) ->
    {<<"?~s must not begin a form">>, [Macro]};
error_text(elif_after_else) ->
    "'elif' following 'else'";
error_text({'NYI', What}) ->
    {<<"not yet implemented '~s'">>, [What]};
error_text({error, Term}) ->
    {<<"-error(~tp).">>, [Term]};
error_text({warning, Term}) ->
    {<<"-warning(~tp).">>, [Term]};
error_text(ftr_after_prefix) ->
    <<"feature directive not allowed after exports or record definitions">>;
error_text(E) ->
    file:format_error(E).

%% Typed record fields as the compiler keeps them: {typed, Fields} without the types, or not_typed.
normalize_typed_record_fields([]) -> {typed, []};
normalize_typed_record_fields(Fields) -> untyped_fields(Fields, [], false).

untyped_fields([], Fields, true) ->
    {typed, lists:reverse(Fields)};
untyped_fields([], _Fields, false) ->
    not_typed;
untyped_fields([{typed_record_field, Field, _} | Rest], Fields, _Typed) ->
    untyped_fields(Rest, [Field | Fields], true);
untyped_fields([Field | Rest], Fields, Typed) ->
    untyped_fields(Rest, [Field | Fields], Typed).

%% A record attribute and the type attribute holding its typed fields back as one typed record attribute.
restore_typed_record_fields([]) ->
    [];
restore_typed_record_fields([
    {attribute, A, record, {Record, _}},
    {attribute, A, type, {{record, Record}, Fields, []}}
    | Forms
]) ->
    [{attribute, A, record, {Record, Fields}} | restore_typed_record_fields(Forms)];
restore_typed_record_fields([{attribute, A, type, {{record, Record}, Fields, []}} | Forms]) ->
    [{attribute, A, record, {Record, Fields}} | restore_typed_record_fields(Forms)];
restore_typed_record_fields([Form | Forms]) ->
    [Form | restore_typed_record_fields(Forms)].

%% interpret_file_attribute(Forms): lines of the top-most file, the -file attributes of the source dropped (those epp
%% made for includes kept).
interpret_file_attribute(Forms) -> file_lines(Forms, 0, []).

file_lines([{attribute, Anno, file, {File, Line}} = Form | Forms], Delta, Files) ->
    case erl_anno:generated(Anno) of
        true ->
            file_lines(Forms, erl_anno:line(Anno) + Delta - Line, Files);
        false ->
            case Files of
                [_, File | Outer] -> [Form | file_lines(Forms, 0, [File | Outer])];
                _ -> [Form | file_lines(Forms, 0, [File | Files])]
            end
    end;
file_lines([Form | Forms], Delta, Files) ->
    Shifted = erl_parse:map_anno(
        fun(Anno) -> erl_anno:set_line(erl_anno:line(Anno) + Delta, Anno) end, Form
    ),
    [Shifted | file_lines(Forms, Delta, Files)];
file_lines([], _Delta, _Files) ->
    [].

%% The protocol

request(Epp) -> reply_of(Epp, erlang:monitor(process, Epp)).

request(Epp, Request) ->
    Epp ! {epp_request, self(), Request},
    reply_of(Epp, erlang:monitor(process, Epp)).

reply_of(Epp, Ref) ->
    receive
        {epp_reply, Epp, Reply} ->
            erlang:demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, _, _, E} ->
            receive
                {epp_reply, Epp, Reply} -> Reply
            after 0 -> exit(E)
            end
    end.

reply(To, Reply) ->
    To ! {epp_reply, self(), Reply},
    ok.

%% The server

server(Parent, Name, Options) ->
    process_flag(trap_exit, true),
    case proplists:get_value(fd, Options) of
        undefined ->
            case file:open(Name, [read, binary]) of
                {ok, Fd} -> start(Parent, Name, Options, #epp{file = Fd});
                {error, E} -> reply(Parent, {error, E})
            end;
        Fd ->
            start(Parent, Name, Options, #epp{file = Fd, pre_opened = true})
    end.

start(Parent, Name, Options, St0) ->
    SourceName = proplists:get_value(source_name, Options, Name),
    Features = proplists:get_value(features, Options, []),
    Internal = proplists:get_value(compiler_internal, Options, []),
    case predefine(proplists:get_value(macros, Options, []), predefined(SourceName, Features)) of
        {ok, Macros} ->
            Default = proplists:get_value(default_encoding, Options, ?DEFAULT_ENCODING),
            {Encoding, Src} = read_source(St0#epp.file, Default),
            reply(Parent, {ok, self(), Encoding}),
            {ok, {_, Reserved0}} = erl_features:init_parse_state(
                [], fun erl_scan:f_reserved_word/1
            ),
            Reserved = proplists:get_value(reserved_word_fun, Options, Reserved0),
            Location = proplists:get_value(location, Options, 1),
            Deterministic = proplists:get_value(deterministic, Options, false),
            Checks = [
                {compiler_internal, [ssa_checks]}
             || proplists:get_bool(ssa_checks, Internal)
            ],
            St = St0#epp{
                src = Src,
                name = SourceName,
                name2 = SourceName,
                path = [filename:dirname(Name) | proplists:get_value(includes, Options, [])],
                location = Location,
                macs = Macros,
                default_encoding = Default,
                scan_opts =
                    [{text_fun, keep_feature_keywords()}, {reserved_word_fun, Reserved}] ++ Checks,
                features = Features,
                else_reserved = Reserved('else'),
                deterministic = Deterministic
            },
            First = file_form(
                file_name(SourceName), erl_anno:new(Location), Location, code, Deterministic
            ),
            serve(St, [First]);
        {error, E} ->
            reply(Parent, {error, E})
    end.

%% Requests answered in order; a scan request takes the next queued reply, stepping the source when none is left.
serve(St, Queue) ->
    receive
        {epp_request, From, scan_erl_form} ->
            {Reply, St1, Queue1} = next_reply(St, Queue),
            reply(From, Reply),
            serve(St1, Queue1);
        {get_features, From} ->
            From ! {features, St#epp.features},
            serve(St, Queue);
        {epp_request, From, macro_defs} ->
            reply(From, [{{atom, K}, V} || K := V <- St#epp.macs]),
            serve(St, Queue);
        {epp_request, From, close} ->
            close_file(St),
            reply(From, ok),
            exit(normal);
        {'EXIT', _, Reason} ->
            exit(Reason);
        Other ->
            io:fwrite("Epp: unknown '~w'\n", [Other]),
            serve(St, Queue)
    end.

next_reply(St, [Reply | Queue]) ->
    {Reply, St, Queue};
next_reply(St0, []) ->
    {[Reply | Queue], St} = step(St0),
    {Reply, St, Queue}.

%% One step: the replies it gives and the state after them.
step(#epp{resume = scan} = St) -> scan(St);
step(#epp{resume = {skip, Sis}} = St) -> skip(St#epp{resume = scan}, Sis);
step(#epp{resume = leave} = St) -> leave(St#epp{resume = scan}).

close_file(#epp{pre_opened = true}) -> ok;
close_file(#epp{file = File}) -> ok = file:close(File).

read_source(File, Default) ->
    case clause_epp:read_all(File) of
        {ok, Bytes} -> clause_epp:source(Bytes, Default);
        {error, _} -> {none, terminated}
    end.

%% Keeps quoted atoms that are keywords of configurable features quoted in the tokens' text.
keep_feature_keywords() ->
    Keywords = lists:flatmap(fun erl_features:keywords/1, erl_features:configurable()),
    Quoted = [atom_to_list(K) ++ "'" || K <- Keywords],
    fun
        (atom, [$' | S]) -> lists:member(S, Quoted);
        (_, _) -> false
    end.

%% The predefined macros; ?MODULE and the like stay undefined until the module attribute.
predefined(File, Enabled) ->
    Machine = list_to_atom(erlang:system_info(machine)),
    Anno = erl_anno:new(1),
    Release = list_to_integer(erlang:system_info(otp_release)),
    Available = [F || F <- erl_features:all(), maps:get(status, erl_features:info(F)) =/= rejected],
    Permanent = [
        F
     || F <- erl_features:all(), maps:get(status, erl_features:info(F)) =:= permanent
    ],
    #{
        'FILE' => {none, [{string, Anno, File}]},
        'FUNCTION_NAME' => undefined,
        'FUNCTION_ARITY' => undefined,
        'LINE' => {none, [{integer, Anno, 1}]},
        'MODULE' => undefined,
        'MODULE_STRING' => undefined,
        'BASE_MODULE' => undefined,
        'BASE_MODULE_STRING' => undefined,
        'MACHINE' => {none, [{atom, Anno, Machine}]},
        Machine => {none, [{atom, Anno, true}]},
        'OTP_RELEASE' => {none, [{integer, Anno, Release}]},
        'FEATURE_AVAILABLE' => [feature_macro(Available)],
        'FEATURE_ENABLED' => [feature_macro(Enabled ++ Permanent)]
    }.

%% ?FEATURE_ENABLED(X) and ?FEATURE_AVAILABLE(X): ((X) == f1 orelse (X) == f2 ...), or false.
feature_macro([]) ->
    {1, {['X'], [{atom, erl_anno:new(1), false}]}};
feature_macro([First | Rest]) ->
    Anno = erl_anno:new(1),
    Test = fun(F) ->
        [{'(', Anno}, {var, Anno, 'X'}, {')', Anno}, {'==', Anno}, {atom, Anno, F}]
    end,
    Body = lists:foldl(
        fun(F, Acc) -> Test(F) ++ [{'orelse', Anno} | Acc] end, Test(First) ++ [{')', Anno}], Rest
    ),
    {1, {['X'], [{'(', Anno} | Body]}}.

%% The macros option: M, {M, Value} and {M, Value, redefine}.
predefine([{M, Value, redefine} | Ms], Macros) when is_atom(M) ->
    predefine(Ms, Macros#{M => {none, erl_parse:tokens(erl_parse:abstract(Value))}});
predefine([{M, Value} | Ms], Macros) when is_atom(M) ->
    case Macros of
        #{M := Defs} when is_list(Defs) -> {error, {redefine, M}};
        #{M := _} ->
            {error, {redefine_predef, M}};
        #{} ->
            predefine(Ms, Macros#{
                M => [{none, {none, erl_parse:tokens(erl_parse:abstract(Value))}}]
            })
    end;
predefine([M | Ms], Macros) when is_atom(M) ->
    predefine([{M, true} | Ms], Macros);
predefine([Bad | _], _Macros) ->
    {error, {bad, Bad}};
predefine([], Macros) ->
    {ok, Macros}.

%% The -file(Name, Line). form epp gives on entering and leaving files.
file_form(Name, LineAnno, At, Where, Deterministic) ->
    Anno0 = erl_anno:new(At),
    Anno =
        case Where of
            code -> Anno0;
            generated -> erl_anno:set_generated(true, Anno0)
        end,
    {ok, [
        {'-', Anno},
        {atom, Anno, file},
        {'(', Anno},
        {string, Anno, source_name(Deterministic, Name)},
        {',', Anno},
        {integer, Anno, erl_anno:line(LineAnno)},
        {')', LineAnno},
        {dot, Anno}
    ]}.

file_name([C | T]) when is_integer(C), C > 0 -> [C | file_name(T)];
file_name([H | T]) -> file_name(H) ++ file_name(T);
file_name([]) -> [];
file_name(N) when is_atom(N) -> atom_to_list(N).

source_name(true, Name) -> filename:basename(Name);
source_name(false, Name) -> Name.

%% Scanning

%% The next form of the current file, as io:scan_erl_form/4 reads it.
read_form(#epp{src = Src, location = Location, scan_opts = Options} = St) ->
    {Result, Src1} = clause_epp:scan(Src, Location, Options),
    {Result, St#epp{src = Src1}}.

scan(St0) ->
    case read_form(St0) of
        {{ok, Tokens, End}, St} ->
            form(Tokens, St#epp{location = End});
        {{error, E, End}, St} ->
            {[{error, E}], St#epp{location = End}};
        {{eof, End}, St} ->
            leave(St#epp{location = End});
        {{error, _}, St} ->
            {[{error, {St0#epp.location, epp, cannot_parse}}], St0#epp{
                src = St#epp.src, resume = leave
            }}
    end.

%% A form: a directive acted on, or the tokens with their macros expanded.
form([{'-', _}, {atom, _, feature} = Feature | Ts], St) ->
    feature(Ts, Feature, St);
form([{'-', _}, {atom, _, define} = Define | Ts], St) ->
    define(Ts, Define, St);
form([{'-', _}, {atom, _, undef} = Undef | Ts], St) ->
    undef(Ts, Undef, leave_prefix(St));
form([{'-', _}, {atom, _, Tag} = Token | Ts], St) when Tag =:= error; Tag =:= warning ->
    error_warning(Ts, Token, leave_prefix(St));
form([{'-', _}, {atom, _, include} = Include | Ts], St) ->
    include(coalesce_strings(Ts), Include, St);
form([{'-', _}, {atom, _, D} = Doc | [{'(', _}, {'{', _} | _] = Ts], St) when
    D =:= doc; D =:= moduledoc
->
    doc_file(coalesce_strings(Ts), Doc, St);
form([{'-', _}, {atom, _, D} = Doc | [{'{', _} | _] = Ts], St) when D =:= doc; D =:= moduledoc ->
    doc_file(coalesce_strings(Ts), Doc, St);
form([{'-', _}, {atom, _, include_lib} = Include | Ts], St) ->
    include_lib(coalesce_strings(Ts), Include, St);
form([{'-', _}, {atom, _, ifdef} = IfDef | Ts], St) ->
    ifdef(Ts, IfDef, St, true);
form([{'-', _}, {atom, _, ifndef} = IfnDef | Ts], St) ->
    ifdef(Ts, IfnDef, St, false);
form([{'-', _}, {atom, _, 'else'} = Else | Ts], St) ->
    else_directive(Ts, Else, St);
form([{'-', _}, {'else', _} = Else | Ts], #epp{else_reserved = true} = St) ->
    else_directive(Ts, Else, St);
form([{'-', _}, {'if', _} = If | Ts], St) ->
    if_directive(Ts, If, St);
form([{'-', _}, {atom, _, elif} = Elif | _], St) ->
    elif(Elif, St);
form([{'-', _}, {atom, _, endif} = Endif | Ts], St) ->
    endif(Ts, Endif, St);
form([{'-', _}, {atom, _, file} = File | Ts0], St) ->
    try expand(Ts0, St) of
        Ts -> file_directive(coalesce_strings(Ts), File, St)
    catch
        throw:{error, L, What} -> {[{error, {L, epp, What}}], St}
    end;
form(Ts0, St) ->
    try expand(Ts0, St#epp{fname = Ts0}) of
        Ts ->
            InPrefix =
                St#epp.in_prefix andalso
                    case Ts of
                        [] -> true;
                        [{'-', _}, Token | _] -> in_prefix(Token);
                        _ -> false
                    end,
            {[{ok, Ts}], St#epp{in_prefix = InPrefix, macs = module_macros(Ts, St#epp.macs)}}
    catch
        throw:{error, L, What} -> {[{error, {L, epp, What}}], St}
    end.

%% -feature(Feature, enable | disable): only before the module's first other attributes.
feature(
    [{'(', _}, {atom, _, F}, {',', _}, {atom, _, Ind}, {')', _}, {dot, _}],
    Feature,
    #epp{in_prefix = true} = St
) when
    Ind =:= enable; Ind =:= disable
->
    case update_features(St, Ind, F) of
        {ok, St1} -> scan(St1);
        {error, {Module, Reason}} -> {[{error, {loc(Feature), Module, Reason}}], St}
    end;
feature(
    [{'(', _}, {atom, _, _}, {',', _}, {atom, _, _}, {')', _}, {dot, _} | _],
    Feature,
    #epp{in_prefix = false} = St
) ->
    {[{error, {loc(Feature), epp, ftr_after_prefix}}], St};
feature(Ts, {atom, _, Tag} = Token, St) ->
    {[{error, {loc(no_match(Ts, Token)), epp, {bad, Tag}}}], St}.

update_features(St, Ind, F) ->
    Reserved0 =
        case proplists:get_value(reserved_word_fun, St#epp.scan_opts) of
            undefined -> fun erl_scan:f_reserved_word/1;
            Fun -> Fun
        end,
    case erl_features:update_parse_state(Ind, F, St#epp.features, Reserved0) of
        {error, Reason} ->
            {error, Reason};
        {ok, {Features, Reserved}} ->
            Options = [
                {reserved_word_fun, Reserved}
                | proplists:delete(reserved_word_fun, St#epp.scan_opts)
            ],
            Macros = (St#epp.macs)#{'FEATURE_ENABLED' => [feature_macro(Features)]},
            {ok, St#epp{
                scan_opts = Options,
                features = Features,
                else_reserved = Reserved('else'),
                macs = Macros
            }}
    end.

%% Directives that leave the prefix where -feature is allowed.
in_prefix({atom, _, Name}) ->
    lists:member(Name, [
        module,
        feature,
        'if',
        'else',
        elif,
        endif,
        ifdef,
        ifndef,
        define,
        undef,
        include,
        include_lib,
        moduledoc,
        doc
    ]);
in_prefix(_) ->
    false.

leave_prefix(St) -> St#epp{in_prefix = false}.

%% -module(M) and -extends(B) define ?MODULE, ?MODULE_STRING, ?BASE_MODULE and ?BASE_MODULE_STRING.
module_macros([{'-', _}, {atom, _, module}, {'(', _} | Ts], Macros) -> module_name(Ts, Macros);
module_macros([{'-', _}, {atom, _, extends}, {'(', _} | Ts], Macros) -> base_module(Ts, Macros);
module_macros(_, Macros) -> Macros.

module_name([{atom, _, _} = A, {',', Anno} | Ts], Macros) ->
    module_name([A, {')', Anno} | Ts], Macros);
module_name([{atom, Anno, A} = Name, {')', _} | _], Macros) ->
    Macros#{
        'MODULE' := {none, [Name]}, 'MODULE_STRING' := {none, [{string, Anno, atom_to_list(A)}]}
    };
module_name(_, Macros) ->
    Macros.

base_module([{atom, Anno, A} = Name, {')', _} | _], Macros) ->
    Macros#{
        'BASE_MODULE' := {none, [Name]},
        'BASE_MODULE_STRING' := {none, [{string, Anno, atom_to_list(A)}]}
    };
base_module(_, Macros) ->
    Macros.

%% -error(Term) and -warning(Term).
error_warning([{'(', _} | _] = Ts0, {atom, _, Tag} = Token, St) ->
    Reply =
        try expand(Ts0, St) of
            Ts ->
                case erl_parse:parse_term(Ts) of
                    {ok, Term} -> {Tag, {loc(Token), epp, {Tag, Term}}};
                    {error, _} -> {error, {loc(Token), epp, {bad, Tag}}}
                end
        catch
            _:_ -> {error, {loc(Token), epp, {bad, Tag}}}
        end,
    {[Reply], St};
error_warning(Ts, {atom, _, Tag} = Token, St) ->
    {[{error, {loc(no_match(Ts, Token)), epp, {bad, Tag}}}], St}.

%% -doc and -moduledoc {file, Name}: the file's text as the attribute's string, inside -file forms naming it.
doc_file(
    [
        {'(', _},
        {'{', _},
        {atom, _, file},
        {',', _},
        {string, _, _} = Name,
        {'}', _},
        {')', _},
        {dot, _} = Dot
    ],
    Doc,
    St
) ->
    doc_content(Name, Dot, Doc, St);
doc_file([{'(', _}, {'{', _}, {atom, _, file} | _] = Ts, Doc, St) ->
    T = find_mismatch(['(', '{', atom, ',', string, '}', ')', dot], Ts, Doc),
    {[{error, {loc(T), epp, {bad, Doc}}}], St};
doc_file([{'(', _}, {'{', _}, T | _], Doc, St) ->
    {[{error, {loc(T), epp, {Doc, invalid, file}}}], St};
doc_file(
    [{'{', _}, {atom, _, file}, {',', _}, {string, _, _} = Name, {'}', _}, {dot, _} = Dot], Doc, St
) ->
    doc_content(Name, Dot, Doc, St);
doc_file([{'{', _}, {atom, _, file} | _] = Ts, {atom, _, Tag} = Doc, St) ->
    T = find_mismatch(['{', {atom, file}, ',', string, '}', dot], Ts, Doc),
    {[{error, {loc(T), epp, {bad, Tag}}}], St};
doc_file([{'{', _}, T | _], {atom, _, Tag}, St) ->
    {[{error, {loc(T), epp, {Tag, invalid, file}}}], St}.

doc_content({string, _, Filename}, Dot, {atom, DocLoc, Doc}, #epp{name = Current} = St) ->
    case file:path_open([hd(St#epp.path)], Filename, [read, binary]) of
        {ok, Fd, Full} ->
            Read = clause_epp:read_all(Fd),
            ok = file:close(Fd),
            case Read of
                {ok, Bin} ->
                    Start = start_location(St#epp.location),
                    Text = [
                        {'-', Start},
                        {atom, Start, Doc},
                        {string, Start, unicode:characters_to_list(Bin)},
                        {dot, Start}
                    ],
                    Restore = file_form(
                        Current, erl_anno:new(loc(Dot)), loc(Dot), code, St#epp.deterministic
                    ),
                    {
                        [
                            file_form(Full, erl_anno:new(Start), Start, code, St#epp.deterministic),
                            {ok, Text},
                            Restore
                        ],
                        St
                    };
                {error, _} ->
                    {[{error, {DocLoc, epp, {Doc, file, Filename}}}], St}
            end;
        {error, enoent} ->
            Empty = [{'-', DocLoc}, {atom, DocLoc, Doc}, {string, DocLoc, ""}, {dot, DocLoc}],
            {[{warning, {DocLoc, epp, {Doc, file, Filename}}}, {ok, Empty}], St};
        {error, _} ->
            {[{error, {DocLoc, epp, {Doc, file, Filename}}}], St}
    end.

%% Macro definitions

define([{'(', _}, {Type, _, _} = Name | Ts], Define, St) when Type =:= atom; Type =:= var ->
    define_body(Ts, Name, Define, St);
define(Ts, Define, St) ->
    {[{error, {loc(find_mismatch(['(', var_or_atom], Ts, Define)), epp, {bad, define}}}], St}.

define_body([{',', _} = Comma | Ts], Name, _Define, St) ->
    try expansion(Ts, Comma) of
        Expansion -> define_macro(none, {none, Expansion}, Name, St)
    catch
        throw:{error, L, What} -> {[{error, {L, epp, What}}], St}
    end;
define_body([{'(', _} = Open | Ts], Name, _Define, St) ->
    try parameters(Ts, [], Open) of
        {ok, {Args, _} = Def} -> define_macro(length(Args), Def, Name, St)
    catch
        throw:{error, L, What} -> {[{error, {L, epp, What}}], St}
    end;
define_body(Ts, _Name, Define, St) ->
    {[{error, {loc(no_match(Ts, Define)), epp, {bad, define}}}], St}.

%% A user macro may be overloaded on its number of arguments; predefined macros cannot be redefined.
define_macro(Arity, Def, {_, _, Key} = Name, #epp{macs = Macros} = St) ->
    case Macros of
        #{Key := Defs} when is_list(Defs) ->
            case proplists:is_defined(Arity, Defs) of
                true -> {[{error, {loc(Name), epp, {redefine, Key}}}], St};
                false -> store_macro(St, Key, Defs, Arity, Def)
            end;
        #{Key := _} ->
            {[{error, {loc(Name), epp, {redefine_predef, Key}}}], St};
        #{} ->
            store_macro(St, Key, [], Arity, Def)
    end.

%% The macros a definition uses are recorded to find circular expansions.
store_macro(#epp{macs = Macros, uses = Uses} = St, Key, Defs, Arity, Def) ->
    try lists:usort(macro_refs(element(2, Def))) of
        Used ->
            Previous = maps:get(Key, Uses, []),
            scan(St#epp{
                uses = Uses#{Key => [{Arity, Used} | Previous]},
                macs = Macros#{Key => [{Arity, Def} | Defs]}
            })
    catch
        {error, L, Reason} -> {[{error, {L, epp, Reason}}], St}
    end.

macro_refs([]) ->
    [];
macro_refs([{'?', _}, {'?', _} | Rest]) ->
    macro_refs(Rest);
macro_refs([{'?', _}, {Type, _, A} = T | Rest]) when Type =:= atom; Type =:= var ->
    [{A, count_args(Rest, loc(T), A)} | macro_refs(Rest)];
macro_refs([_ | Rest]) ->
    macro_refs(Rest).

parameters([{')', _} = Close | Ts], Args, _T0) ->
    {ok, {lists:reverse(Args), parameters_end(Ts, Close)}};
parameters([{var, _, Name} = T | Ts], Args, _T0) ->
    check_parameter(Name, Args, T),
    more_parameters(Ts, [Name | Args], T);
parameters(Ts, _Args, T0) ->
    throw({error, loc(no_match(Ts, T0)), {bad, define}}).

more_parameters([{')', _} = Close | Ts], Args, _T0) ->
    {ok, {lists:reverse(Args), parameters_end(Ts, Close)}};
more_parameters([{',', _}, {var, _, Name} = T | Ts], Args, _T0) ->
    check_parameter(Name, Args, T),
    more_parameters(Ts, [Name | Args], T);
more_parameters(Ts, _Args, T0) ->
    throw({error, loc(no_match(Ts, T0)), {bad, define}}).

parameters_end([{',', _} = Comma | Ts], _T0) -> expansion(Ts, Comma);
parameters_end(Ts, T0) -> throw({error, loc(no_match(Ts, T0)), missing_comma}).

expansion([{')', _}, {dot, _}], _T0) -> [];
expansion([{dot, _} = Dot], _T0) -> throw({error, loc(Dot), missing_parenthesis});
expansion([T | Ts], _T0) -> [T | expansion(Ts, T)];
expansion([], T0) -> throw({error, loc(T0), premature_end}).

check_parameter(Name, Args, T) ->
    case lists:member(Name, Args) of
        true -> throw({error, loc(T), {duplicated_argument, Name}});
        false -> ok
    end.

undef([{'(', _}, {Type, _, M}, {')', _}, {dot, _}], _Undef, St) when Type =:= atom; Type =:= var ->
    scan(St#epp{macs = maps:remove(M, St#epp.macs), uses = maps:remove(M, St#epp.uses)});
undef(Ts, Undef, St) ->
    {
        [{error, {loc(find_mismatch(['(', var_or_atom, ')', dot], Ts, Undef)), epp, {bad, undef}}}],
        St
    }.

%% Includes

include([{'(', _}, {string, _, Name0} = Name, {')', _}, {dot, _}], _Include, St) ->
    enter_include(expand_var(Name0), Name, St);
include(Ts, Include, St) ->
    {
        [{error, {loc(find_mismatch(['(', string, ')', dot], Ts, Include)), epp, {bad, include}}}],
        St
    }.

enter_include(_Name, Include, St) when length(St#epp.sstk) >= ?MAX_INCLUDE_DEPTH ->
    {[{error, {loc(Include), epp, {depth, "include"}}}], St};
enter_include(Name, Include, St) ->
    case file:path_open(St#epp.path, Name, [read, binary]) of
        {ok, Fd, Full} -> enter_file(Fd, Full, St, start_location(St#epp.location));
        {error, _} -> {[{error, {loc(Include), epp, {include, file, Name}}}], St}
    end.

%% -include_lib(Name): through the include path, then in the directory of the application its first part names.
include_lib([{'(', _}, {string, _, _}, {')', _}, {dot, _}], Include, St) when
    length(St#epp.sstk) >= ?MAX_INCLUDE_DEPTH
->
    {[{error, {loc(Include), epp, {depth, "include_lib"}}}], St};
include_lib([{'(', _}, {string, _, Name0} = Token, {')', _}, {dot, _}], _Include, St) ->
    Name = expand_var(Name0),
    Location = start_location(St#epp.location),
    case file:path_open(St#epp.path, Name, [read, binary]) of
        {ok, Fd, Full} ->
            enter_file(Fd, Full, St, Location);
        {error, _} ->
            case library_header(Name) of
                {ok, Header} ->
                    case file:open(Header, [read, binary]) of
                        {ok, Fd} -> enter_file(Fd, Header, St, Location);
                        {error, _} -> {[{error, {loc(Token), epp, {include, lib, Name}}}], St}
                    end;
                error ->
                    {[{error, {loc(Token), epp, {include, lib, Name}}}], St}
            end
    end;
include_lib(Ts, Include, St) ->
    {
        [
            {error, {
                loc(find_mismatch(['(', string, ')', dot], Ts, Include)), epp, {bad, include_lib}
            }}
        ],
        St
    }.

library_header(Name) ->
    try
        [App | Path] = filename:split(Name),
        {ok, join_names([code:lib_dir(list_to_atom(App)) | Path])}
    catch
        _:_ -> error
    end.

%% An included file: its own state, with the including file's state saved; the included file's directory heads the
%% include path in place of the including file's.
enter_file(Fd, Full, St0, Location) ->
    #epp{macs = Macros0, default_encoding = Default, deterministic = Deterministic} = St0,
    Reply = file_form(Full, erl_anno:new(Location), Location, code, Deterministic),
    {_, Src} = read_source(Fd, Default),
    Macros = Macros0#{
        'FILE' := {none, [{string, erl_anno:new(Location), source_name(Deterministic, Full)}]}
    },
    St = #epp{
        file = Fd,
        src = Src,
        location = Location,
        name = Full,
        name2 = Full,
        delta = 0,
        sstk = [St0 | St0#epp.sstk],
        path = [filename:dirname(Full) | tl(St0#epp.path)],
        macs = Macros,
        in_prefix = St0#epp.in_prefix,
        features = St0#epp.features,
        scan_opts = St0#epp.scan_opts,
        else_reserved = St0#epp.else_reserved,
        default_encoding = Default,
        deterministic = Deterministic
    },
    {[Reply], St}.

%% The end of a file: unterminated conditionals are errors, then back in the including file (with a -file form, and
%% another for a -file the including file had set), or eof.
leave(#epp{istk = [I | Cis]} = St) ->
    {[{error, {St#epp.location, epp, {illegal, "unterminated", I}}}], St#epp{
        istk = Cis, resume = leave
    }};
leave(#epp{sstk = [Old | Outer]} = St) ->
    close_file(St),
    #epp{location = OldLocation, delta = Delta, name = OldName, name2 = OldName2} = Old,
    Current = add_line(OldLocation, Delta),
    Anno = erl_anno:new(Current),
    Macros = (St#epp.macs)#{
        'FILE' := {none, [{string, Anno, source_name(St#epp.deterministic, OldName2)}]}
    },
    Next = Old#epp{
        sstk = Outer,
        macs = Macros,
        uses = St#epp.uses,
        in_prefix = St#epp.in_prefix,
        features = St#epp.features,
        else_reserved = St#epp.else_reserved,
        scan_opts = St#epp.scan_opts
    },
    Back = file_form(OldName, Anno, Current, code, St#epp.deterministic),
    Renamed = [
        file_form(OldName2, erl_anno:new(OldLocation), Current, generated, St#epp.deterministic)
     || OldName2 =/= OldName
    ],
    {[Back | Renamed], Next};
leave(#epp{sstk = []} = St) ->
    {[{eof, St#epp.location}], St}.

%% -file(Name, Line): lines from here on count from Line in Name.
file_directive(
    [{'(', _}, {string, _, Name}, {',', _}, {integer, _, Line}, {')', _}, {dot, _}], File, St
) ->
    Reply = file_form(Name, erl_anno:new(Line), loc(File), generated, St#epp.deterministic),
    Macros = (St#epp.macs)#{
        'FILE' := {none, [{string, erl_anno:new(1), source_name(St#epp.deterministic, Name)}]}
    },
    Location = new_location(Line, St#epp.location, loc(File)),
    Delta = erl_anno:line(element(2, File)) - Line + St#epp.delta,
    {[Reply], St#epp{name2 = Name, location = Location, delta = Delta, macs = Macros}};
file_directive(Ts, File, St) ->
    {
        [
            {error, {
                loc(find_mismatch(['(', string, ',', integer, ')', dot], Ts, File)),
                epp,
                {bad, file}
            }}
        ],
        St
    }.

new_location(Line, End, File) when is_integer(File) -> Line + (End - File);
new_location(Line, {End, _}, {File, _}) -> {Line + (End - File), 1}.

%% Conditionals

ifdef([{'(', _}, {Type, _, M}, {')', _}, {dot, _}], {atom, _, Tag}, St, Defined) when
    Type =:= atom; Type =:= var
->
    case is_defined(M, St) =:= Defined of
        true -> scan(St#epp{istk = [Tag | St#epp.istk]});
        false -> skip(St, [Tag])
    end;
ifdef(Ts, {atom, _, Tag} = Token, St, _Defined) ->
    Error = {error, {loc(find_mismatch(['(', var_or_atom, ')', dot], Ts, Token)), epp, {bad, Tag}}},
    {[Error], St#epp{resume = {skip, [Tag]}}}.

is_defined(Name, #epp{macs = Macros}) ->
    case Macros of
        #{Name := undefined} -> false;
        #{Name := _} -> true;
        #{} -> false
    end.

else_directive([{dot, _}], Else, St) ->
    case St#epp.istk of
        ['else' | Cis] ->
            {[{error, {loc(Else), epp, {illegal, "repeated", 'else'}}}], St#epp{
                istk = Cis, resume = {skip, ['else']}
            }};
        [_ | Cis] ->
            skip(St#epp{istk = Cis}, ['else']);
        [] ->
            {[{error, {loc(Else), epp, {illegal, "unbalanced", 'else'}}}], St}
    end;
else_directive(Ts, Else, St) ->
    {[{error, {loc(no_match(Ts, Else)), epp, {bad, 'else'}}}], St}.

if_directive([{'(', _} | _] = Ts, If, St) ->
    try eval_if(Ts, St) of
        true -> scan(St#epp{istk = ['if' | St#epp.istk]});
        _ -> skip(St, ['if'])
    catch
        throw:Error0 ->
            Error =
                case Error0 of
                    {_, erl_parse, _} -> {error, Error0};
                    {error, L, What} -> {error, {L, epp, What}};
                    _ -> {error, {loc(If), epp, Error0}}
                end,
            {[Error], St#epp{resume = {skip, ['if']}}}
    end;
if_directive(Ts, If, St) ->
    {[{error, {loc(no_match(Ts, If)), epp, {bad, 'if'}}}], St#epp{resume = {skip, ['if']}}}.

%% The condition of -if and -elif: a guard expression, defined(M) told by the macros, evaluated; any error is false.
eval_if(Ts0, St) ->
    Ts = expand(Ts0, St),
    Es0 =
        case erl_parse:parse_exprs(Ts) of
            {ok, Es} -> Es;
            {error, E} -> throw(E)
        end,
    Es1 = defined_calls(Es0, St),
    case Es1 of
        [E1] ->
            clause_erl_lint:is_guard_expr(E1) orelse throw({bad, 'if'});
        _ ->
            throw({bad, 'if'})
    end,
    try
        erl_eval:exprs(
            Es1, erl_eval:new_bindings(), {value, fun(_Name, _Args) -> error(badarg) end}
        )
    of
        {value, Result, _} -> Result
    catch
        _:_ -> false
    end.

defined_calls({call, _, {atom, _, defined}, [N0]}, #epp{macs = Macros}) ->
    N =
        case N0 of
            {var, _, N1} -> N1;
            {atom, _, N1} -> N1;
            _ -> throw({bad, 'if'})
        end,
    {atom, erl_anno:new(0), is_map_key(N, Macros)};
defined_calls([H | T], St) ->
    [defined_calls(H, St) | defined_calls(T, St)];
defined_calls(T, St) when is_tuple(T) ->
    list_to_tuple(defined_calls(tuple_to_list(T), St));
defined_calls(Other, _St) ->
    Other.

elif(Elif, St) ->
    case St#epp.istk of
        ['else' | Cis] ->
            {[{error, {loc(Elif), epp, {illegal, "unbalanced", 'elif'}}}], St#epp{
                istk = Cis, resume = {skip, ['else']}
            }};
        [_ | Cis] ->
            skip(St#epp{istk = Cis}, ['elif']);
        [] ->
            {[{error, {loc(Elif), epp, {illegal, "unbalanced", elif}}}], St}
    end.

endif([{dot, _}], Endif, St) ->
    case St#epp.istk of
        [_ | Cis] -> scan(St#epp{istk = Cis});
        [] -> {[{error, {loc(Endif), epp, {illegal, "unbalanced", endif}}}], St}
    end;
endif(Ts, Endif, St) ->
    {[{error, {loc(no_match(Ts, Endif)), epp, {bad, endif}}}], St}.

%% Skipping forms until the conditionals Sis close, nested ones counted.
skip(St, []) ->
    scan(St);
skip(St0, [I | Sis] = Stack) ->
    case read_form(St0) of
        {{ok, Ts, End}, St1} ->
            skip_form(Ts, St1#epp{location = End}, Stack);
        {{error, {_, file_io_server, invalid_unicode} = E, _End}, St1} ->
            {[{error, E}], St0#epp{src = St1#epp.src, resume = leave}};
        {{error, _, End}, St1} ->
            skip(St1#epp{location = End}, Stack);
        {{eof, End}, St1} ->
            leave(St1#epp{location = End, istk = [I | Sis]});
        {{error, _}, St1} ->
            {[{error, {St0#epp.location, epp, cannot_parse}}], St0#epp{
                src = St1#epp.src, resume = leave
            }}
    end.

skip_form([{'-', _}, {atom, _, Tag} | _], St, Stack) when Tag =:= ifdef; Tag =:= ifndef ->
    skip(St, [Tag | Stack]);
skip_form([{'-', _}, {'if', _} | _], St, Stack) ->
    skip(St, ['if' | Stack]);
skip_form([{'-', _}, {atom, _, 'else'} = Else | _], St, Stack) ->
    skip_else(Else, St, Stack);
skip_form([{'-', _}, {'else', _} = Else | _], #epp{else_reserved = true} = St, Stack) ->
    skip_else(Else, St, Stack);
skip_form([{'-', _}, {atom, _, elif} = Elif | Ts], St, Stack) ->
    skip_elif(Ts, Elif, St, Stack);
skip_form([{'-', _}, {atom, _, endif} | _], St, [_ | Sis]) ->
    skip(St, Sis);
skip_form(_, St, Stack) ->
    skip(St, Stack).

skip_else(Else, St, ['else' | _] = Stack) ->
    {[{error, {loc(Else), epp, {illegal, "repeated", 'else'}}}], St#epp{resume = {skip, Stack}}};
skip_else(_Else, St, ['elif' | Sis]) ->
    skip(St, ['else' | Sis]);
skip_else(_Else, St, [_]) ->
    scan(St#epp{istk = ['else' | St#epp.istk]});
skip_else(_Else, St, Stack) ->
    skip(St, Stack).

skip_elif(_Ts, Elif, St, ['else' | _] = Stack) ->
    {[{error, {loc(Elif), epp, elif_after_else}}], St#epp{resume = {skip, Stack}}};
skip_elif(Ts, Elif, St, [_]) ->
    if_directive(Ts, Elif, St);
skip_elif(_Ts, _Elif, St, Stack) ->
    skip(St, Stack).

%% Macro expansion

%% Tokens with their macros expanded, the expansion at the location of the call; {error, Location, What} thrown.
expand([{'?', _}, {atom, _, M} = Name | Ts], St) ->
    expand_call(Name, M, Ts, St);
expand([{'?', Q}, {var, L, 'FUNCTION_NAME'} = Token | Ts], St0) ->
    St = function_name(Token, St0),
    case St#epp.fname of
        undefined -> [{'?', Q}, Token];
        {Name, _} -> [{atom, L, Name}]
    end ++ expand(Ts, St);
expand([{'?', Q}, {var, L, 'FUNCTION_ARITY'} = Token | Ts], St0) ->
    St = function_name(Token, St0),
    case St#epp.fname of
        undefined -> [{'?', Q}, Token];
        {_, Arity} -> [{integer, L, Arity}]
    end ++ expand(Ts, St);
expand([{'?', _}, {var, L, 'LINE'} = Token | Ts], St) ->
    [{integer, L, erl_scan:line(Token)} | expand(Ts, St)];
expand([{'?', _}, {var, _, M} = Name | Ts], St) ->
    expand_call(Name, M, Ts, St);
expand([{'?', _}, Token | _], _St) ->
    Text =
        case erl_scan:text(Token) of
            Chars when is_list(Chars) -> Chars;
            undefined -> io_lib:fwrite(<<"~tp">>, [erl_scan:symbol(Token)])
        end,
    throw({error, loc(Token), {call, [$? | Text]}});
expand([T | Ts], St) ->
    [T | expand(Ts, St)];
expand([], _St) ->
    [].

expand_call(Name, M, Ts, #epp{macs = Macros, uses = Uses} = St) ->
    L = loc(Name),
    Anno = element(2, Name),
    case definition(L, M, Ts, Macros) of
        {none, Body} ->
            check_uses([{M, none}], [], Uses, L),
            expand(expand(substitute(Body, Anno, [], #{}), St) ++ Ts, St);
        {Params, Body} ->
            check_uses([{M, length(Params)}], [], Uses, L),
            {Bindings, Rest} = bind_args(Ts, L, M, Params, #{}),
            expand(substitute(Body, Anno, Rest, Bindings), St)
    end.

%% The definition of macro M for the call's number of arguments.
definition(L, M, Ts, Macros) ->
    Arity = count_args(Ts, L, M),
    case Macros of
        #{M := undefined} ->
            throw({error, L, {undefined, M, Arity}});
        #{M := [{none, Def}]} ->
            Def;
        #{M := Defs} when is_list(Defs) ->
            case proplists:get_value(Arity, Defs) of
                undefined -> throw({error, L, {mismatch, M}});
                Def -> Def
            end;
        #{M := Predefined} ->
            Predefined;
        #{} ->
            throw({error, L, {undefined, M, Arity}})
    end.

%% A macro expanding to itself, directly or through others, is circular.
check_uses([], _Ancestors, _Uses, _L) ->
    ok;
check_uses([{Name, Arity} = M | Rest], Ancestors, Uses, L) ->
    case lists:member(M, Ancestors) of
        true ->
            throw({error, L, {circular, Name, Arity}});
        false ->
            check_uses(uses_of(M, Uses), [M | Ancestors], Uses, L),
            check_uses(Rest, Ancestors, Uses, L)
    end.

uses_of({M, Arity}, Uses) ->
    case Uses of
        #{M := List} -> proplists:get_value(Arity, List, proplists:get_value(none, List, []));
        #{} -> []
    end.

bind_args([{'(', _}, {')', _} | Ts], _L, _M, [], Bindings) ->
    {Bindings, Ts};
bind_args([{'(', _} | Ts0], L, M, [A | As], Bindings) ->
    {Arg, Ts} = macro_arg(Ts0, [], []),
    more_args(Ts, L, M, As, store_arg(L, M, A, Arg, Bindings));
bind_args(_Ts, L, M, _As, _Bindings) ->
    throw({error, L, {mismatch, M}}).

more_args([{')', _} | Ts], _L, _M, [], Bindings) ->
    {Bindings, Ts};
more_args([{',', _} | Ts0], L, M, [A | As], Bindings) ->
    {Arg, Ts} = macro_arg(Ts0, [], []),
    more_args(Ts, L, M, As, store_arg(L, M, A, Arg, Bindings));
more_args([], L, M, _As, _Bindings) ->
    throw({error, L, {arg_error, M}});
more_args(_Ts, L, M, _As, _Bindings) ->
    throw({error, L, {mismatch, M}}).

store_arg(L, M, _A, [], _Bindings) -> throw({error, L, {mismatch, M}});
store_arg(_L, _M, A, Arg, Bindings) -> Bindings#{A => Arg}.

%% The number of arguments of a macro call: none without parentheses.
count_args([{'(', _}, {')', _} | _], _L, _M) ->
    0;
count_args([{'(', _}, {',', _} | _], L, M) ->
    throw({error, L, {arg_error, M}});
count_args([{'(', _} | Ts0], L, M) ->
    {_, Ts} = macro_arg(Ts0, [], []),
    count_more(Ts, L, M, 1);
count_args(_Ts, _L, _M) ->
    none.

count_more([{')', _} | _], _L, _M, N) ->
    N;
count_more([{',', _}, {')', _} | _], L, M, _N) ->
    throw({error, L, {arg_error, M}});
count_more([{',', _} | Ts0], L, M, N) ->
    {_, Ts} = macro_arg(Ts0, [], []),
    count_more(Ts, L, M, N + 1);
count_more([], L, M, _N) ->
    throw({error, L, {arg_error, M}});
count_more(_Ts, L, M, _N) ->
    throw({error, L, {mismatch, M}}).

%% One macro argument: tokens up to a , or ) outside brackets and keyword blocks. A fun ( may begin a fun type,
%% which ends at a , or ), or a fun expression, which ends at end.
macro_arg([{',', _} = T | Ts], [], Arg) ->
    {lists:reverse(Arg), [T | Ts]};
macro_arg([{')', _} = T | Ts], [], Arg) ->
    {lists:reverse(Arg), [T | Ts]};
macro_arg([{'fun', _} = Fun | [{'(', _} | _] = Ts], Closes, Arg) ->
    macro_arg(Ts, [fun_end | Closes], [Fun | Arg]);
macro_arg([{'fun', _} = Fun, {var, _, _} = Name | [{'(', _} | _] = Ts], Closes, Arg) ->
    macro_arg(Ts, ['end' | Closes], [Name, Fun | Arg]);
macro_arg([{Open, _} = T | Ts], Closes, Arg) when
    Open =:= '(';
    Open =:= '<<';
    Open =:= '[';
    Open =:= '{';
    Open =:= 'begin';
    Open =:= 'if';
    Open =:= 'case';
    Open =:= 'maybe';
    Open =:= 'receive';
    Open =:= 'try';
    Open =:= 'cond'
->
    macro_arg(Ts, [closing(Open) | Closes], [T | Arg]);
macro_arg([{Clause, _} | _] = Ts, [fun_end | Closes], Arg) when
    Clause =:= 'when'; Clause =:= '->'
->
    macro_arg(Ts, ['end' | Closes], Arg);
macro_arg([{C, _} = T | Ts], [fun_end | Closes], Arg) ->
    case C =:= ',' orelse C =:= ')' of
        true -> macro_arg([T | Ts], Closes, Arg);
        false -> macro_arg(Ts, [fun_end | Closes], [T | Arg])
    end;
macro_arg([{C, _} = T | Ts], [C | Closes], Arg) ->
    macro_arg(Ts, Closes, [T | Arg]);
macro_arg([T | Ts], Closes, Arg) ->
    macro_arg(Ts, Closes, [T | Arg]);
macro_arg([], _Closes, Arg) ->
    {lists:reverse(Arg), []}.

closing('(') -> ')';
closing('<<') -> '>>';
closing('[') -> ']';
closing('{') -> '}';
closing(_) -> 'end'.

%% The macro body with its parameters replaced by the arguments (??Arg by their text): body tokens take the call's
%% annotation, argument tokens keep theirs, and tokens after an argument take the annotation of its last token.
substitute([{var, _, V} | Ts], Anno, Rest, Bindings) ->
    case Bindings of
        #{V := Arg} -> insert_arg(Arg, Ts, Anno, Rest, Bindings);
        #{} -> [{var, Anno, V} | substitute(Ts, Anno, Rest, Bindings)]
    end;
substitute([{'?', _}, {'?', _}, {var, _, V} | Ts], Anno, Rest, Bindings) ->
    case Bindings of
        #{V := Arg} -> insert_arg(stringify(Arg, Anno), Ts, Anno, Rest, Bindings);
        #{} -> [{var, Anno, V} | substitute(Ts, Anno, Rest, Bindings)]
    end;
substitute([T | Ts], Anno, Rest, Bindings) ->
    [setelement(2, T, Anno) | substitute(Ts, Anno, Rest, Bindings)];
substitute([], _Anno, Rest, _Bindings) ->
    Rest.

insert_arg([A | As], Ts, _Anno, Rest, Bindings) ->
    [A | insert_arg(As, Ts, element(2, A), Rest, Bindings)];
insert_arg([], Ts, Anno, Rest, Bindings) ->
    substitute(Ts, Anno, Rest, Bindings).

%% ?FUNCTION_NAME and ?FUNCTION_ARITY: the name and arity of the function the form begins, found once per form.
function_name(Token, #epp{fname = Ts0} = St) when is_list(Ts0) ->
    try expand(Ts0, St#epp{fname = undefined}) of
        [{atom, _, Name}, {'(', _} | Ts] ->
            St#epp{fname = arity(Ts, 1, {Name, 0})};
        [{'?', _} | _] ->
            {var, _, Macro} = Token,
            throw({error, loc(Token), {illegal_function_usage, Macro}});
        _ ->
            {var, _, Macro} = Token,
            throw({error, loc(Token), {illegal_function, Macro}})
    catch
        throw:_ -> St#epp{fname = {'_', 0}}
    end;
function_name(_Token, St) ->
    St.

arity([T | Ts], Depth, {Name, Arity} = FA) ->
    case bracket(T) of
        comma when Depth =:= 1 -> arity(Ts, Depth, {Name, Arity + 1});
        comma -> arity(Ts, Depth, FA);
        left when Arity =:= 0 -> arity(Ts, Depth + 1, {Name, 1});
        left -> arity(Ts, Depth + 1, FA);
        right when Depth =:= 1 -> FA;
        right -> arity(Ts, Depth - 1, FA);
        other when Arity =:= 0 -> arity(Ts, Depth, {Name, 1});
        other -> arity(Ts, Depth, FA)
    end;
arity([], _Depth, FA) ->
    FA.

bracket({C, _}) when C =:= ',' -> comma;
bracket({C, _}) when C =:= '('; C =:= '{'; C =:= '['; C =:= '<<' -> left;
bracket({C, _}) when C =:= ')'; C =:= '}'; C =:= ']'; C =:= '>>' -> right;
bracket(_) -> other.

%% ??Arg: one string token holding the argument's tokens as text, separated by spaces.
stringify(Ts, Anno) ->
    [$\s | S] = lists:flatten([io_lib:format(" ~ts", [token_text(T)]) || T <- Ts]),
    [{string, Anno, S}].

token_text({dot, _}) -> ".";
token_text({X, _}) when is_atom(X) -> atom_to_list(X);
token_text({var, _, X}) -> atom_to_list(X);
token_text({char, _, C}) -> io_lib:write_char(C);
token_text({string, _, X}) -> io_lib:write_string(X);
token_text({atom, _, X}) -> io_lib:write_atom(X);
token_text({_, _, X}) -> io_lib:format("~w", [X]).

%% Adjacent string tokens as one, at the first's annotation.
coalesce_strings([{string, A, S} | Ts]) -> coalesce_strings(Ts, A, [S]);
coalesce_strings([T | Ts]) -> [T | coalesce_strings(Ts)];
coalesce_strings([]) -> [].

coalesce_strings([{string, _, S} | Ts], A, Acc) ->
    coalesce_strings(Ts, A, [S | Acc]);
coalesce_strings(Ts, A, Acc) ->
    [{string, A, lists:append(lists:reverse(Acc))} | coalesce_strings(Ts)].

%% The token where a directive stops matching its expected shape.
find_mismatch([Tag | Tags], [{Tag, _} = T | Ts], _T0) ->
    find_mismatch(Tags, Ts, T);
find_mismatch([Tag | Tags], [{Tag, _, _} = T | Ts], _T0) ->
    find_mismatch(Tags, Ts, T);
find_mismatch([var_or_atom | Tags], [{Type, _, _} = T | Ts], _T0) when
    Type =:= var; Type =:= atom
->
    find_mismatch(Tags, Ts, T);
find_mismatch([{Tag, Value} | Tags], [{Tag, _, Value} = T | Ts], _T0) ->
    find_mismatch(Tags, Ts, T);
find_mismatch(_, Ts, T0) ->
    no_match(Ts, T0).

no_match([T | _], _T0) -> T;
no_match(_, T0) -> T0.

%% An include name starting with $VAR: VAR's value in its place when set.
expand_var("$" ++ _ = Name) ->
    try
        ["$" ++ Var | Rest] = filename:split(Name),
        Value = os:getenv(Var),
        true = Value =/= false,
        join_names([Value | Rest])
    catch
        error:_ -> Name
    end;
expand_var(Name) ->
    Name.

join_names(["." | [_ | _] = Rest]) -> join_names(Rest);
join_names(Components) -> filename:join(Components).

loc(Token) -> erl_scan:location(Token).

add_line(Line, Offset) when is_integer(Line) -> Line + Offset;
add_line({Line, Column}, Offset) -> {Line + Offset, Column}.

start_location(Line) when is_integer(Line) -> 1;
start_location({_, _}) -> {1, 1}.
