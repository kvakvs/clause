%% clause_public_key_cacerts: public_key:cacerts_get/0, cacerts_load/0,1 and cacerts_clear/0 (plan 21 step LIB80D6,
%% docs/libraries/public_key.md#trust-stores): the operating system's trusted CA certificates as OTP's
%% pubkey_os_cacerts loads them: OTP's bundle files per Unix, the macOS keychains through /usr/bin/security, and the
%% Windows ROOT store through clause_public_key:os_cacerts/0; cached in a persistent term.
-module(clause_public_key_cacerts).
-export([clear/0, format_error/2, get/0, load/0, load/1]).

-include("include/public_key.hrl").
-include_lib("kernel/include/file.hrl").
-include_lib("kernel/include/logger.hrl").

%% The persistent term the certificates are cached in, OTP's.
-define(KEY, pubkey_os_cacerts).

%% public_key:cacerts_get/0: the loaded certificates, loaded first (from cacerts_path if set) when there are none.
get() ->
    case persistent_term:get(?KEY, not_loaded) of
        not_loaded ->
            _ = application:load(public_key),
            Result =
                case application:get_env(public_key, cacerts_path) of
                    {ok, EnvVar} -> load([EnvVar]);
                    undefined -> load()
                end,
            case Result of
                ok ->
                    persistent_term:get(?KEY);
                {error, Reason} ->
                    erlang:error({failed_load_cacerts, conv_error_reason(Reason)}, none, [
                        {error_info, #{cause => Reason, module => ?MODULE}}
                    ])
            end;
        CaCerts ->
            CaCerts
    end.

%% public_key:cacerts_load/0: the operating system's certificates.
load() ->
    DefError = {error, no_cacerts_found},
    case os:type() of
        {unix, linux} -> load(linux_paths(), DefError);
        {unix, OS} when OS == openbsd; OS == freebsd; OS == dragonfly; OS == netbsd -> load(bsd_paths(), DefError);
        {unix, sunos} -> load(sunos_paths(), DefError);
        {win32, _} -> load_win32();
        {unix, darwin} -> load_darwin();
        Os -> {error, {enotsup, Os}}
    end.

%% public_key:cacerts_load/1: the certificates of the first of the paths (files, or directories of .pem files) that
%% has some.
load(Paths) ->
    load(Paths, {error, enoent}).

%% public_key:cacerts_clear/0: whether certificates were loaded.
clear() ->
    persistent_term:erase(?KEY).

load([Path | Paths], Error) ->
    Result =
        case dir_or_file(Path) of
            enoent -> Error;
            directory -> load_from_files(Path);
            file -> load_from_file(Path)
        end,
    case Result of
        ok -> ok;
        Err -> load(Paths, Err)
    end;
load([], Error) ->
    Error.

dir_or_file(Path) ->
    case file:read_file_info(Path) of
        {ok, #file_info{type = directory}} -> directory;
        {ok, #file_info{type = regular}} -> file;
        {ok, #file_info{}} -> link_type(Path);
        {error, _} -> enoent
    end.

link_type(Path) ->
    case filelib:is_dir(Path) of
        true -> directory;
        false -> file
    end.

load_from_file(Path) when is_list(Path); is_binary(Path) ->
    try
        {ok, Binary} = file:read_file(Path),
        ok = decode_result(Binary)
    catch
        _:_Reason -> {error, enoent}
    end.

%% The PEM certificates of a bundle, others ignored with a warning.
decode_result(Binary) ->
    try
        store(lists:foldl(fun pem_cert/2, [], public_key:pem_decode(Binary)))
    catch
        _:Reason -> {error, Reason}
    end.

pem_cert({'Certificate', Der, not_encrypted}, Acc) ->
    try
        [#cert{der = Der, otp = public_key:pkix_decode_cert(Der, otp)} | Acc]
    catch
        _:_ -> Acc
    end;
pem_cert(Wrong, Acc) ->
    ?LOG_WARNING("PUBKEY cacerts load: Ignored content of type: ~w", [element(1, Wrong)]),
    Acc.

load_from_files(Path) ->
    store(filelib:fold_files(Path, ".*\.pem", false, fun file_certs/2, [])).

file_certs(FileName, Acc) ->
    try
        {ok, Bin} = file:read_file(FileName),
        [
            #cert{der = Der, otp = public_key:pkix_decode_cert(Der, otp)}
         || {'Certificate', Der, not_encrypted} <- public_key:pem_decode(Bin)
        ] ++ Acc
    catch
        _:_ -> Acc
    end.

load_win32() ->
    store(lists:foldl(fun win32_cert/2, [], clause_public_key:os_cacerts())).

win32_cert({_Enc, Der}, Acc) ->
    try
        [#cert{der = Der, otp = public_key:pkix_decode_cert(Der, otp)} | Acc]
    catch
        _:_ -> Acc
    end.

%% The system roots and, if it exports, the system keychain.
load_darwin() ->
    case darwin_certs("/System/Library/Keychains/SystemRootCertificates.keychain") of
        {ok, Bin1} ->
            case darwin_certs("/Library/Keychains/System.keychain") of
                {ok, Bin2} ->
                    decode_result(<<Bin1/binary, Bin2/binary>>);
                Err ->
                    ?LOG_WARNING("Unable to load additional OS certificates from System.keychain : ~p~n", [Err]),
                    decode_result(Bin1)
            end;
        Err ->
            Err
    end.

darwin_certs(KeyChainFile) ->
    Args = ["export", "-t", "certs", "-f", "pemseq", "-k", KeyChainFile],
    try
        run_cmd("/usr/bin/security", Args)
    catch
        error:Reason -> {error, {eopnotsupp, Reason}}
    end.

store([]) -> {error, no_cacerts_found};
store(CaCerts) -> persistent_term:put(?KEY, CaCerts).

linux_paths() ->
    [
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        "/etc/ssl/cert.pem"
    ].

bsd_paths() ->
    [
        "/etc/ssl/cert.pem",
        "/etc/openssl/certs/cacert.pem",
        "/etc/openssl/certs/ca-certificates.crt",
        "/usr/local/share/certs/ca-root-nss.crt"
    ].

sunos_paths() ->
    ["/etc/certs/CA/", "/etc/ssl/cacert.pem"].

%% A command's output when it exits with status 0.
run_cmd(Cmd, Args) ->
    Port = open_port({spawn_executable, Cmd}, [{args, Args}, binary, exit_status, stderr_to_stdout]),
    unlink(Port),
    cmd_data(Port, <<>>).

cmd_data(Port, Acc) ->
    receive
        {Port, {data, Bin}} -> cmd_data(Port, <<Acc/binary, Bin/binary>>);
        {Port, {exit_status, 0}} -> {ok, Acc};
        {Port, {exit_status, Status}} -> {error, {eopnotsupp, Status, Acc}}
    end.

conv_error_reason(enoent) -> enoent;
conv_error_reason(no_cacerts_found) -> no_cacerts_found;
conv_error_reason({enotsup, _OS}) -> enotsup;
conv_error_reason({eopnotsupp, _Reason}) -> eopnotsupp;
conv_error_reason({eopnotsupp, _Status, _Acc}) -> eopnotsupp.

%% The extended error of a failed cacerts_get/0 (erl_error).
format_error(Reason, [{_M, _F, _As, Info} | _]) ->
    ErrorInfoMap = proplists:get_value(error_info, Info, #{}),
    Message =
        case maps:get(cause, ErrorInfoMap, none) of
            enoent -> "operating system CA bundle could not be located";
            no_cacerts_found -> "no CA certificates were found on the system";
            {enotsup, OS} -> io_lib:format("operating system ~p is not supported", [OS]);
            {eopnotsupp, SubReason} -> io_lib:format("operation failed because of ~p", [SubReason]);
            {eopnotsupp, Status, _Acc} -> io_lib:format("operation failed with status ~B", [Status])
        end,
    #{
        general => io_lib:format("Failed to load cacerts: ~s", [Message]),
        reason => io_lib:format("~p: ~p", [pubkey_os_cacerts, Reason])
    }.
