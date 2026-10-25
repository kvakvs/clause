-module(clause_net).
-export([
    gethostname/0,
    getaddrinfo/2,
    getnameinfo/4,
    getservbyname/2,
    getservbyport/2,
    inet_getifaddrs/0,
    getifaddrs/0,
    if_names/0
]).

% Clause's system name and interface lookups (plan 21 step LIB60, docs/builtins.md#network-names): runtime builtins
% in runtime/src/builtins/net/, which replace these bodies at registration. library/kernel/net.erl and inet.erl
% answer OTP's functions over them; text arguments are UTF-8 binaries or undefined.

gethostname() -> clause_builtin_placeholder.
getaddrinfo(_, _) -> clause_builtin_placeholder.
getnameinfo(_, _, _, _) -> clause_builtin_placeholder.
getservbyname(_, _) -> clause_builtin_placeholder.
getservbyport(_, _) -> clause_builtin_placeholder.
inet_getifaddrs() -> clause_builtin_placeholder.
getifaddrs() -> clause_builtin_placeholder.
if_names() -> clause_builtin_placeholder.
