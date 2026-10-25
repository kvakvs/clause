%% Clause's escript (plan 21 step LIB55, docs/library.md): making and taking apart escript files, written from OTP's
%% documentation and following OTP's section layout and header reading. A Clause program is an executable rather
%% than a script, so script_name/0 is the executable's path and running escripts (start/0,1, parse_file/1) is
%% declined; extracting with compile_source needs the compiler and fails.
-module(escript).
-export([
    create/2,
    extract/2,
    parse_file/1,
    script_name/0,
    start/0,
    start/1
]).

-define(SHEBANG, "/usr/bin/env escript").
-define(COMMENT, "This is an -*- erlang -*- file").

% The sections of an escript: the body's type (source, beam or archive), the shebang, comment and emulator argument
% lines, and the body.
-record(sections, {type, shebang, comment, emu_args, body}).

% --- Making escripts ---

create(File, Options) when is_list(Options) ->
    try
        S = prepare(Options, #sections{}),
        BinList = [
            Section
         || Section <- [S#sections.shebang, S#sections.comment, S#sections.emu_args, S#sections.body],
            Section =/= undefined
        ],
        case File of
            binary -> {ok, list_to_binary(BinList)};
            _ -> written(file:write_file(File, BinList), File)
        end
    catch
        throw:PrepareReason -> {error, PrepareReason}
    end.

written(ok, _File) -> ok;
written({error, Reason}, File) -> {error, {Reason, File}}.

prepare([H | T], S) -> prepare(T, section(H, S));
prepare([], #sections{body = undefined}) -> throw(missing_body);
prepare([], #sections{type = Type} = S) when Type =:= source; Type =:= beam; Type =:= archive -> S;
prepare([], #sections{type = Type}) -> throw({illegal_type, Type});
prepare(BadOptions, _) -> throw({badarg, BadOptions}).

% One option's section; undefined leaves it out, default (or the bare atom) is OTP's text.
section({shebang, undefined}, S) ->
    S;
section(shebang, S) ->
    S#sections{shebang = "#!" ++ ?SHEBANG ++ "\n"};
section({shebang, default}, S) ->
    S#sections{shebang = "#!" ++ ?SHEBANG ++ "\n"};
section({shebang, Shebang}, S) when is_list(Shebang) -> S#sections{shebang = "#!" ++ Shebang ++ "\n"};
section({comment, undefined}, S) ->
    S;
section(comment, S) ->
    S#sections{comment = "%% " ++ ?COMMENT ++ "\n"};
section({comment, default}, S) ->
    S#sections{comment = "%% " ++ ?COMMENT ++ "\n"};
section({comment, Comment}, S) when is_list(Comment) -> S#sections{comment = "%% " ++ Comment ++ "\n"};
section({emu_args, undefined}, S) ->
    S;
section({emu_args, Args}, S) when is_list(Args) -> S#sections{emu_args = "%%!" ++ Args ++ "\n"};
section({Type, File} = H, S) when is_list(File) -> S#sections{type = Type, body = read_body(File, H)};
section({Type, Bin}, S) when is_binary(Bin) -> S#sections{type = Type, body = Bin};
section({archive = Type, ZipFiles, ZipOptions} = H, S) when is_list(ZipFiles), is_list(ZipOptions) ->
    S#sections{type = Type, body = archive(ZipFiles, ZipOptions, H)};
section(H, _S) ->
    throw({badarg, H}).

read_body(File, Option) ->
    case file:read_file(File) of
        {ok, Bin} -> Bin;
        {error, Reason} -> throw({Reason, Option})
    end.

archive(ZipFiles, ZipOptions, Option) ->
    File = "dummy.zip",
    case zip:create(File, ZipFiles, ZipOptions ++ [memory]) of
        {ok, {File, ZipBin}} -> ZipBin;
        {error, Reason} -> throw({Reason, Option})
    end.

% --- Taking escripts apart ---

extract(File, Options) when is_list(File), is_list(Options) ->
    try
        CompileSource = parse_extract_options(Options, false),
        {HeaderSz, _StartLine, Fd, Sections} = parse_header(File, not CompileSource),
        case {Sections#sections.type, CompileSource} of
            {source, true} ->
                ok = file:close(Fd),
                % Compiling the source to a beam needs the compiler.
                throw({compile, [{errors, ["compile_source needs the Erlang compiler"]}, {warnings, []}]});
            {_, _} ->
                ok = file:close(Fd),
                return_sections(Sections, body(File, HeaderSz))
        end
    catch
        throw:Reason -> {error, Reason}
    end.

body(File, HeaderSz) ->
    case file:read_file(File) of
        {ok, <<_Header:HeaderSz/binary, Bin/binary>>} -> Bin;
        {error, ReadReason} -> throw(ReadReason)
    end.

parse_extract_options([compile_source | T], _) -> parse_extract_options(T, true);
parse_extract_options([H | _], _) -> throw({badarg, H});
parse_extract_options([], CompileSource) -> CompileSource.

return_sections(S, Bin) ->
    {ok, [
        normalize_section(shebang, S#sections.shebang),
        normalize_section(comment, S#sections.comment),
        normalize_section(emu_args, S#sections.emu_args),
        normalize_section(S#sections.type, Bin)
    ]}.

normalize_section(Name, undefined) ->
    {Name, undefined};
normalize_section(shebang, "#!" ++ Chars) ->
    Stripped = string:trim(string:trim(Chars, trailing, "$\n"), both),
    case Stripped =:= ?SHEBANG of
        true -> {shebang, default};
        false -> {shebang, Stripped}
    end;
normalize_section(comment, Chars) ->
    Chopped = string:trim(Chars, trailing, "$\n"),
    Stripped = string:trim(string:trim(Chopped, leading, "$%"), both),
    case Stripped =:= ?COMMENT of
        true -> {comment, default};
        false -> {comment, Stripped}
    end;
normalize_section(emu_args, "%%!" ++ Chars) ->
    {emu_args, string:trim(string:trim(Chars, trailing, "$\n"), both)};
normalize_section(Name, Chars) ->
    {Name, Chars}.

% The header's size and lines: a shebang line, then an optional comment and emulator arguments line; without a
% shebang the first line is skipped (or kept, when KeepFirst).
parse_header(File, KeepFirst) ->
    LineNo = 1,
    {ok, Fd} =
        case file:open(File, [read]) of
            {ok, Fd0} -> {ok, Fd0};
            {error, R} -> fatal(lists:concat([file:format_error(R), ": '", File, "'"]))
        end,
    {ok, HeaderSz0} = file:position(Fd, cur),
    Line1 = get_line(Fd),
    case classify_line(Line1) of
        shebang -> find_first_body_line(Fd, HeaderSz0, LineNo, KeepFirst, #sections{shebang = Line1});
        archive -> {HeaderSz0, LineNo, Fd, #sections{type = archive}};
        beam -> {HeaderSz0, LineNo, Fd, #sections{type = beam}};
        _ -> find_first_body_line(Fd, HeaderSz0, LineNo, KeepFirst, #sections{})
    end.

find_first_body_line(Fd, HeaderSz0, LineNo, KeepFirst, Sections) ->
    {ok, HeaderSz1} = file:position(Fd, cur),
    Line2 = get_line(Fd),
    {ok, HeaderSz2} = file:position(Fd, cur),
    if
        Sections#sections.shebang =:= undefined, KeepFirst =:= true ->
            {HeaderSz0, LineNo, Fd, Sections#sections{type = guess_type(Line2)}};
        Sections#sections.shebang =:= undefined ->
            {HeaderSz1, LineNo, Fd, Sections#sections{type = guess_type(Line2)}};
        true ->
            after_shebang(Fd, {HeaderSz1, HeaderSz2}, LineNo, Line2, Sections)
    end.

% After a shebang: emulator arguments on the second line, or a comment possibly followed by them.
after_shebang(Fd, {HeaderSz1, HeaderSz2}, LineNo, Line2, Sections) ->
    case classify_line(Line2) of
        emu_args ->
            Line3 = get_line(Fd),
            {HeaderSz2, LineNo + 2, Fd, Sections#sections{
                type = guess_type(Line3), comment = undefined, emu_args = Line2
            }};
        comment ->
            after_comment(Fd, HeaderSz2, LineNo, Line2, Sections);
        _ ->
            {HeaderSz1, LineNo + 1, Fd, Sections#sections{type = guess_type(Line2)}}
    end.

after_comment(Fd, HeaderSz2, LineNo, Line2, Sections) ->
    Line3 = get_line(Fd),
    {ok, HeaderSz3} = file:position(Fd, cur),
    case classify_line(Line3) of
        emu_args ->
            Line4 = get_line(Fd),
            {HeaderSz3, LineNo + 3, Fd, Sections#sections{type = guess_type(Line4), comment = Line2, emu_args = Line3}};
        _ ->
            {HeaderSz2, LineNo + 2, Fd, Sections#sections{type = guess_type(Line3), comment = Line2}}
    end.

classify_line(Line) ->
    case Line of
        "#!" ++ _ -> shebang;
        "PK" ++ _ -> archive;
        "FOR1" ++ _ -> beam;
        "%%!" ++ _ -> emu_args;
        "%" ++ _ -> comment;
        _ -> undefined
    end.

guess_type(Line) ->
    case classify_line(Line) of
        archive -> archive;
        beam -> beam;
        _ -> source
    end.

get_line(P) ->
    case io:get_line(P, '') of
        eof -> fatal("Premature end of file reached");
        Line -> Line
    end.

fatal(Str) -> throw(Str).

% --- The running script ---

% The executable's path: a Clause program is its own script.
script_name() -> init:script_name().

% Running escripts needs the emulator's code loading.
start() -> erlang:error({not_implemented, start, 0}).
start(_) -> erlang:error({not_implemented, start, 1}).
parse_file(_) -> erlang:error({not_implemented, parse_file, 1}).
