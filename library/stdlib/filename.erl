%% Clause's filename (plan 21 step LIB5, docs/library.md): file name manipulation for the host's os:type(), Unix
%% (/ separators) or Windows (/ and \, drive letters). The functions walk names as OTP's do, so results, their
%% list or binary shapes and error reasons agree; Erlang, since each is a walk over a name's characters.
-module(filename).
-export([
    absname/1,
    absname/2,
    absname_join/2,
    append/2,
    basedir/2,
    basedir/3,
    basename/1,
    basename/2,
    dirname/1,
    extension/1,
    flatten/1,
    join/1,
    join/2,
    nativename/1,
    pathtype/1,
    rootname/1,
    rootname/2,
    split/1,
    validate/1
]).

-define(IS_LETTER(L),
    (is_integer(L) andalso (($A =< L andalso L =< $Z) orelse ($a =< L andalso L =< $z)))
).

%% The family of the host system: win32 or unix.
family() -> element(1, os:type()).

%% Windows' directory and drive separators; none on Unix.
separators() ->
    case family() of
        win32 -> {$\\, $:};
        _ -> {false, false}
    end.

%% A deep list name flattened: atoms by their characters, binaries kept as elements; a binary as it is.
flatten(Bin) when is_binary(Bin) -> Bin;
flatten(List) -> flat(List, []).

flat([H | T], Tail) when is_list(H) -> flat(H, flat(T, Tail));
flat([H | T], Tail) when is_atom(H) -> atom_to_list(H) ++ flat(T, Tail);
flat([H | T], Tail) -> [H | flat(T, Tail)];
flat([], Tail) -> Tail;
flat(Atom, Tail) when is_atom(Atom) -> atom_to_list(Atom) ++ flatten(Tail).

%% A list name as a binary in the native name encoding; badarg when it cannot be encoded.
to_binary(List) ->
    case unicode:characters_to_binary(flatten(List), unicode, file:native_name_encoding()) of
        {error, _, _} -> erlang:error(badarg);
        Bin when is_binary(Bin) -> Bin
    end.

%% The absolute name of Name against the current directory or AbsBase.
absname(Name) ->
    {ok, Cwd} = file:get_cwd(),
    absname(Name, Cwd).

absname(Name, AbsBase) when is_binary(Name), is_list(AbsBase) ->
    absname(Name, to_binary(AbsBase));
absname(Name, AbsBase) when is_list(Name), is_binary(AbsBase) ->
    absname(to_binary(Name), AbsBase);
absname(Name, AbsBase) ->
    case pathtype(Name) of
        relative -> absname_join(AbsBase, Name);
        absolute -> join([flatten(Name)]);
        volumerelative -> volume_absname(split(Name), split(AbsBase), AbsBase)
    end.

