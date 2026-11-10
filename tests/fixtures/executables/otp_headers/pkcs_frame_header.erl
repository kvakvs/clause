%% PKCS-FRAME.hrl, a generated header public_key.hrl includes, named alone (plan 21 step MIM9b).
-module(pkcs_frame_header).
-export([run/0]).

-include_lib("public_key/include/PKCS-FRAME.hrl").

run() ->
    record_info(fields, 'EncryptedPrivateKeyInfo').
