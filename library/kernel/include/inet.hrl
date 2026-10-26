%% Clause's kernel/include/inet.hrl (plan 21 step LIB75, docs/libraries/library.md): the #hostent{} record of inet's
%% documentation, the result of inet:gethostbyname/1,2 and inet:gethostbyaddr/1, written for Clause.
-record(hostent, {
    % Official name of the host.
    h_name :: inet:hostname(),
    % Other names of the host.
    h_aliases = [] :: [inet:hostname()],
    % Address family of h_addr_list.
    h_addrtype :: inet | inet6,
    % Length of each address in bytes (4 or 16).
    h_length :: non_neg_integer(),
    % The host's addresses.
    h_addr_list = [] :: [inet:ip_address()]
}).