%% A volume-relative Windows name made absolute: a leading slash takes AbsBase's volume, a drive its own current
%% directory (the drive's root when unknown).
volume_absname([<<"/">> | Rest], [Volume | _], _) ->
    join([Volume | Rest]);
volume_absname([<<X, $:>> | Rest], [<<X, _/binary>> | _], AbsBase) ->
    absname(join(Rest), AbsBase);
volume_absname([<<X, $:>> | Rest], _, _) ->
    Cwd =
        case file:get_cwd([X, $:]) of
            {ok, Dir} -> to_binary(Dir);
            {error, _} -> <<X, $:, $/>>
        end,
    absname(join(Rest), Cwd);
volume_absname(["/" | Rest], [Volume | _], _) ->
    join([Volume | Rest]);
volume_absname([[X, $:] | Rest], [[X | _] | _], AbsBase) ->
    absname(join(Rest), AbsBase);
volume_absname([[X, $:] | Rest], _, _) ->
    Cwd =
        case file:get_cwd([X, $:]) of
            {ok, Dir} -> Dir;
            {error, _} -> [X, $:, $/]
        end,
    absname(join(Rest), Cwd).

%% AbsBase joined with the flattened Name.
absname_join(AbsBase, Name) -> join(AbsBase, flatten(Name)).

%% Dir/Name, with no normalization.
append(Dir, Name) when is_binary(Dir), is_binary(Name) -> <<Dir/binary, $/, Name/binary>>;
append(Dir, Name) when is_binary(Dir) -> append(Dir, to_binary(Name));
append(Dir, Name) when is_binary(Name) -> append(to_binary(Dir), Name);
append(Dir, Name) -> Dir ++ [$/ | Name].

%% The last component of a name.
basename(Name) when is_binary(Name) ->
    case family() of
        win32 -> last_part(drop_drive(Name), [<<"/">>, <<"\\">>]);
        _ -> last_part(Name, [<<"/">>])
    end;
basename(Name0) ->
    {DirSep, DriveSep} = separators(),
    Name = skip_drive(flatten(Name0), DriveSep),
    last_component(Name, Name, DirSep).

drop_drive(<<L, $:, Rest/binary>>) when ?IS_LETTER(L) -> Rest;
drop_drive(Name) -> Name.

last_part(Bin, Separators) ->
    case [Part || Part <- binary:split(Bin, Separators, [global]), Part =/= <<>>] of
        [] -> <<>>;
        Parts -> lists:last(Parts)
    end.

%% The characters after the last separator (a trailing one dropped).
last_component([$/], Tail, _) ->
    [_ | Rest] = lists:reverse(Tail),
    lists:reverse(Rest);
last_component([$/ | Rest], _, DirSep) ->
    last_component(Rest, Rest, DirSep);
last_component([DirSep | Rest], Tail, DirSep) when is_integer(DirSep) ->
    last_component([$/ | Rest], Tail, DirSep);
last_component([C | Rest], Tail, DirSep) when is_integer(C) ->
    last_component(Rest, Tail, DirSep);
last_component([], Tail, _) ->
    Tail.

skip_drive(Name, false) -> Name;
skip_drive([L, DriveSep | Name], DriveSep) when ?IS_LETTER(L) -> Name;
skip_drive(Name, _) -> Name.

%% The last component without the extension Ext.
basename(Name, Ext) when is_binary(Name), is_list(Ext) ->
    basename(Name, to_binary(Ext));
basename(Name, Ext) when is_list(Name), is_binary(Ext) ->
    basename(to_binary(Name), Ext);
basename(Name, Ext) when is_binary(Name), is_binary(Ext) ->
    Base = basename(Name),
    Keep = byte_size(Base) - byte_size(Ext),
    Start = byte_size(Name) - Keep - byte_size(Ext),
    case Name of
        _ when Keep < 0 -> Base;
        <<_:Start/binary, Part:Keep/binary, Ext/binary>> -> Part;
        _ -> Base
    end;
basename(Name0, Ext0) ->
    {DirSep, DriveSep} = separators(),
    without_extension(skip_drive(flatten(Name0), DriveSep), flatten(Ext0), [], DirSep).

without_extension(Ext, Ext, Tail, _) ->
    lists:reverse(Tail);
without_extension([$/], Ext, Tail, DirSep) ->
    without_extension([], Ext, Tail, DirSep);
without_extension([$/ | Rest], Ext, _, DirSep) ->
    without_extension(Rest, Ext, [], DirSep);
without_extension([DirSep | Rest], Ext, Tail, DirSep) when is_integer(DirSep) ->
    without_extension([$/ | Rest], Ext, Tail, DirSep);
without_extension([C | Rest], Ext, Tail, DirSep) when is_integer(C) ->
    without_extension(Rest, Ext, [C | Tail], DirSep);
without_extension([], _, Tail, _) ->
    lists:reverse(Tail).

%% The directory part of a name.
dirname(Name) when is_binary(Name) ->
    {DirSep, DriveSep} = separators(),
    Separators = [<<"/">> | [<<DirSep>> || is_integer(DirSep)]],
    {Drive, Dirs} = split_drive(Name, DriveSep),
    Parts =
        case binary:split(Dirs, Separators, [global]) of
            [] -> [];
            All -> lists:reverse(strip_empty(tl(lists:reverse(All))))
        end,
    Prefix =
        case {Parts, Drive} of
            {[], <<>>} -> <<".">>;
            _ -> Drive
        end,
    dir_join(Parts, Prefix);
dirname(Name0) ->
    dir_chars(flatten(Name0), [], [], separators()).

split_drive(<<L, DriveSep, Rest/binary>>, DriveSep) when is_integer(DriveSep), ?IS_LETTER(L) ->
    {<<L, DriveSep>>, Rest};
split_drive(Name, _) ->
    {<<>>, Name}.

strip_empty([<<>>, X | Y]) -> strip_empty([X | Y]);
strip_empty(Parts) -> Parts.

dir_join([<<>> | Rest], Acc) -> dir_join_rest(Rest, <<Acc/binary, "/">>);
dir_join(Parts, Acc) -> dir_join_rest(Parts, Acc).

dir_join_rest([], Acc) -> Acc;
dir_join_rest([One], Acc) -> <<Acc/binary, One/binary>>;
dir_join_rest([Part | Rest], Acc) -> dir_join(Rest, <<Acc/binary, Part/binary, "/">>).

dir_chars([$/ | Rest], Dir, File, Seps) ->
    dir_chars(Rest, File ++ Dir, [$/], Seps);
dir_chars([DirSep | Rest], Dir, File, {DirSep, _} = Seps) when is_integer(DirSep) ->
    dir_chars(Rest, File ++ Dir, [$/], Seps);
dir_chars([L, DriveSep | Rest], [], [], {_, DriveSep} = Seps) when
    is_integer(DriveSep), ?IS_LETTER(L)
->
    dir_chars(Rest, [DriveSep, L], [], Seps);
dir_chars([C | Rest], Dir, File, Seps) when is_integer(C) ->
    dir_chars(Rest, Dir, [C | File], Seps);
dir_chars([], [], File, _) ->
    case lists:reverse(File) of
        [$/ | _] -> [$/];
        _ -> "."
    end;
dir_chars([], [$/ | Rest], File, Seps) ->
    dir_chars([], Rest, File, Seps);
dir_chars([], [DriveSep, L], File, {_, DriveSep}) ->
    case lists:reverse(File) of
        [$/ | _] -> [L, DriveSep, $/];
        _ -> [L, DriveSep]
    end;
dir_chars([], Dir, _, _) ->
    lists:reverse(Dir).

%% The extension of the last component, the dot included; empty without one or for a dotfile.
extension(Name) when is_binary(Name) ->
    {DirSep, _} = separators(),
    Separators = [<<"/">> | [<<DirSep>> || is_integer(DirSep)]],
    case binary:matches(Name, [<<".">>]) of
        [] ->
            <<>>;
        Dots ->
            case lists:last(Dots) of
                {0, _} ->
                    <<>>;
                {At, _} ->
                    <<_:(At - 1)/binary, Part/binary>> = Name,
                    case binary:match(Part, Separators) of
                        nomatch ->
                            <<_:At/binary, Result/binary>> = Name,
                            Result;
                        _ ->
                            <<>>
                    end
            end
    end;
extension(Name0) ->
    extension_chars([$/ | flatten(Name0)], [], family()).

extension_chars([$. | Rest] = Result, _, Family) ->
    extension_chars(Rest, Result, Family);
extension_chars([$/, $. | Rest], _, Family) ->
    extension_chars(Rest, [], Family);
extension_chars([$\\, $. | Rest], _, win32) ->
    extension_chars(Rest, [], win32);
extension_chars([C | Rest], [], Family) when is_integer(C) -> extension_chars(Rest, [], Family);
extension_chars([$/ | Rest], _, Family) ->
    extension_chars(Rest, [], Family);
extension_chars([$\\ | Rest], _, win32) ->
    extension_chars(Rest, [], win32);
extension_chars([C | Rest], Result, Family) when is_integer(C) ->
    extension_chars(Rest, Result, Family);
extension_chars([], Result, _) ->
    Result.

%% Components joined with separators and normalized: redundant separators removed, Windows' drive letters lowered
%% and backslashes made slashes.
join([Name1, Name2 | Rest]) -> join([join(Name1, Name2) | Rest]);
join([Name]) when is_list(Name) -> join_chars(Name, [], [], family());
join([Name]) when is_binary(Name) -> join_bytes(Name, <<>>, [], family());
join([Name]) when is_atom(Name) -> join([atom_to_list(Name)]).

join(Name1, Name2) when is_list(Name1), is_list(Name2) ->
    case pathtype(Name2) of
        relative -> join_chars(Name1, Name2, [], family());
        _ -> join_chars(Name2, [], [], family())
    end;
join(Name1, Name2) when is_binary(Name1), is_list(Name2) ->
    join(Name1, to_binary(Name2));
join(Name1, Name2) when is_list(Name1), is_binary(Name2) ->
    join(to_binary(Name1), Name2);
join(Name1, Name2) when is_binary(Name1), is_binary(Name2) ->
    case pathtype(Name2) of
        relative -> join_bytes(Name1, Name2, [], family());
        _ -> join_bytes(Name2, <<>>, [], family())
    end;
join(Name1, Name2) when is_atom(Name1) ->
    join(atom_to_list(Name1), Name2);
join(Name1, Name2) when is_atom(Name2) ->
    join(Name1, atom_to_list(Name2)).

%% The joined characters reversed in Result; Next is the name joined after Name.
join_chars([U, $: | Rest], Next, [], win32) when is_integer(U), U >= $A, U =< $Z ->
    join_chars(Rest, Next, [$:, U + $a - $A], win32);
join_chars([$\\, $\\ | Rest], Next, [], win32) ->
    join_chars([$/, $/ | Rest], Next, [], win32);
join_chars([$/, $/ | Rest], Next, [], win32) ->
    join_chars(Rest, Next, [$/, $/], win32);
join_chars([$\\ | Rest], Next, Result, win32) ->
    join_chars([$/ | Rest], Next, Result, win32);
join_chars([$/ | Rest], Next, [$., $/ | Result], Family) ->
    join_chars(Rest, Next, [$/ | Result], Family);
join_chars([$/ | Rest], Next, [$/ | Result], Family) ->
    join_chars(Rest, Next, [$/ | Result], Family);
join_chars([], [], Result, Family) ->
    finish_join(Result, Family);
join_chars([], Next, [$: | Rest], win32) ->
    join_chars(Next, [], [$: | Rest], win32);
join_chars([], Next, [$/ | Result], Family) ->
    join_chars(Next, [], [$/ | Result], Family);
join_chars([], Next, [$., $/ | Result], Family) ->
    join_chars(Next, [], [$/ | Result], Family);
join_chars([], Next, Result, Family) ->
    join_chars(Next, [], [$/ | Result], Family);
join_chars([[_ | _] = List | Rest], Next, Result, Family) ->
    join_chars(List ++ Rest, Next, Result, Family);
join_chars([[] | Rest], Next, Result, Family) ->
    join_chars(Rest, Next, Result, Family);
join_chars([C | Rest], Next, Result, Family) when is_integer(C) ->
    join_chars(Rest, Next, [C | Result], Family);
join_chars([Atom | Rest], Next, Result, Family) when is_atom(Atom) ->
    join_chars(atom_to_list(Atom) ++ Rest, Next, Result, Family).

join_bytes(<<U, $:, Rest/binary>>, Next, [], win32) when is_integer(U), U >= $A, U =< $Z ->
    join_bytes(Rest, Next, [$:, U + $a - $A], win32);
join_bytes(<<$\\, $\\, Rest/binary>>, Next, [], win32) ->
    join_bytes(<<$/, $/, Rest/binary>>, Next, [], win32);
join_bytes(<<$/, $/, Rest/binary>>, Next, [], win32) ->
    join_bytes(Rest, Next, [$/, $/], win32);
join_bytes(<<$\\, Rest/binary>>, Next, Result, win32) ->
    join_bytes(<<$/, Rest/binary>>, Next, Result, win32);
join_bytes(<<$/, Rest/binary>>, Next, [$., $/ | Result], Family) ->
    join_bytes(Rest, Next, [$/ | Result], Family);
join_bytes(<<$/, Rest/binary>>, Next, [$/ | Result], Family) ->
    join_bytes(Rest, Next, [$/ | Result], Family);
join_bytes(<<>>, <<>>, Result, Family) ->
    list_to_binary(finish_join(Result, Family));
join_bytes(<<>>, Next, [$: | Rest], win32) ->
    join_bytes(Next, <<>>, [$: | Rest], win32);
join_bytes(<<>>, Next, [$/, $/ | Result], win32) ->
    join_bytes(Next, <<>>, [$/, $/ | Result], win32);
join_bytes(<<>>, Next, [$/ | Result], Family) ->
    join_bytes(Next, <<>>, [$/ | Result], Family);
join_bytes(<<>>, Next, [$., $/ | Result], Family) ->
    join_bytes(Next, <<>>, [$/ | Result], Family);
join_bytes(<<>>, Next, Result, Family) ->
    join_bytes(Next, <<>>, [$/ | Result], Family);
join_bytes(<<C, Rest/binary>>, Next, Result, Family) ->
    join_bytes(Rest, Next, [C | Result], Family).

%% The reversed result in order, without a trailing separator (a root keeps it).
finish_join([$/, $:, L], win32) -> [L, $:, $/];
finish_join([$/], _) -> [$/];
finish_join([$/, $/], win32) -> [$/, $/];
finish_join([$/ | Name], _) -> lists:reverse(Name);
finish_join(Name, _) -> lists:reverse(Name).

%% Whether a name is absolute, relative, or (Windows) relative to a volume.
pathtype(Atom) when is_atom(Atom) ->
    pathtype(atom_to_list(Atom));
pathtype(Name) when is_list(Name); is_binary(Name) ->
    case family() of
        win32 -> windows_pathtype(Name);
        _ -> unix_pathtype(Name)
    end.

unix_pathtype(<<$/, _/binary>>) -> absolute;
unix_pathtype([$/ | _]) -> absolute;
unix_pathtype([List | Rest]) when is_list(List) -> unix_pathtype(List ++ Rest);
unix_pathtype([Atom | Rest]) when is_atom(Atom) -> unix_pathtype(atom_to_list(Atom) ++ Rest);
unix_pathtype(_) -> relative.

windows_pathtype([List | Rest]) when is_list(List) -> windows_pathtype(List ++ Rest);
windows_pathtype([Atom | Rest]) when is_atom(Atom) -> windows_pathtype(atom_to_list(Atom) ++ Rest);
windows_pathtype([C, List | Rest]) when is_list(List) -> windows_pathtype([C | List ++ Rest]);
windows_pathtype(<<A, B, _/binary>>) when
    (A =:= $/ orelse A =:= $\\), (B =:= $/ orelse B =:= $\\)
->
    absolute;
windows_pathtype(<<A, _/binary>>) when A =:= $/; A =:= $\\ -> volumerelative;
windows_pathtype(<<_, $:, S, _/binary>>) when S =:= $/; S =:= $\\ -> absolute;
windows_pathtype(<<_, $:, _/binary>>) ->
    volumerelative;
windows_pathtype([A, B | _]) when (A =:= $/ orelse A =:= $\\), (B =:= $/ orelse B =:= $\\) ->
    absolute;
windows_pathtype([A | _]) when A =:= $/; A =:= $\\ -> volumerelative;
windows_pathtype([C1, C2, List | Rest]) when is_list(List) -> pathtype([C1, C2 | List ++ Rest]);
windows_pathtype([_, $:, S | _]) when S =:= $/; S =:= $\\ -> absolute;
windows_pathtype([_, $: | _]) ->
    volumerelative;
windows_pathtype(_) ->
    relative.

%% The name without the extension of its last component.
rootname(Name) when is_binary(Name) ->
    list_to_binary(rootname(binary_to_list(Name)));
rootname(Name0) ->
    root_chars(flatten(Name0), [], [], family()).

root_chars([$/ | Rest], Root, Ext, Family) ->
    root_chars(Rest, [$/] ++ Ext ++ Root, [], Family);
root_chars([$\\ | Rest], Root, Ext, win32) ->
    root_chars(Rest, [$/] ++ Ext ++ Root, [], win32);
root_chars([$. | Rest], [$/ | _] = Root, [], Family) ->
    root_chars(Rest, [$. | Root], [], Family);
root_chars([$. | Rest], Root, Ext, Family) ->
    root_chars(Rest, Ext ++ Root, ".", Family);
root_chars([C | Rest], Root, [], Family) when is_integer(C) ->
    root_chars(Rest, [C | Root], [], Family);
root_chars([C | Rest], Root, Ext, Family) when is_integer(C) ->
    root_chars(Rest, Root, [C | Ext], Family);
root_chars([], Root, _, _) ->
    lists:reverse(Root).

%% The name without Ext at its end.
rootname(Name, Ext) when is_binary(Name), is_binary(Ext) ->
    list_to_binary(rootname(binary_to_list(Name), binary_to_list(Ext)));
rootname(Name, Ext) when is_binary(Name) ->
    rootname(Name, to_binary(Ext));
rootname(Name, Ext) when is_binary(Ext) ->
    rootname(to_binary(Name), Ext);
rootname(Name0, Ext0) ->
    without_suffix(flatten(Name0), flatten(Ext0), [], family()).

without_suffix(Ext, Ext, [$/ | _] = Result, _) ->
    lists:reverse(Result, Ext);
without_suffix(Ext, Ext, [$\\ | _] = Result, win32) ->
    lists:reverse(Result, Ext);
without_suffix(Ext, Ext, Result, _) ->
    lists:reverse(Result);
without_suffix([], _, Result, _) ->
    lists:reverse(Result);
without_suffix([C | Rest], Ext, Result, Family) when is_integer(C) ->
    without_suffix(Rest, Ext, [C | Result], Family).

%% The components of a name, a root or drive first.
split(Name) when is_binary(Name) ->
    case family() of
        win32 -> windows_split_bytes(Name);
        _ -> unix_split_bytes(Name)
    end;
split(Name0) ->
    Name = flatten(Name0),
    case family() of
        win32 -> windows_split(Name);
        _ -> components(Name, [], unix)
    end.

unix_split_bytes(Name) ->
    Parts =
        case binary:split(Name, [<<"/">>], [global]) of
            [<<>> | Rest] when Rest =/= [] -> [<<"/">> | Rest];
            All -> All
        end,
    [Part || Part <- Parts, Part =/= <<>>].

lower_drive(L) when L >= $A, L =< $Z -> L + $a - $A;
lower_drive(L) -> L.

nonempty_parts(Bin) ->
    [Part || Part <- binary:split(Bin, [<<"/">>, <<"\\">>], [global]), Part =/= <<>>].

windows_split_bytes(<<L, $:, S, Rest/binary>>) when
    (S =:= $\\ orelse S =:= $/) andalso ?IS_LETTER(L)
->
    [<<(lower_drive(L)), $:, $/>> | nonempty_parts(Rest)];
windows_split_bytes(<<L, $:, Rest/binary>>) when ?IS_LETTER(L) ->
    [<<(lower_drive(L)), $:>> | nonempty_parts(Rest)];
windows_split_bytes(<<S, S, Rest/binary>>) when S =:= $\\; S =:= $/ ->
    [<<"//">> | nonempty_parts(Rest)];
windows_split_bytes(<<S, Rest/binary>>) when S =:= $\\; S =:= $/ ->
    [<<$/>> | nonempty_parts(Rest)];
windows_split_bytes(Name) ->
    nonempty_parts(Name).

windows_split([S, S | Rest]) when S =:= $\\; S =:= $/ -> components(Rest, [[$/, $/]], win32);
windows_split([$\\ | Rest]) ->
    windows_split([$/ | Rest]);
windows_split([X, $\\ | Rest]) when is_integer(X) -> windows_split([X, $/ | Rest]);
windows_split([X, Y, $\\ | Rest]) when is_integer(X), is_integer(Y) ->
    windows_split([X, Y, $/ | Rest]);
windows_split([U, $: | Rest]) when is_integer(U), $A =< U, U =< $Z ->
    windows_split([U + $a - $A, $: | Rest]);
windows_split([L, $:, $/ | Rest]) ->
    components(Rest, [], [[L, $:, $/]], win32);
windows_split([L, $: | Rest]) ->
    components(Rest, [], [[L, $:]], win32);
windows_split(Name) ->
    components(Name, [], win32).

components([$/ | Rest], Done, Family) -> components(Rest, [], [[$/] | Done], Family);
components([$\\ | Rest], Done, win32) -> components(Rest, [], [[$/] | Done], win32);
components(Name, Done, Family) -> components(Name, [], Done, Family).

components([$\\ | Rest], Part, Done, win32) ->
    components([$/ | Rest], Part, Done, win32);
components([$/ | Rest], [], Done, Family) ->
    components(Rest, [], Done, Family);
components([$/ | Rest], Part, Done, Family) ->
    components(Rest, [], [lists:reverse(Part) | Done], Family);
components([C | Rest], Part, Done, Family) when is_integer(C) ->
    components(Rest, [C | Part], Done, Family);
components([], [], Done, _) ->
    lists:reverse(Done);
components([], Part, Done, Family) ->
    components([], [], [lists:reverse(Part) | Done], Family).

%% The joined name in the host's native form: backslashes on Windows.
nativename(Name0) ->
    Name = join([Name0]),
    case family() of
        win32 when is_binary(Name) -> binary:replace(Name, <<"/">>, <<"\\">>, [global]);
        win32 -> [native_char(C) || C <- Name];
        _ -> Name
    end.

native_char($/) -> $\\;
native_char(C) -> C.

%% A standard directory of an application: user_cache, user_config, user_data, user_log, site_config or site_data,
%% by the XDG rules on Linux, the macOS and Windows conventions elsewhere.
basedir(Type, App) when is_atom(Type), is_list(App) orelse is_binary(App) ->
    basedir(Type, App, #{}).

basedir(Type, App, Opts) when is_atom(Type), is_map(Opts), is_list(App) orelse is_binary(App) ->
    Os = basedir_os(Opts),
    Name = basedir_name(Os, App, Opts),
    Base = basedir_base(Type, Os),
    case {Type, Os} of
        {user_log, linux} -> filename:join([Base, Name, "log"]);
        {user_log, windows} -> filename:join([Base, Name, "Logs"]);
        {user_cache, windows} -> filename:join([Base, Name, "Cache"]);
        _ when Type =:= site_config; Type =:= site_data -> [filename:join([B, Name]) || B <- Base];
        _ -> filename:join([Base, Name])
    end.

basedir_os(#{os := Os}) when Os =:= linux; Os =:= windows; Os =:= darwin ->
    Os;
basedir_os(#{}) ->
    case os:type() of
        {unix, darwin} -> darwin;
        {win32, _} -> windows;
        _ -> linux
    end.

basedir_name(windows, App, #{author := Author, version := Version}) ->
    filename:join([Author, App, Version]);
basedir_name(windows, App, #{author := Author}) ->
    filename:join([Author, App]);
basedir_name(_, App, #{version := Version}) ->
    filename:join([App, Version]);
basedir_name(_, App, _) ->
    App.

basedir_base(Type, linux) ->
    case Type of
        user_data -> env_or("XDG_DATA_HOME", home(".local/share"));
        user_config -> env_or("XDG_CONFIG_HOME", home(".config"));
        user_cache -> env_or("XDG_CACHE_HOME", home(".cache"));
        user_log -> env_or("XDG_CACHE_HOME", home(".cache"));
        site_data -> lexemes(env_or("XDG_DATA_DIRS", "/usr/local/share/:/usr/share/"));
        site_config -> lexemes(env_or("XDG_CONFIG_DIRS", "/etc/xdg"))
    end;
basedir_base(Type, darwin) ->
    case Type of
        user_data -> home("Library/Application Support");
        user_config -> home("Library/Application Support");
        user_cache -> home("Library/Caches");
        user_log -> home("Library/Logs");
        site_data -> ["/Library/Application Support"];
        site_config -> ["/Library/Application Support"]
    end;
basedir_base(Type, windows) ->
    case os:getenv("APPDATA") of
        Missing when Missing =:= false; Missing =:= [] ->
            case Type of
                user_data -> home("Local");
                user_config -> home("Roaming");
                user_cache -> home("Local");
                user_log -> home("Local");
                site_data -> [];
                site_config -> []
            end;
        AppData ->
            case Type of
                user_data -> env_or("LOCALAPPDATA", AppData);
                user_config -> AppData;
                user_cache -> env_or("LOCALAPPDATA", AppData);
                user_log -> env_or("LOCALAPPDATA", AppData);
                site_data -> [];
                site_config -> []
            end
    end.

%% The non-empty pieces of a colon-separated directory list, as string:lexemes(List, ":") gives them.
lexemes(List) -> [Piece || Piece <- string_pieces(List, [], []), Piece =/= []].

string_pieces([$: | Rest], Piece, Pieces) ->
    string_pieces(Rest, [], [lists:reverse(Piece) | Pieces]);
string_pieces([C | Rest], Piece, Pieces) ->
    string_pieces(Rest, [C | Piece], Pieces);
string_pieces([], Piece, Pieces) ->
    lists:reverse([lists:reverse(Piece) | Pieces]).

%% An environment variable's value, or Default when it is unset or empty.
env_or(Name, Default) ->
    case os:getenv(Name) of
        [] -> Default;
        false -> Default;
        Value -> Value
    end.

%% Dir under the home directory: $HOME, else the home argument.
home(Dir) ->
    case os:getenv("HOME") of
        false ->
            {ok, [[Home]]} = init:get_argument(home),
            filename:join(Home, Dir);
        Home ->
            filename:join(Home, Dir)
    end.

%% Whether a name is valid for the file system: no NUL, and characters the native name encoding can hold.
validate(Name) when is_binary(Name) ->
    try
        true = valid_bytes(Name, 0) > 0
    catch
        _:_ -> false
    end;
validate(Name) when is_list(Name); is_atom(Name) ->
    Encoding = file:native_name_encoding(),
    Os = os:type(),
    try
        true = valid_chars(Name, Encoding, Os, 0) > 0
    catch
        _:_ -> false
    end.

valid_bytes(<<>>, Count) -> Count;
valid_bytes(<<0, _/binary>>, _) -> throw(invalid);
valid_bytes(<<_, Rest/binary>>, Count) -> valid_bytes(Rest, Count + 1).

valid_chars([], _, _, Count) ->
    Count;
valid_chars(C, Encoding, Os, Count) when is_integer(C) ->
    valid_char(C, Encoding, Os),
    Count + 1;
valid_chars(A, Encoding, Os, Count) when is_atom(A) ->
    valid_chars(atom_to_list(A), Encoding, Os, Count);
valid_chars([H | T], Encoding, Os, Count) ->
    valid_chars(T, Encoding, Os, valid_chars(H, Encoding, Os, Count)).

valid_char(C, _, _) when C < 1 -> throw(invalid);
valid_char(C, latin1, _) when C > 255 -> throw(invalid);
valid_char(C, utf8, _) when C >= 16#110000 -> throw(invalid);
valid_char(C, utf8, {win32, _}) when C > 16#ffff -> throw(invalid);
valid_char(_, utf8, {win32, _}) -> ok;
valid_char(C, utf8, _) when 16#D800 =< C, C =< 16#DFFF -> throw(invalid);
valid_char(_, _, _) -> ok.
