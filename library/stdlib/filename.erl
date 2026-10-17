%% Clause's filename (plan 21 steps LIB5C and LIB5G, docs/builtins.md#filename): the name walks are runtime builtins
%% with clause_builtin_placeholder bodies, following OTP's Unix or Windows rules for the host. absname/1,2 (the
%% current directories) and basedir/2,3 (the environment) are Erlang over them.
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

%% A list name as a binary in the native name encoding; badarg when it cannot be encoded.
to_binary(List) ->
    case
        unicode:characters_to_binary(filename:flatten(List), unicode, file:native_name_encoding())
    of
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
    case filename:pathtype(Name) of
        relative -> filename:absname_join(AbsBase, Name);
        absolute -> filename:join([filename:flatten(Name)]);
        volumerelative -> volume_absname(filename:split(Name), filename:split(AbsBase), AbsBase)
    end.

%% A volume-relative Windows name made absolute: a leading slash takes AbsBase's volume, a drive its own current
%% directory (the drive's root when unknown).
volume_absname([<<"/">> | Rest], [Volume | _], _) ->
    filename:join([Volume | Rest]);
volume_absname([<<X, $:>> | Rest], [<<X, _/binary>> | _], AbsBase) ->
    absname(filename:join(Rest), AbsBase);
volume_absname([<<X, $:>> | Rest], _, _) ->
    Cwd =
        case file:get_cwd([X, $:]) of
            {ok, Dir} -> to_binary(Dir);
            {error, _} -> <<X, $:, $/>>
        end,
    absname(filename:join(Rest), Cwd);
volume_absname(["/" | Rest], [Volume | _], _) ->
    filename:join([Volume | Rest]);
volume_absname([[X, $:] | Rest], [[X | _] | _], AbsBase) ->
    absname(filename:join(Rest), AbsBase);
volume_absname([[X, $:] | Rest], _, _) ->
    Cwd =
        case file:get_cwd([X, $:]) of
            {ok, Dir} -> Dir;
            {error, _} -> [X, $:, $/]
        end,
    absname(filename:join(Rest), Cwd).

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

absname_join(_, _) -> clause_builtin_placeholder.
append(_, _) -> clause_builtin_placeholder.
basename(_) -> clause_builtin_placeholder.
basename(_, _) -> clause_builtin_placeholder.
dirname(_) -> clause_builtin_placeholder.
extension(_) -> clause_builtin_placeholder.
flatten(_) -> clause_builtin_placeholder.
join(_) -> clause_builtin_placeholder.
join(_, _) -> clause_builtin_placeholder.
nativename(_) -> clause_builtin_placeholder.
pathtype(_) -> clause_builtin_placeholder.
rootname(_) -> clause_builtin_placeholder.
rootname(_, _) -> clause_builtin_placeholder.
split(_) -> clause_builtin_placeholder.
validate(_) -> clause_builtin_placeholder.
