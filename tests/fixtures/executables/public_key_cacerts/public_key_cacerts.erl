%% public_key:cacerts_get/0, cacerts_load/0,1 and cacerts_clear/0 (plan 21 step LIB80D6): CA certificates loaded from
%% a PEM bundle, a directory of .pem files and the cacerts_path setting, missing and empty files, the failure of
%% cacerts_get/0, and the operating system's store (only whether it has certificates, as it
%% differs per machine). Files are created in the working directory; the certificates were made with OpenSSL.
-module(public_key_cacerts).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    D = fun(Name) -> base64:decode(proplists:get_value(Name, data())) end,
    Pem = fun(Names) -> public_key:pem_encode([{'Certificate', D(Name), not_encrypted} || Name <- Names]) end,
    Bundle = "cacerts_bundle.pem",
    Empty = "cacerts_empty.pem",
    Dir = "cacerts.dir",
    cleanup(Bundle, Empty, Dir),
    ok = file:write_file(Bundle, Pem([ca, other])),
    ok = file:write_file(Empty, <<"no certificates here\n">>),
    ok = file:make_dir(Dir),
    ok = file:write_file(filename:join(Dir, "a.pem"), Pem([leaf])),
    ok = file:write_file(filename:join(Dir, "b.pem"), Pem([ca, other])),
    ok = file:write_file(filename:join(Dir, "c.txt"), Pem([leaf])),
    io:format("clear ~p~n", [public_key:cacerts_clear()]),
    io:format("bundle ~p ~p~n", [public_key:cacerts_load(Bundle), names(public_key:cacerts_get())]),
    io:format("clear ~p ~p~n", [public_key:cacerts_clear(), public_key:cacerts_clear()]),
    io:format("missing ~p~n", [public_key:cacerts_load("cacerts_missing.pem")]),
    io:format("empty ~p~n", [public_key:cacerts_load(Empty)]),
    io:format("directory ~p ~p~n", [public_key:cacerts_load(Dir), lists:sort(names(public_key:cacerts_get()))]),
    io:format("binary path ~p ~p~n", [public_key:cacerts_load(list_to_binary(Bundle)), names(public_key:cacerts_get())]),
    true = public_key:cacerts_clear(),
    ok = application:load(public_key),
    ok = application:set_env(public_key, cacerts_path, Bundle),
    io:format("cacerts_path ~p~n", [names(public_key:cacerts_get())]),
    true = public_key:cacerts_clear(),
    ok = application:set_env(public_key, cacerts_path, "cacerts_missing.pem"),
    io:format("failed ~p~n", [
        try public_key:cacerts_get() of
            Certs -> {unexpected, length(Certs)}
        catch
            error:Reason -> Reason
        end
    ]),
    ok = application:unset_env(public_key, cacerts_path),
    io:format("os ~p~n", [public_key:cacerts_load()]),
    io:format("os certificates ~p~n", [certificates(public_key:cacerts_get())]),
    true = public_key:cacerts_clear(),
    io:format("os get ~p~n", [certificates(public_key:cacerts_get())]),
    cleanup(Bundle, Empty, Dir),
    io:format("public_key_cacerts: ok~n").

