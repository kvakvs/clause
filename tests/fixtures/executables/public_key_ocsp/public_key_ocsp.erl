%% public_key:pkix_ocsp_validate/5 and ocsp_extensions/1 (plan 21 step LIB80D4): OCSP responses checked for a
%% certificate: good, revoked and unknown statuses, several single responses, SHA-256 certificate IDs, the CA, a
%% designated responder by name and by key, unauthorised and expired responders with is_trusted_responder_fun,
%% nonces, tampered signatures, response status and type errors, and re-signed responses with stale times. The CA,
%% certificates and responses were made once with OpenSSL (valid until 2036).
-module(public_key_ocsp).
-export([main/1]).

-include_lib("public_key/include/public_key.hrl").

main(_) ->
    D = fun(Name) -> base64:decode(proplists:get_value(Name, data())) end,
    Ca = public_key:pkix_decode_cert(D(ca), otp),
    Other = public_key:pkix_decode_cert(D(other), otp),
    Leaf = public_key:pkix_decode_cert(D(leaf), otp),
    Revoked = public_key:pkix_decode_cert(D(revoked), otp),
    Unknown = public_key:pkix_decode_cert(D(unknown), otp),
    Trusted = [{is_trusted_responder_fun, fun(_) -> true end}],
    Nonce = nonce(D(nonce)),
    Cases = [
        {good, Leaf, Ca, good, undefined, []},
        {good_der, D(leaf), D(ca), good, undefined, []},
        {good_nonce_missing, Leaf, Ca, good, <<4, 2, 1, 2>>, []},
        {nonce, Leaf, Ca, nonce, Nonce, []},
        {nonce_unchecked, Leaf, Ca, nonce, undefined, []},
        {nonce_mismatch, Leaf, Ca, nonce, <<4, 2, 1, 2>>, []},
        {revoked, Revoked, Ca, revoked_resp, undefined, []},
        {unknown, Unknown, Ca, unknown_resp, undefined, []},
        {multi_revoked, Revoked, Ca, multi, undefined, []},
        {multi_leaf, Leaf, Ca, multi, undefined, []},
        {sha256, Leaf, Ca, sha256, undefined, []},
        {designated, Leaf, Ca, designated, undefined, []},
        {by_key, Leaf, Ca, by_key, undefined, []},
        {noeku, Leaf, Ca, noeku, undefined, []},
        {noeku_trusted, Leaf, Ca, noeku, undefined, Trusted},
        {other, Leaf, Ca, other_resp, undefined, []},
        {other_trusted, Leaf, Ca, other_resp, undefined, Trusted},
        {expired_trusted, Leaf, Ca, expired, undefined, Trusted},
        {no_certs, Leaf, Ca, no_certs, undefined, []},
        {not_in_response, Unknown, Ca, good, undefined, []},
        {wrong_issuer, Leaf, Other, good, undefined, []}
    ],
    [
        io:format("~p ~p~n", [Name, public_key:pkix_ocsp_validate(Cert, Issuer, D(Resp), NonceExt, Options)])
     || {Name, Cert, Issuer, Resp, NonceExt, Options} <- Cases
    ],
    Self = self(),
    Recording = fun(#cert{otp = Responder}) ->
        Self ! {trusted, subject_cn(Responder)},
        false
    end,
    public_key:pkix_ocsp_validate(Leaf, Ca, D(noeku), undefined, [{is_trusted_responder_fun, Recording}]),
    io:format("trusted fun ~p~n", [
        receive
            Message -> Message
        after 0 -> none
        end
    ]),
    Validate = fun(Der) -> public_key:pkix_ocsp_validate(Leaf, Ca, Der, undefined, []) end,
    io:format("tampered ~p~n", [Validate(tamper(D(no_certs)))]),
    io:format("try_later ~p~n", [
        Validate(public_key:der_encode('OCSPResponse', #'OCSPResponse'{responseStatus = tryLater}))
    ]),
    Bytes = #'ResponseBytes'{responseType = {1, 2, 3, 4}, response = <<4, 0>>},
    io:format("other type ~p~n", [
        Validate(
            public_key:der_encode('OCSPResponse', #'OCSPResponse'{responseStatus = successful, responseBytes = Bytes})
        )
    ]),
    io:format("too large ~p~n", [Validate(<<0:(102401 * 8)>>)]),
    Key = public_key:der_decode('RSAPrivateKey', D(ca_key)),
    Resign = fun(Change) -> resign(D(no_certs), Change, Key) end,
    io:format("resigned ~p~n", [Validate(Resign(fun(RD) -> RD end))]),
    io:format("produced later ~p~n", [
        Validate(Resign(fun(RD) -> RD#'ResponseData'{producedAt = "20991231000000Z"} end))
    ]),
    io:format("this update later ~p~n", [
        Validate(Resign(single(fun(S) -> S#'SingleResponse'{thisUpdate = "20991231000000Z"} end)))
    ]),
    io:format("next update passed ~p~n", [
        Validate(Resign(single(fun(S) -> S#'SingleResponse'{nextUpdate = "20200101000000Z"} end)))
    ]),
    io:format("no next update ~p~n", [
        Validate(Resign(single(fun(S) -> S#'SingleResponse'{nextUpdate = asn1_NOVALUE} end)))
    ]),
    io:format("ocsp_extensions ~p ~p~n", [
        public_key:ocsp_extensions(undefined), public_key:ocsp_extensions(<<4, 2, 1, 2>>)
    ]),
    io:format("public_key_ocsp: ok~n").

%% The nonce extension's value in a response, as the request sent it.
nonce(Der) ->
    #'OCSPResponse'{responseBytes = #'ResponseBytes'{response = Basic}} = public_key:der_decode('OCSPResponse', Der),
    #'BasicOCSPResponse'{tbsResponseData = #'ResponseData'{responseExtensions = [Extension]}} =
        public_key:der_decode('BasicOCSPResponse', Basic),
    io:format("nonce extension ~p~n", [Extension]),
    element(4, Extension).

subject_cn(#'OTPCertificate'{tbsCertificate = #'OTPTBSCertificate'{subject = {rdnSequence, RDNs}}}) ->
    hd([Value || [#'AttributeTypeAndValue'{type = ?'id-at-commonName', value = Value}] <- RDNs]).

%% A response with the last byte of its signature changed.
tamper(Der) ->
    Size = byte_size(Der) - 1,
    <<Head:Size/binary, Last>> = Der,
    <<Head/binary, (Last bxor 1)>>.

%% A change to the response data's first single response.
single(Change) ->
    fun(#'ResponseData'{responses = [Single | Rest]} = RD) ->
        RD#'ResponseData'{responses = [Change(Single) | Rest]}
    end.

%% The response with its data changed and signed again with the CA's key.
resign(Der, Change, Key) ->
    #'OCSPResponse'{responseBytes = Bytes} = Response = public_key:der_decode('OCSPResponse', Der),
    Basic = public_key:der_decode('BasicOCSPResponse', Bytes#'ResponseBytes'.response),
    ResponseData = Change(Basic#'BasicOCSPResponse'.tbsResponseData),
    Signature = public_key:sign(public_key:der_encode('ResponseData', ResponseData), sha256, Key),
    NewBasic = Basic#'BasicOCSPResponse'{tbsResponseData = ResponseData, signature = Signature},
    NewBytes = Bytes#'ResponseBytes'{response = public_key:der_encode('BasicOCSPResponse', NewBasic)},
    public_key:der_encode('OCSPResponse', Response#'OCSPResponse'{responseBytes = NewBytes}).

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
        {ca_key, <<
            "MIIEowIBAAKCAQEAz5xJmzbXxyv7pIqXof6wKCU59LxJID8ETwXU5qip3Up6QnEK9M6ZXNUh0lDsKAMhPnGzD9hIQ8bWlImO54Un"
            "QMYrPrFmBVJ/1poyk0qVKxjlrjy5ChISWKXORWl4fSflcw7obHfL57Y7MtugplJdelSJD2sIeCYhKgAOqC7ZDCA+LHiEpetYGMAK"
            "wTf3ed4eiO7gNC+VktAX0DA3S6AZb98xAPkwwZcraQANbv1vXMaM+JrsKgSnBLzUf02ecYoQAlwfUEN4GJSRGIKazkrSByf83Zof"
            "sympnAthPrN5Xw2FSrM8teJHHRijA2GDfiGy8UIjSDKLo48X5UlKzIdwpQIDAQABAoIBAAftcVgWb4Xg9ZJyYljit0pJJZ3lRKuR"
            "DHKSB++o2BelnOaFl4QCkZOcZda4lVPvzqiqQcqkmRnRcWcpm4XWrUmJZ6RUtM8WN+BzcePy8KA9KqQNGIm8Z+vvpOE8pketKtNK"
            "afCeYHBgIoGOJmPDeGNP8A3hKekgl5qZdvXQGCSg0ujeJkjgFZyYKtm7KWDTGgIo/2Jm1LHO85pVVmuEnuVZC4ABCyMJQnD6QxBQ"
            "FkTamWVpuFpqG+Fx6BeoccyAlTlSCq+T44f71/1T1h5AT4y+Rad9a8oW35JN6k24qR4WiEqgqxU6LpeM5ewf1fCbiyRmrXje7Eiv"
            "44qWUven+fECgYEA9xvG2nH60Jgajd9BPadNOl8anm1UqP6Nivp+jEVJ6VUNW2Xn114UHAhnkGnMsRCAEpZiiOgDeKLtAVJOaRbv"
            "n6e+yr+JqyoMQYYsggdmjkhuyvNFx5r0Cw4TsktZsf71RidBLwzFNwwixbc5tAFPELhqgrMMwug51ACmcrYV8l0CgYEA1xStdH56"
            "EeSdJ/xoD3+Nr5tT63+8exDK3vNdzyaOaMfp1rna3HmebVBCJRZ2mrRTV4zQu6EYHegZbJ2fdlGCyPQi4RwzePVZGmkTtgz5DSnm"
            "AwlETlL2fDcrVSueO4JMRv+/a4eIZDvF471sfzCPwa8rA0uBBJCOEXZ5qeb7oukCgYEArhPLSifxYPFGA5u7AgvRw8Uro1+xvDtW"
            "ywkcDQTixJU+C50GbhnAYN8q1fkyUaomDvISCBAqIdnkzIGG61FVDUvGhF3aRdIhQCedQxyjiqhMJHysM6ldMkyAY6HhVhsd/oW4"
            "zcRZSiYqeM5SwBQL9uNzZQ9tdgDSfB7ZIwZS/6ECgYAWgOJZIvaQsrPCwET/AiCH9B+Gr1nAIpSnWR1ClE0oJ2qO/ekpUkxCMbiJ"
            "+tyz+RQTP/eC+ir0zhkC5UcO8R5W+jxAA1xbVohZHA9k59yTBQQ84xZTf5isjHUrNyIaJdrnPtD/ZjHeEub0mhglAqa35CUC9j5s"
            "hNdoelXmcfV4KQKBgA+JbcGP39N5v830187HkdUKVW6Zxs0WjuzhgOl+7EgKIyh2mAlphDaNbei2JNwD8LCOW3j4TSWpcx7hNHvC"
            "UGzi7leFyE5P9Odo+7b7mbMCgpyZ+oj5MmSbarjjIzkTCS06hW04BFhvcy57qIsztaRZw+EHtPoQCapCc1WxYNIx"
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
        >>},
        {revoked, <<
            "MIICcTCCAVmgAwIBAgICEAIwDQYJKoZIhvcNAQELBQAwMzELMAkGA1UEBhMCU0UxEjAQBgNVBAoMCU9DU1AgVGVzdDEQMA4GA1UE"
            "AwwHT0NTUCBDQTAeFw0yNjEwMjYyMDIyMjBaFw0zNjEwMjMyMDIyMjBaMDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRl"
            "c3QxEDAOBgNVBAMMB3Jldm9rZWQwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQQ6F+lS4AE5p/vbnWBwqqnybCL+6mO05AdRFMi"
            "WyJhkFDTiymj3TYOv2Br25uSZzxpq5OIfCrX4d0vmfUKxzZYo1owWDAJBgNVHRMEAjAAMAsGA1UdDwQEAwIHgDAdBgNVHQ4EFgQU"
            "6Ftil/fMMrGbtnElFSLRa6p3dhswHwYDVR0jBBgwFoAUCqiaj/y/gwD5fYj5LM4bMJRcAV4wDQYJKoZIhvcNAQELBQADggEBAERZ"
            "G0AJ1aiuAyMfwiqGDugN4Ds76tTCdQ/ZUsC710M+oOFZ92kB27lnvVcg/VRGdnHHgT2wPYu+tJB3+EFMew2uKEgvc6PjqUkhFjKI"
            "73r6iPd9Ru5ixA3zqNmVE/h5LeD8Ar4Yk3lpSbtpL7yrrl/ZdVZPq/ubP7Wsl5MuPrJ0WDvdOlkeih80Ns4Baus/o4duasp+2QDX"
            "QQhBGPgnp/MghlTYFUHrbMKRzY1C+aDTRXe0NcpqZVQqJra2YHqhY10KhCEf8tXpyPSf9cRVXyU953fSKVeTRhIcp6FgF42WgmVA"
            "I3LIs5hhrbVc7PYho1YYBcARBuOdW4UiigONgz4="
        >>},
        {unknown, <<
            "MIICcTCCAVmgAwIBAgICEAMwDQYJKoZIhvcNAQELBQAwMzELMAkGA1UEBhMCU0UxEjAQBgNVBAoMCU9DU1AgVGVzdDEQMA4GA1UE"
            "AwwHT0NTUCBDQTAeFw0yNjEwMjYyMDIyMjBaFw0zNjEwMjMyMDIyMjBaMDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRl"
            "c3QxEDAOBgNVBAMMB3Vua25vd24wWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATHrAmhs9Yn9PnQrcOYB9dPOx2uZtI/P3PMn/aj"
            "tWBfUprld05AU+y38/LYoNMg4N5h5oLa8ErA+Ap584fVPD3go1owWDAJBgNVHRMEAjAAMAsGA1UdDwQEAwIHgDAdBgNVHQ4EFgQU"
            "gQMSMRLUXe/A787p2RL8h5G6jg0wHwYDVR0jBBgwFoAUCqiaj/y/gwD5fYj5LM4bMJRcAV4wDQYJKoZIhvcNAQELBQADggEBACWv"
            "j2FS73HlsiBxb3ApgNk4qtiLFovmZxPywZgWGEGkaQO0C8MqxNZqGY2/wdBm4Bq3ToXOFL9V+zwtpaUsA1nNYBQ7xv11Xbgtt+VD"
            "aBf3q+ruOxfMfzkiHtIbfFmgseJcAZwytZLNkVy8drh10zwmVPDDFV6xP0YPKJYsQllrA1ov9lDuVgAuQkCEU7JXhnA0X6XNpegk"
            "B4iJh5qyXSyVuCy4Tq00QGCULu1RE8MpAC1yv/oyW2+pMZxaBleGBUKZv3ujk5flL9d5AxZR53TCYSEx01R7AaZ6hH6h1w4lwWrx"
            "TM4Bm3IS/cd2OwOmh5A4rsT+Sn+REs4Ijff1ECc="
        >>},
        {good, <<
            "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"
        >>},
        {nonce, <<
            "MIICCQoBAKCCAgIwggH+BgkrBgEFBQcwAQEEggHvMIIB6zCB1KE1MDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "EDAOBgNVBAMMB09DU1AgQ0EYDzIwMjYxMDI2MjAyMjIxWjBlMGMwOzAJBgUrDgMCGgUABBRZPPFwxTaCd/fvYz9BFOSXWAkwAAQU"
            "Cqiaj/y/gwD5fYj5LM4bMJRcAV4CAhABgAAYDzIwMjYxMDI2MjAyMjIxWqARGA8yMDM2MTAyMzIwMjIyMVqhIzAhMB8GCSsGAQUF"
            "BzABAgQSBBDEZJ2cmQNx1BQkfTqa+1afMA0GCSqGSIb3DQEBCwUAA4IBAQBNsCk+vErHhXSMACGQftwYrVPbkqzKTzlEbHQwRL1V"
            "knKaznKk0h9fTvm/FIy2+pGcvWgm5h2U2M+7ZR92H1TX6SEkUzDoMtiH5f84X47uJYjvSrV2nFNyaue8tv+37cy54z0SGVZQaPYP"
            "b6x4EYX9+lXqsEbCe+rFVULLQwk+5TDm9dBDHEYJGDDuvsuMByhrZ6UWYEZ78dmWEFoO1oEswcvRGniFyxuSdNWmbaxLfA+3cgOn"
            "DxwUTcjZJdW9oDQ8wA/v2TbDcVYMTJlqtZaxZBhDr6tF4MsgD66CEDbsqQK9vSvaF/3xNuzKwpn0NZOURGH84GsOEJeU3/ttUYoa"
        >>},
        {revoked_resp, <<
            "MIIB+goBAKCCAfMwggHvBgkrBgEFBQcwAQEEggHgMIIB3DCBxaE1MDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "EDAOBgNVBAMMB09DU1AgQ0EYDzIwMjYxMDI2MjAyMjIxWjB7MHkwOzAJBgUrDgMCGgUABBRZPPFwxTaCd/fvYz9BFOSXWAkwAAQU"
            "Cqiaj/y/gwD5fYj5LM4bMJRcAV4CAhACoRYYDzIwMjYwMTAxMDAwMDAwWqADCgEBGA8yMDI2MTAyNjIwMjIyMVqgERgPMjAzNjEw"
            "MjMyMDIyMjFaMA0GCSqGSIb3DQEBCwUAA4IBAQCVn5+231M3/ht5HxVHUPxQB5YTCIeXnEJCUKU52gY+wvMR+tK3EOkUoydGBHP+"
            "HibG2hC36cpE3rqHXq3jcTxlacy0YEqUgI7lxS75sFaPxKuAxb+iSAwDJstFavDkAp84HFq3IUWHjC1O27qywfWAxGXc61uXozYF"
            "MymNvJO4A27QGy/TRqjY2xP6jdpKZEM3n9Q9iq7MxcC6itRqceBsO55tVoBE8oCI8uMMWE5X7gbUhvvXp0ktmiLmdZJI+DX15Pya"
            "Rw+SWzg7SsHIQIqVf/q32Cw/hStgq+59nLbvi+UwK4Jjmbg1Mxbty5+9DT0LSjgTV3e3j2LvyBY6GoSJ"
        >>},
        {unknown_resp, <<
            "MIIB5AoBAKCCAd0wggHZBgkrBgEFBQcwAQEEggHKMIIBxjCBr6E1MDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "EDAOBgNVBAMMB09DU1AgQ0EYDzIwMjYxMDI2MjAyMjIxWjBlMGMwOzAJBgUrDgMCGgUABBRZPPFwxTaCd/fvYz9BFOSXWAkwAAQU"
            "Cqiaj/y/gwD5fYj5LM4bMJRcAV4CAhADggAYDzIwMjYxMDI2MjAyMjIxWqARGA8yMDM2MTAyMzIwMjIyMVowDQYJKoZIhvcNAQEL"
            "BQADggEBAMtCw2j/sUwoKax8NfpEYMFumDj6kWrufdpAiLlPRn62DcyWIF8DEGNPjEWAyU15CaZmiIy2kdfEmOMnOEr40If2p3W7"
            "B6SCOpykKB/NZ4+J0m8Vl8Q2Reu4QoWNgh2EP63vDIrwfjaxDrO7gXYBQ8N7swmCSAUIEda+DlPNl8Qk3B2ZcpjUnbVaWqXw99uP"
            "dcMa++BCjzdSrZO/cIxCiShqkMx26gVyX3PyCkdgcMGCrSCQzR9+GQLuuyE5XKcyho0JbW2/tWARRguSkpMaQ+AzZsfVJag+i2wR"
            "cWJMkMdi+g+IykVfmJd2k8BlOq/Vxh+HcK+wG74CQzl05TcmG60="
        >>},
        {multi, <<
            "MIICYQoBAKCCAlowggJWBgkrBgEFBQcwAQEEggJHMIICQzCCASuhNTAzMQswCQYDVQQGEwJTRTESMBAGA1UECgwJT0NTUCBUZXN0"
            "MRAwDgYDVQQDDAdPQ1NQIENBGA8yMDI2MTAyNjIwMjIyMVowgeAweTA7MAkGBSsOAwIaBQAEFFk88XDFNoJ39+9jP0EU5JdYCTAA"
            "BBQKqJqP/L+DAPl9iPkszhswlFwBXgICEAKhFhgPMjAyNjAxMDEwMDAwMDBaoAMKAQEYDzIwMjYxMDI2MjAyMjIxWqARGA8yMDM2"
            "MTAyMzIwMjIyMVowYzA7MAkGBSsOAwIaBQAEFFk88XDFNoJ39+9jP0EU5JdYCTAABBQKqJqP/L+DAPl9iPkszhswlFwBXgICEAGA"
            "ABgPMjAyNjEwMjYyMDIyMjFaoBEYDzIwMzYxMDIzMjAyMjIxWjANBgkqhkiG9w0BAQsFAAOCAQEAK6ZeSF+GKkuhIokP3Qcx+f/c"
            "7cyad0hUsLS+3kjoaOyZm1yrZXbdHtcRwnvE+nCVdYZ5i5QGh3g4PV1ln3Ax8xrsUe7FACkz+uCSFLLtI3MDDSGsN5OUPkZLdTx+"
            "yUmy9wflsl1V8g5XQhYMM8u6Uevx4o3eRald+z6xkmsl4ODenuRW9HhPb+FdReIJIxgOT2fG3H4jalkSI/rymaW+QHT4Bgn8QuLl"
            "Q4uZTU459RgzAfDld1Kgpjuegdpo2hDjw5wjfL8V8E0ctxKTVuqy3nAvV5nn9J+t21sclsKwjoRn5qpOWihLRKslXGslJTja1NaK"
            "S42F6o7nXk/zX1NvHw=="
        >>},
        {sha256, <<
            "MIICAQoBAKCCAfowggH2BgkrBgEFBQcwAQEEggHnMIIB4zCBzKE1MDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "EDAOBgNVBAMMB09DU1AgQ0EYDzIwMjYxMDI2MjAyMjIxWjCBgTB/MFcwDQYJYIZIAWUDBAIBBQAEIJaf/WBBYnX8EsL11Ek0b8XR"
            "WQF83HlYM827jLAdmw1sBCDxUwn6oJi2VpHPHdxmS1/NrUR7Ut3bYeYbx9ym+gQkMwICEAGAABgPMjAyNjEwMjYyMDIyMjFaoBEY"
            "DzIwMzYxMDIzMjAyMjIxWjANBgkqhkiG9w0BAQsFAAOCAQEAe+sP1yEbvUGzLu9RzJW3UJn1J3I6KWCDrAb2WsvT6XMhAB8aSJvI"
            "Xz3uwJ0B7XSLDQS+4qDz+I5t1tc3B0YT3lg7CUK4SsEINrq2odSkjnZtxR6PUVRZkAtW2PlWI8jWUxFdC/gvoEJp6b6HNXqtPICj"
            "V6P/N7uTCEuaEVVNL9fqM9D8Jc7czItNxnyIhEj77ITD5Mpo6a/fBFv5trzE7Mvzyf1Ig/Fg0AqZyohdlxgBS3Ae7ictncSIjnap"
            "x4eVmUrEDIG7AFxIYPj1x7bPg0TKU+4FuCLIPioUQIzKdwkLFKEC+IXRJyiEs9B7CHtonbsU94HpcSPd6fVz6TSanQ=="
        >>},
        {designated, <<
            "MIIDvAoBAKCCA7UwggOxBgkrBgEFBQcwAQEEggOiMIIDnjCBsaE3MDUxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "EjAQBgNVBAMMCXJlc3BvbmRlchgPMjAyNjEwMjYyMDIyMjFaMGUwYzA7MAkGBSsOAwIaBQAEFFk88XDFNoJ39+9jP0EU5JdYCTAA"
            "BBQKqJqP/L+DAPl9iPkszhswlFwBXgICEAGAABgPMjAyNjEwMjYyMDIyMjFaoBEYDzIwMzYxMDIzMjAyMjIxWjAKBggqhkjOPQQD"
            "AgNIADBFAiBa3ouda1iJnw2QjeDtCkYa9/sXdDBJLUvQnRz2cOye0wIhAL2aJSSp1u/iJ26F4bFPWvJLmma0TStMixdUAo7RbKEA"
            "oIICkDCCAowwggKIMIIBcKADAgECAgIQBDANBgkqhkiG9w0BAQsFADAzMQswCQYDVQQGEwJTRTESMBAGA1UECgwJT0NTUCBUZXN0"
            "MRAwDgYDVQQDDAdPQ1NQIENBMB4XDTI2MTAyNjIwMjIyMFoXDTM2MTAyMzIwMjIyMFowNTELMAkGA1UEBhMCU0UxEjAQBgNVBAoM"
            "CU9DU1AgVGVzdDESMBAGA1UEAwwJcmVzcG9uZGVyMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAETXoTmP53pwaIy/w/Un0x2K7D"
            "YQEMGedvmOIV4J8HEIaTiGfUFov1xG6pmL0iaAC6iIusQ4NUUmKQME6bO/HkwaNvMG0wCQYDVR0TBAIwADALBgNVHQ8EBAMCB4Aw"
            "EwYDVR0lBAwwCgYIKwYBBQUHAwkwHQYDVR0OBBYEFGJ9dF6bTSK7ZiLJP8adAt68cTfpMB8GA1UdIwQYMBaAFAqomo/8v4MA+X2I"
            "+SzOGzCUXAFeMA0GCSqGSIb3DQEBCwUAA4IBAQAftj8lXpwmpyO/UGA4uk1YZZgFdzSoHV3XnfO0Fk6wYJmbb12f6GxNCXxQB0Cm"
            "mIA5JwGBhPNEMM08iDbfnQoeA7DLH48JTgM0W03Wz2y+v45hnMT9Yuzi4HI1nbf7i94QntVUyg2JfJeYb8arG1HrfRDljAUrxoQY"
            "zDBiNmnvUbV+GOWIYiX+MdnzMjTi0gNxK6OMcJrNgX3l7Wl213jucO6fqA4xoLRst31pbhMfqqGWG2Gdt3FL1yKmJhNwUam7lEyx"
            "yUl7q8dNXFI2YuYiysFOJsTnijXq0zzOsDukwviAxuG/TPOgNi1p7Hxi31qsbd1Z5TUUTjczpNQar4wn"
        >>},
        {by_key, <<
            "MIIDmgoBAKCCA5MwggOPBgkrBgEFBQcwAQEEggOAMIIDfDCBkKIWBBRifXRem00iu2YiyT/GnQLevHE36RgPMjAyNjEwMjYyMDIy"
            "MjFaMGUwYzA7MAkGBSsOAwIaBQAEFFk88XDFNoJ39+9jP0EU5JdYCTAABBQKqJqP/L+DAPl9iPkszhswlFwBXgICEAGAABgPMjAy"
            "NjEwMjYyMDIyMjFaoBEYDzIwMzYxMDIzMjAyMjIxWjAKBggqhkjOPQQDAgNHADBEAiA6M0GOIGvLDlyIHRWgf6Hm5l+ZFson2rQd"
            "SIp92Hly1wIgT58KhVx92/IdUGPq3nBXFFCtpqionBhLK3QwMQMOAbCgggKQMIICjDCCAogwggFwoAMCAQICAhAEMA0GCSqGSIb3"
            "DQEBCwUAMDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3QxEDAOBgNVBAMMB09DU1AgQ0EwHhcNMjYxMDI2MjAyMjIw"
            "WhcNMzYxMDIzMjAyMjIwWjA1MQswCQYDVQQGEwJTRTESMBAGA1UECgwJT0NTUCBUZXN0MRIwEAYDVQQDDAlyZXNwb25kZXIwWTAT"
            "BgcqhkjOPQIBBggqhkjOPQMBBwNCAARNehOY/nenBojL/D9SfTHYrsNhAQwZ52+Y4hXgnwcQhpOIZ9QWi/XEbqmYvSJoALqIi6xD"
            "g1RSYpAwTps78eTBo28wbTAJBgNVHRMEAjAAMAsGA1UdDwQEAwIHgDATBgNVHSUEDDAKBggrBgEFBQcDCTAdBgNVHQ4EFgQUYn10"
            "XptNIrtmIsk/xp0C3rxxN+kwHwYDVR0jBBgwFoAUCqiaj/y/gwD5fYj5LM4bMJRcAV4wDQYJKoZIhvcNAQELBQADggEBAB+2PyVe"
            "nCanI79QYDi6TVhlmAV3NKgdXded87QWTrBgmZtvXZ/obE0JfFAHQKaYgDknAYGE80QwzTyINt+dCh4DsMsfjwlOAzRbTdbPbL6/"
            "jmGcxP1i7OLgcjWdt/uL3hCe1VTKDYl8l5hvxqsbUet9EOWMBSvGhBjMMGI2ae9RtX4Y5YhiJf4x2fMyNOLSA3Ero4xwms2BfeXt"
            "aXbXeO5w7p+oDjGgtGy3fWluEx+qoZYbYZ23cUvXIqYmE3BRqbuUTLHJSXurx01cUjZi5iLKwU4mxOeKNerTPM6wO6TC+IDG4b9M"
            "86A2LWnsfGLfWqxt3VnlNRRONzOk1BqvjCc="
        >>},
        {noeku, <<
            "MIIDnwoBAKCCA5gwggOUBgkrBgEFBQcwAQEEggOFMIIDgTCBraEzMDExCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "DjAMBgNVBAMMBW5vZWt1GA8yMDI2MTAyNjIwMjIyMVowZTBjMDswCQYFKw4DAhoFAAQUWTzxcMU2gnf372M/QRTkl1gJMAAEFAqo"
            "mo/8v4MA+X2I+SzOGzCUXAFeAgIQAYAAGA8yMDI2MTAyNjIwMjIyMVqgERgPMjAzNjEwMjMyMDIyMjFaMAoGCCqGSM49BAMCA0gA"
            "MEUCIQCUhjkeX9otziomf0LnsqDS8IoOJDcC9AWQPAA8g9z+iAIgLGHOTRpf0Ev0cZcUeQ/9VG+NodknH+1/5KURIE9g/m2gggJ3"
            "MIICczCCAm8wggFXoAMCAQICAhAFMA0GCSqGSIb3DQEBCwUAMDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3QxEDAO"
            "BgNVBAMMB09DU1AgQ0EwHhcNMjYxMDI2MjAyMjIwWhcNMzYxMDIzMjAyMjIwWjAxMQswCQYDVQQGEwJTRTESMBAGA1UECgwJT0NT"
            "UCBUZXN0MQ4wDAYDVQQDDAVub2VrdTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABDkHohgsb6Ap0TcmwoooecH5uPHYtl8nD4t7"
            "4PShyeSFZv80r6w7O3JXn7MD99VAN5Qn0d7yPyCFSBwGbhVRy42jWjBYMAkGA1UdEwQCMAAwCwYDVR0PBAQDAgeAMB0GA1UdDgQW"
            "BBRNtuBvh9fb1of0myUgy6poAhyWFTAfBgNVHSMEGDAWgBQKqJqP/L+DAPl9iPkszhswlFwBXjANBgkqhkiG9w0BAQsFAAOCAQEA"
            "os0iF7ObudueuffrfmMbCARBz5A5RASNfYOgPGTEP47QtID8bT/UYB2vgz8tUMvsx/dNTWSUANGiFNTjbQIBLgmzc+GmuoiA2si7"
            "QGvnTsAPWfg1Q5+hridImEYVc99FKmGGIe6YzPhepcWUbqmBlupfvBfSpBnuH0WeszGlKCxEIpiEoTK0IWqaTcN4ZWJWaQnFAhic"
            "exfpp3JDs8b/odhJkonyP9N1qpklFpNRXKR8CYMqXl3rhhEeGynA6vACIh70C7M7O1dip4kfdoNv6vsdWXerPlyKG5b7u/yCjSot"
            "06UhLICNWlMbm9IKj+5zH/WIBjZ5wEc0WzeI3bOSPg=="
        >>},
        {other_resp, <<
            "MIIC3woBAKCCAtgwggLUBgkrBgEFBQcwAQEEggLFMIICwTCBsKE2MDQxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "ETAPBgNVBAMMCE90aGVyIENBGA8yMDI2MTAyNjIwMjIyMlowZTBjMDswCQYFKw4DAhoFAAQUWTzxcMU2gnf372M/QRTkl1gJMAAE"
            "FAqomo/8v4MA+X2I+SzOGzCUXAFeAgIQAYAAGA8yMDI2MTAyNjIwMjIyMlqgERgPMjAzNjEwMjMyMDIyMjJaMAoGCCqGSM49BAMC"
            "A0gAMEUCIQDXdJzdbQmcSmc8DrqvBjDE+M5JiDZlFxp5mCcM4h6UpAIgEj1FBIsfbajk1nehG3HNqNMm0CQX/+MzT1l+4P1geBug"
            "ggG0MIIBsDCCAawwggFSoAMCAQICFHdK0RHpoWapJNu6OIqnHrlzlNX/MAoGCCqGSM49BAMCMDQxCzAJBgNVBAYTAlNFMRIwEAYD"
            "VQQKDAlPQ1NQIFRlc3QxETAPBgNVBAMMCE90aGVyIENBMB4XDTI2MTAyNjIwMjIxOVoXDTM2MTAyMzIwMjIxOVowNDELMAkGA1UE"
            "BhMCU0UxEjAQBgNVBAoMCU9DU1AgVGVzdDERMA8GA1UEAwwIT3RoZXIgQ0EwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASM9Mkq"
            "DarD9RrpTjAF0Yi2Ys8THpQ0EQkEry8XwwyFJSRQrnsWuwnQ3+seP58EG5WUPdByu3VCwRtBtP+Kwldwo0IwQDAPBgNVHRMBAf8E"
            "BTADAQH/MA4GA1UdDwEB/wQEAwIBBjAdBgNVHQ4EFgQUHAMjIjfAcIk9yoDebf2UpAESDMQwCgYIKoZIzj0EAwIDSAAwRQIgQIXJ"
            "rowBs/aW9ew1zeEI8P/7/d68ci81HbPpvhNKt1sCIQC+WkRoGoyAUhQE6Wi66AGS4UZCz46YuJWnSi9TxG2L1w=="
        >>},
        {expired, <<
            "MIIDtwoBAKCCA7AwggOsBgkrBgEFBQcwAQEEggOdMIIDmTCBr6E1MDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "EDAOBgNVBAMMB2V4cGlyZWQYDzIwMjYxMDI2MjAyMjIyWjBlMGMwOzAJBgUrDgMCGgUABBRZPPFwxTaCd/fvYz9BFOSXWAkwAAQU"
            "Cqiaj/y/gwD5fYj5LM4bMJRcAV4CAhABgAAYDzIwMjYxMDI2MjAyMjIyWqARGA8yMDM2MTAyMzIwMjIyMlowCgYIKoZIzj0EAwID"
            "RwAwRAIgWkUAAuu4o1o47IQ4meidvRM2XRtuNBNxsUbz8Wo1lNYCIDx+tQzyAyA5zEzJpfTzznYzk6mmGqZ3WTkcH11y40cjoIIC"
            "jjCCAoowggKGMIIBbqADAgECAgIQBjANBgkqhkiG9w0BAQsFADAzMQswCQYDVQQGEwJTRTESMBAGA1UECgwJT0NTUCBUZXN0MRAw"
            "DgYDVQQDDAdPQ1NQIENBMB4XDTIwMDEwMTAwMDAwMFoXDTIxMDEwMTAwMDAwMFowMzELMAkGA1UEBhMCU0UxEjAQBgNVBAoMCU9D"
            "U1AgVGVzdDEQMA4GA1UEAwwHZXhwaXJlZDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABEccFcikwrkvkzsc+OLW8mv+xBcsuEG2"
            "TXl6LVp4AVi29bpNaS6Sxxbs62gQZ3SVINpPwCpXzvu+2TWlbM+w+H6jbzBtMAkGA1UdEwQCMAAwCwYDVR0PBAQDAgeAMBMGA1Ud"
            "JQQMMAoGCCsGAQUFBwMJMB0GA1UdDgQWBBTmHtxHrqg218r7qaNu+GkpKNy9vTAfBgNVHSMEGDAWgBQKqJqP/L+DAPl9iPkszhsw"
            "lFwBXjANBgkqhkiG9w0BAQsFAAOCAQEAeei3gpmCsP9UNSr1MJ6V9lDODsZhQ96nB0GonKMqOq3UBxA/2UphHbj/QN/uy4cLp7ED"
            "cu+VuPq9ZuuuFTqPi959FXx1kpSUMihw6LMrBKb0UJnceohT5kku4qmq5WEAkm7no1ADsJ11IXnZjZG4ba1LFQ1n3QW1Z4Q1C7Rb"
            "VRhgRvCbiBuTLKJPKSpYAzN1g3vgQn5fwKchtqqZIFBv8M7HNYyeRd2Lqe9RQMltIdpwv24cKmTBWIhVI7eyl0ogWvp/ne98O8Ud"
            "XyQoEfUbkPbRkY2Jysb1bI1iWpbDkUxkW2q3BmM429pRTKQgxVRyROSc0T84sRtH8STMQWTS+A=="
        >>},
        {no_certs, <<
            "MIIB5AoBAKCCAd0wggHZBgkrBgEFBQcwAQEEggHKMIIBxjCBr6E1MDMxCzAJBgNVBAYTAlNFMRIwEAYDVQQKDAlPQ1NQIFRlc3Qx"
            "EDAOBgNVBAMMB09DU1AgQ0EYDzIwMjYxMDI2MjAyMjIyWjBlMGMwOzAJBgUrDgMCGgUABBRZPPFwxTaCd/fvYz9BFOSXWAkwAAQU"
            "Cqiaj/y/gwD5fYj5LM4bMJRcAV4CAhABgAAYDzIwMjYxMDI2MjAyMjIyWqARGA8yMDM2MTAyMzIwMjIyMlowDQYJKoZIhvcNAQEL"
            "BQADggEBAAasSrXMYzC/qAQ4nibO7i+WUUBd1yiDepOCI3/erWoTjLvzE1gYhAsWJahKTMZZR0JUnNiXeBSEibWrUiWWRMaC2ki+"
            "BZqxSq4EJlQaAD4dcdSUn2jaXIMfVaDXfahHv4585EbAMO5NLT9I8xIUnnFivgolYh/irPElzli2ldEX9aCAG/OoUht1i02nj+Xn"
            "W8LOwPVhIyBgJUBpjFfrzr+oAL+l1+t1ihP36JeUX5/boHlfRm1uINvyhBaDGKn+OFBkp5Nm537vmt+ta+R/PzpxxIVcyHPyYDWr"
            "bbDdBs0gMNXZLlaZR8hrAOKGNpMXZTXtwvj8DtFMu0bucmA026Q="
        >>}
    ].
