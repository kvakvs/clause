%% Clause's dist_util.hrl (plan 30 step DIST4b2a, docs/distribution.md#carriers): the #hs_data{} record a carrier fills
%% for dist_util's handshake, with OTP's record name and fields (alt_dist's #hs_data{} record), and OTP's macros.

% Handshake tracing, off as in OTP's default build.
-define(debug(Term), ok).
-define(trace(Fmt, Args), ok).
-define(trace_factor, 1).

% End the handshake or connection process: exit shutdown, or exit Reason.
-define(shutdown(Data), dist_util:shutdown(?MODULE, ?LINE, Data)).
-define(shutdown2(Data, Reason), dist_util:shutdown(?MODULE, ?LINE, Data, Reason)).

-record(hs_data, {
    % The net_kernel side that called setup/5 or accept_connection/5.
    kernel_pid,
    % The other node's name: given by setup/5, read from the peer on accept.
    other_node,
    % This node's name.
    this_node,
    % The distribution controller.
    socket,
    % The setup timer of dist_util:start_timer/1.
    timer,
    % The flags this node offers, set by dist_util.
    this_flags,
    % The nodes net_kernel allows, given by accept_connection/5.
    allowed,
    % The other node's protocol version from epmd.
    other_version,
    % The other node's flags, set by dist_util.
    other_flags,
    % Whether the other node started the connection, set by dist_util.
    other_started,
    % fun(Ctrl, Data) sending a handshake packet.
    f_send,
    % fun(Ctrl, Length, Timeout) receiving a handshake packet.
    f_recv,
    % fun(Ctrl) setting the controller's options before the connection is up, and after it.
    f_setopts_pre_nodeup,
    f_setopts_post_nodeup,
    % fun(Ctrl) giving {ok, Id} of the controller setnode/3 registers.
    f_getll,
    % fun(Ctrl, Node) giving the #net_address{} of the other node.
    f_address,
    % fun(Ctrl) sending a tick over an up connection.
    mf_tick,
    % fun(Ctrl) giving {ok, Received, Sent, PendingSend} of an up connection.
    mf_getstat,
    % normal or hidden, given by setup/5.
    request_type = normal,
    % fun(Ctrl, Options) setting and fun(Ctrl, Options) reading an up connection's options; optional.
    mf_setopts,
    mf_getopts,
    % fun(Ctrl, Node, DHandle) or fun(Ctrl, Node, DHandle, Context) once the connection is up; optional.
    f_handshake_complete,
    % Flags to add to the offer, to reject and to require of the other node; optional.
    add_flags,
    reject_flags,
    require_flags,
    % This node's and the other node's creations, set by dist_util.
    this_creation,
    other_creation
}).
