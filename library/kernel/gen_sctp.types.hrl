%% OTP 29 gen_sctp types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([assoc_id/0,
              option/0,
              option_name/0,
              option_value/0,
              sctp_socket/0]).
-record(sctp_initmsg,{num_ostreams, max_instreams, max_attempts,
                      max_init_timeo}).
-record(sctp_sndrcvinfo,{stream, ssn, flags, ppid, context, timetolive,
                         tsn, cumtsn, assoc_id}).
-record(sctp_rtoinfo,{assoc_id, initial, max, min}).
-record(sctp_assocparams,{assoc_id, asocmaxrxt,
                          number_peer_destinations, peer_rwnd,
                          local_rwnd, cookie_life}).
-record(sctp_prim,{assoc_id, addr}).
-record(sctp_setpeerprim,{assoc_id, addr}).
-record(sctp_setadaptation,{adaptation_ind}).
-record(sctp_paddrparams,{assoc_id, address, hbinterval, pathmaxrxt,
                          pathmtu, sackdelay, flags}).
-record(sctp_event_subscribe,{data_io_event, association_event,
                              address_event, send_failure_event,
                              peer_error_event, shutdown_event,
                              partial_delivery_event,
                              adaptation_layer_event,
                              authentication_event}).
-record(sctp_assoc_value,{assoc_id, assoc_value}).
-record(sctp_paddrinfo,{assoc_id, address, state, cwnd, srtt, rto, mtu}).
-record(sctp_status,{assoc_id, state, rwnd, unackdata, penddata,
                     instrms, outstrms, fragmentation_point, primary}).
-type assoc_id() :: term().
-type option() :: elementary_option() | record_option().
-type option_name() ::
          elementary_option_name() | record_option() | ro_option().
-type option_value() ::
          elementary_option() | record_option() | ro_option().
-type elementary_option() ::
          {active, true | false | once | -32768..32767} |
          {buffer, non_neg_integer()} |
          {non_block_send, boolean()} |
          {debug, boolean()} |
          {dontroute, boolean()} |
          {exclusiveaddruse, boolean()} |
          {high_msgq_watermark, pos_integer()} |
          {linger, {boolean(), non_neg_integer()}} |
          {low_msgq_watermark, pos_integer()} |
          {mode, list | binary} |
          list | binary |
          {priority, non_neg_integer()} |
          {recbuf, non_neg_integer()} |
          {reuseaddr, boolean()} |
          {reuseport, boolean()} |
          {reuseport_lb, boolean()} |
          {ipv6_v6only, boolean()} |
          {sndbuf, non_neg_integer()} |
          {sctp_autoclose, non_neg_integer()} |
          {sctp_disable_fragments, boolean()} |
          {sctp_i_want_mapped_v4_addr, boolean()} |
          {sctp_maxseg, non_neg_integer()} |
          {sctp_nodelay, boolean()} |
          {tos, non_neg_integer()} |
          {tclass, non_neg_integer()} |
          {ttl, non_neg_integer()} |
          {recvtos, boolean()} |
          {recvtclass, boolean()} |
          {recvttl, boolean()}.
-type elementary_option_name() ::
          active | buffer | non_block_send | debug | dontroute |
          exclusiveaddruse | high_msgq_watermark | linger |
          low_msgq_watermark | mode | priority | recbuf | reuseaddr |
          reuseport | reuseport_lb | ipv6_v6only | sctp_autoclose |
          sctp_disable_fragments | sctp_i_want_mapped_v4_addr |
          sctp_maxseg | sctp_nodelay | sndbuf | tos | tclass | ttl |
          recvtos | recvtclass | recvttl.
-type record_option() ::
          {sctp_adaptation_layer, #sctp_setadaptation{}} |
          {sctp_associnfo, #sctp_assocparams{}} |
          {sctp_default_send_param, #sctp_sndrcvinfo{}} |
          {sctp_delayed_ack_time, #sctp_assoc_value{}} |
          {sctp_events, #sctp_event_subscribe{}} |
          {sctp_initmsg, #sctp_initmsg{}} |
          {sctp_peer_addr_params, #sctp_paddrparams{}} |
          {sctp_primary_addr, #sctp_prim{}} |
          {sctp_rtoinfo, #sctp_rtoinfo{}} |
          {sctp_set_peer_primary_addr, #sctp_setpeerprim{}}.
-type ro_option() ::
          {sctp_get_peer_addr_info, #sctp_paddrinfo{}} |
          {sctp_status, #sctp_status{}}.
-type sctp_socket() :: port().
