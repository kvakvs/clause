%% OTP-PUB-KEY.hrl, a generated header public_key.hrl includes, named alone as hackney's hackney_ssl_certificate
%% does (plan 21 step MIM9b); PKCS-FRAME.hrl alone in pkcs_frame_header.
-module(pub_key_headers).
-export([run/0]).

-include_lib("public_key/include/OTP-PUB-KEY.hrl").

run() ->
    {record_info(fields, 'OTPTBSCertificate'), ?'id-sha256', pkcs_frame_header:run()}.
