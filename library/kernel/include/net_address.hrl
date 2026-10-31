%% Clause's net_address.hrl (plan 30 step DIST4b1a, docs/distribution.md#carriers): the address a distribution carrier
%% listens on, with OTP's record name and fields, since carriers make and read it.
-record(net_address, {
    % The carrier's own address, {IP, Port} for TCP.
    address,
    % The host name part of node names on this address.
    host,
    % The carrier's protocol, such as tcp.
    protocol,
    % The address family, such as inet or inet6.
    family
}).
