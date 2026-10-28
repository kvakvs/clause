%% clause_ssl_engine: the TLS engine of ssl connections over OpenSSL's libssl (plan 21 step LIB81B,
%% runtime/src/builtins/ssl_engine/), in the ssl runtime archive a program links when it compiles this module
%% (docs/builtins.md#ssl-engine). An engine runs the record layer and handshake of one connection over memory buffers:
%% each step takes the bytes received from the transport and returns the bytes to send to it. Each catalog builtin
%% replaces its clause_builtin_placeholder body.
-module(clause_ssl_engine).
-export([
    context/2,
    connection/2,
    handshake/2,
    verify/2,
    configure/2,
    read/2,
    write/2,
    shutdown/1,
    key_update/2,
    info/1,
    peer_chain/1
]).

context(_Role, _Options) -> clause_builtin_placeholder.
connection(_Context, _Options) -> clause_builtin_placeholder.
handshake(_Engine, _Received) -> clause_builtin_placeholder.
verify(_Engine, _Decision) -> clause_builtin_placeholder.
configure(_Engine, _Context) -> clause_builtin_placeholder.
read(_Engine, _Received) -> clause_builtin_placeholder.
write(_Engine, _Data) -> clause_builtin_placeholder.
shutdown(_Engine) -> clause_builtin_placeholder.
key_update(_Engine, _Type) -> clause_builtin_placeholder.
info(_Engine) -> clause_builtin_placeholder.
peer_chain(_Engine) -> clause_builtin_placeholder.