%% The subject common names of loaded certificates, in their order.
names(Certs) ->
    [
        hd([Value || [#'AttributeTypeAndValue'{type = ?'id-at-commonName', value = Value}] <- RDNs])
     || #cert{otp = #'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{subject = {rdnSequence, RDNs}}}} <- Certs
    ].

%% Whether there are certificates and all are combined certificates whose DER decodes to them.
certificates(Certs) ->
    Certs =/= [] andalso
        lists:all(fun(#cert{der = Der, otp = Otp}) -> public_key:pkix_decode_cert(Der, otp) =:= Otp end, Certs).

cleanup(Bundle, Empty, Dir) ->
    _ = [
        file:delete(File)
     || File <- [Bundle, Empty | [filename:join(Dir, Name) || Name <- ["a.pem", "b.pem", "c.txt"]]]
    ],
    _ = file:del_dir(Dir),
    ok.

data() ->
    [
        {ca, <<
            "MIIDNjCCAh6gAwIBAgIUajntA5NNK5VTkqe8p32WQAX5y1QwDQYJKoZIhvcNAQELBQAwMzELMAkGA1UEBhMCU0UxEjAQBgNVBAoM"
            "CU9DU1AgVGVzdDEQMA4GA1UEAwwHT0NTUCBDQTAeFw0yNjEwMjYyMDIyMTlaFw0zNjEwMjMyMDIyMTlaMDMxCzAJBgNVBAYTAlNF"
            "MRIwEAYDVQQKDAlPQ1NQIFRlc3QxEDAOBgNVBAMMB09DU1AgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDPnEmb"
            "NtfHK/ukipeh/rAoJTn0vEkgPwRPBdTmqKndSnpCcQr0zplc1SHSUOwoAyE+cbMP2EhDxtaUiY7nhSdAxis+sWYFUn/WmjKTSpUr"
            "GOWuPLkKEhJYpc5FaXh9J+VzDuhsd8vntjsy26CmUl16VIkPawh4JiEqAA6oLtkMID4seISl61gYwArBN/d53h6I7uA0L5WS0BfQ"
            "MDdLoBlv3zEA+TDBlytpAA1u/W9cxoz4muwqBKcEvNR/TZ5xihACXB9QQ3gYlJEYgprOStIHJ/zdmh+zKamcC2E+s3lfDYVKszy1"
            "4kcdGKMDYYN+IbLxQiNIMoujjxflSUrMh3ClAgMBAAGjQjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgEGMB0GA1Ud"
            "DgQWBBQKqJqP/L+DAPl9iPkszhswlFwBXjANBgkqhkiG9w0BAQsFAAOCAQEAg11HazBBopD5hom18St+fHDCyHQzya43k+H9/zgA"
            "AKcwOkqs2hcmfMi+hfwXBsfsOn/hX9btwyK8DXYcSgfudfsQLvqPVRdxaRBnWeCBMzBY4aemzHfl4hnv5a8njBsmavkAi/81CnCi"
            "wOECMr6TclmN4mwvipXZwQ/u4Oh/hgMMYTZB+oU/HwS4hFpVy/IgcNFxPOkIc/NteL3TUg2j01a3RdYnORIauzmqpe4tG+XbCxTw"
            "gI+C4GA2iA0v++XwqwsIgQzoW7bFKpIBTNWRFd3yGLm5ycHpr2k6hnD2AF/XXIDLi5BELGF8s2SKSQnaQTmxZvrBSgGQTWfyb2QK"
            "mQ=="
        >>},
        {other, <<
            "MIIBrDCCAVKgAwIBAgIUd0rREemhZqkk27o4iqceuXOU1f8wCgYIKoZIzj0EAwIwNDELMAkGA1UEBhMCU0UxEjAQBgNVBAoMCU9D"
            "U1AgVGVzdDERMA8GA1UEAwwIT3RoZXIgQ0EwHhcNMjYxMDI2MjAyMjE5WhcNMzYxMDIzMjAyMjE5WjA0MQswCQYDVQQGEwJTRTES"
            "MBAGA1UECgwJT0NTUCBUZXN0MREwDwYDVQQDDAhPdGhlciBDQTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABIz0ySoNqsP1GulO"
            "MAXRiLZizxMelDQRCQSvLxfDDIUlJFCuexa7CdDf6x4/nwQblZQ90HK7dULBG0G0/4rCV3CjQjBAMA8GA1UdEwEB/wQFMAMBAf8w"
            "DgYDVR0PAQH/BAQDAgEGMB0GA1UdDgQWBBQcAyMiN8BwiT3KgN5t/ZSkARIMxDAKBggqhkjOPQQDAgNIADBFAiBAhcmujAGz9pb1"
            "7DXN4Qjw//v93rxyLzUds+m+E0q3WwIhAL5aRGgajIBSFATpaLroAZLhRkLPjpi4ladKL1PEbYvX"
        >>},
        {leaf, <<
            "MIICbjCCAVagAwIBAgICEAEwDQYJKoZIhvcNAQELBQAwMzELMAkGA1UEBhMCU0UxEjAQBgNVBAoMCU9DU1AgVGVzdDEQMA4GA1UE"
            "AwwHT0NTUCBDQTAeFw0yNjEwMjYyMDIyMjBaFw0zNjEwMjMyMDIyMjBaMDAxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRl"
            "c3QxDTALBgNVBAMMBGxlYWYwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASJ1s0WzKl67ZxDCnFNkPZHxbD0c7+9SYEqw98wwVeP"
            "yzuoSUZBVp+NsErxi1XfEKMyZHbDmZ8fBkF+ljtMlkDdo1owWDAJBgNVHRMEAjAAMAsGA1UdDwQEAwIHgDAdBgNVHQ4EFgQUdkAd"
            "vKS2K0NulqQm6YQVtJ4BLXIwHwYDVR0jBBgwFoAUCqiaj/y/gwD5fYj5LM4bMJRcAV4wDQYJKoZIhvcNAQELBQADggEBALvUvfX3"
            "7OIYINu7bS9O2c8+3/heYYYIZyAPskS1bKn5waHz7Jn4mpFoXUJ5/49r42TKeSqwFNzzc9xwb8HfmGqoyjqdpnb4PZCZ11BG4NlT"
            "3UFnaqyHklNEj2nZoO9M4WHPFxFUzIIayOJ2f+it/Fd7n3legqOCCuAQnR8jL5+bMZEmay7ZDn7TpUpEvQQezvrIsjUdq0OiuIPl"
            "VbMCQ9CzyE7BBAnoFgiX6hVBTvlH7MGsgkHLM39SPfO36awsc1/SX/5xUI1dG04RZjzykoK6X+IrAzT4uQuuPcEoCMNt3U+Ut1GH"
            "yq8GNL15WtmV1BAe9f6YDFKBRsOaFErvXnk="
        >>}
    ].
