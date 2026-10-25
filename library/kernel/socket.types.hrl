%% OTP 29 socket types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([cmsg/0,
              cmsg_recv/0,
              cmsg_send/0,
              completion_handle/0,
              completion_info/0,
              completion_tag/0,
              domain/0,
              ee_origin/0,
              eei/0,
              extended_err/0,
              icmp_dest_unreach/0,
              icmpv6_dest_unreach/0,
              in6_addr/0,
              in_addr/0,
              info_keys/0,
              interface_type/0,
              invalid/0,
              ioctl_device_flag/0,
              ioctl_device_map/0,
              ip_mreq/0,
              ip_mreq_source/0,
              ip_msfilter/0,
              ip_pktinfo/0,
              ip_pmtudisc/0,
              ip_tos/0,
              iptos_dscp/0,
              iptos_native/0,
              iptos_tos/0,
              iptos_value/0,
              ipv6_hops/0,
              ipv6_mreq/0,
              ipv6_pktinfo/0,
              ipv6_pmtudisc/0,
              level/0,
              linger/0,
              msg/0,
              msg_flag/0,
              msg_recv/0,
              msg_send/0,
              otp_socket_option/0,
              port_number/0,
              protocol/0,
              sctp_assoc_id/0,
              sctp_assoc_stats/0,
              sctp_assocparams/0,
              sctp_event_subscribe/0,
              sctp_initmsg/0,
              sctp_pap_flag/0,
              sctp_pap_flags/0,
              sctp_peer_address_info/0,
              sctp_peer_address_parameters/0,
              sctp_peer_address_state/0,
              sctp_rtoinfo/0,
              sctp_set_adaptation_layer_ind/0,
              sctp_set_peer_primary_address/0,
              sctp_set_primary_address/0,
              sctp_setadaption/0,
              sctp_snd_rcv_info/0,
              sctp_status/0,
              select_handle/0,
              select_info/0,
              select_tag/0,
              sockaddr/0,
              sockaddr_dl/0,
              sockaddr_in/0,
              sockaddr_in6/0,
              sockaddr_ll/0,
              sockaddr_native/0,
              sockaddr_recv/0,
              sockaddr_un/0,
              sockaddr_unspec/0,
              socket/0,
              socket_counters/0,
              socket_handle/0,
              socket_info/0,
              socket_option/0,
              timespec/0,
              timeval/0,
              type/0]).
-type uint8() :: 0..255.
-type uint16() :: 0..65535.
-type uint32() :: 0..4294967295.
-type uint64() :: 0..18446744073709551615.
-type invalid() :: {invalid, What :: term()}.
-type eei() ::
          #{info :=
                econnreset | econnaborted | netname_deleted |
                too_many_cmds |
                atom(),
            raw_info := term()}.
-type socket_counters() ::
          #{read_byte := non_neg_integer(),
            read_fails := non_neg_integer(),
            read_pkg := non_neg_integer(),
            read_pkg_max := non_neg_integer(),
            read_tries := non_neg_integer(),
            read_waits := non_neg_integer(),
            write_byte := non_neg_integer(),
            write_fails := non_neg_integer(),
            write_pkg := non_neg_integer(),
            write_pkg_max := non_neg_integer(),
            write_tries := non_neg_integer(),
            write_waits := non_neg_integer(),
            sendfile => non_neg_integer(),
            sendfile_byte => non_neg_integer(),
            sendfile_fails => non_neg_integer(),
            sendfile_max => non_neg_integer(),
            sendfile_pkg => non_neg_integer(),
            sendfile_pkg_max => non_neg_integer(),
            sendfile_tries => non_neg_integer(),
            sendfile_waits => non_neg_integer(),
            acc_success := non_neg_integer(),
            acc_fails := non_neg_integer(),
            acc_tries := non_neg_integer(),
            acc_waits := non_neg_integer()}.
-type socket_info() ::
          #{domain := domain() | integer(),
            type := type() | integer(),
            protocol := protocol() | integer(),
            owner := pid(),
            ctype := normal | fromfd | {fromfd, integer()},
            counters := socket_counters(),
            num_readers := non_neg_integer(),
            num_writers := non_neg_integer(),
            num_acceptors := non_neg_integer(),
            writable := boolean(),
            readable := boolean(),
            rstates := [atom()],
            wstates := [atom()]}.
-type domain() :: inet | inet6 | local | unspec.
-type type() :: stream | dgram | raw | rdm | seqpacket.
-type protocol() :: atom().
-type port_number() :: 0..65535.
-type in_addr() :: {0..255, 0..255, 0..255, 0..255}.
-type in6_flow_info() :: 0..1048575.
-type in6_scope_id() :: 0..4294967295.
-type in6_addr() ::
          {0..65535,
           0..65535,
           0..65535,
           0..65535,
           0..65535,
           0..65535,
           0..65535,
           0..65535}.
-type linger() :: #{onoff := boolean(), linger := non_neg_integer()}.
-type timeval() :: #{sec := integer(), usec := integer()}.
-type timespec() :: #{sec := integer(), nsec := integer()}.
-type ip_mreq() :: #{multiaddr := in_addr(), interface := in_addr()}.
-type ip_mreq_source() ::
          #{multiaddr := in_addr(),
            interface := in_addr(),
            sourceaddr := in_addr()}.
-type ip_msfilter() ::
          #{multiaddr := in_addr(),
            interface := in_addr(),
            mode := include | exclude,
            slist := [in_addr()]}.
-type ip_pmtudisc() :: want | dont | do | probe.
-type ip_tos() ::
          #{native := iptos_native(),
            tos := iptos_tos(),
            dscp := iptos_dscp() | non_neg_integer()}.
-type iptos_value() :: iptos_tos() | iptos_dscp() | iptos_native().
-type iptos_tos() ::
          #{precedence := iptos_tos_prec() | non_neg_integer(),
            tos := iptos_tos_value() | non_neg_integer()}.
-type iptos_tos_prec() ::
          netcontrol | internetcontrol | critical_ecp | flashoverride |
          flash | immediate | priority | routine.
-type iptos_tos_value() ::
          default | lowdelay | throughput | reliability | mincost.
-type iptos_dscp() ::
          cs0 | le | cs1 | af11 | af12 | af13 | cs2 | af21 | af22 |
          af23 | cs3 | af31 | af32 | af33 | cs4 | af41 | af42 | af43 |
          cs5 | voice_admit | nqb | ef | cs6 | cs7.
-type iptos_native() :: non_neg_integer().
-type ip_pktinfo() ::
          #{ifindex := non_neg_integer(),
            spec_dst := in_addr(),
            addr := in_addr()}.
-type ipv6_mreq() ::
          #{multiaddr := in6_addr(), interface := non_neg_integer()}.
-type ipv6_pmtudisc() :: want | dont | do | probe.
-type ipv6_hops() :: default | 0..255.
-type ipv6_pktinfo() :: #{addr := in6_addr(), ifindex := integer()}.
-type sctp_assoc_id() :: integer().
-type sctp_setadaption() :: #{adaption_ind := uint32()}.
-type sctp_assocparams() ::
          #{assoc_id := sctp_assoc_id(),
            asocmaxrxt := uint16(),
            number_peer_destinations := uint16(),
            peer_rwnd := uint32(),
            local_rwnd := uint32(),
            cookie_life := uint32()}.
-type sctp_event_subscribe() ::
          #{data_io := boolean(),
            association := boolean(),
            address := boolean(),
            send_failure := boolean(),
            peer_error := boolean(),
            shutdown := boolean(),
            partial_delivery := boolean(),
            adaptation_layer => boolean(),
            sender_dry => boolean(),
            stream_reset => boolean(),
            assoc_reset => boolean(),
            stream_change => boolean()}.
-type sctp_initmsg() ::
          #{num_ostreams := uint16(),
            max_instreams := uint16(),
            max_attempts := uint16(),
            max_init_timeo := uint16()}.
-type sctp_rtoinfo() ::
          #{assoc_id := sctp_assoc_id(),
            initial := uint32(),
            max := uint32(),
            min := uint32()}.
-type sctp_set_peer_primary_address() ::
          #{assoc_id := sctp_assoc_id(), addr := sockaddr()}.
-type sctp_set_primary_address() ::
          #{assoc_id := sctp_assoc_id(), addr := sockaddr()}.
-type sctp_set_adaptation_layer_ind() :: #{ind := uint32()}.
-type sctp_peer_address_parameters() ::
          #{assoc_id := sctp_assoc_id(),
            addr := sockaddr(),
            heatbeat_interval := uint32(),
            path_max_rxt := uint16(),
            path_mtu => uint32(),
            sack_delay => uint32(),
            flags => sctp_pap_flags(),
            ipv6_flowlabel => uint32(),
            dscp => uint8()}.
-type sctp_pap_flags() :: integer() | [sctp_pap_flag()].
-type sctp_pap_flag() ::
          enable_heartbeats | disable_heartbeats |
          send_heartbeat_immediately | enable_pmtu_discovery |
          disable_pmtu_discovery | enable_sack | disable_sack |
          set_heartbeat_delay_to_zero | ipv6_flowlabel | dscp.
-type sctp_status() ::
          #{assoc_id := sctp_assoc_id(),
            state := sctp_peer_address_state(),
            rwnd := uint32(),
            unacked_data := uint16(),
            pending_data := uint16(),
            in_streams := uint16(),
            out_streams := uint16(),
            fragmentation_point := uint32(),
            primary := sctp_peer_address_info()}.
-type sctp_assoc_stats() ::
          #{assoc_id := sctp_assoc_id(),
            max_rto_addr := sockaddr(),
            max_rto := uint64(),
            in_sacks := uint64(),
            out_sacks := uint64(),
            in_packets := uint64(),
            out_packets := uint64(),
            rtx_chunks := uint64(),
            out_of_seq_tsns := uint64(),
            in_dup_chunks := uint64(),
            gap_ack_recv := uint64(),
            in_unordered_chunks := uint64(),
            out_unordered_chunks := uint64(),
            in_ordered_chunks := uint64(),
            out_ordered_chunks := uint64(),
            in_ctrl_chunks := uint64(),
            out_ctrl_chunks := uint64()}.
-type sctp_peer_address_info() ::
          #{assoc_id := sctp_assoc_id(),
            address := sockaddr(),
            state := sctp_peer_address_state(),
            cwnd := uint32(),
            srtt := uint32(),
            rto := uint32(),
            mtu := uint32()}.
-type sctp_peer_address_state() ::
          inactive | potentially_failed | active | unconfirmed | unknown.
-type packet_type() ::
          host | broadcast | multicast | otherhost | outgoing |
          loopback | user | kernel | fastroute |
          non_neg_integer().
-type hatype() ::
          netrom | eether | ether | ax25 | pronet | chaos | ieee802 |
          arcnet | appletlk | dlci | atm | metricom | ieee1394 | eui64 |
          infiniband | tunnel | tunnel6 | loopback | localtlk | none |
          void |
          non_neg_integer().
-type sockaddr_un() :: #{family := local, path := binary() | string()}.
-type sockaddr_in() ::
          #{family := inet,
            port := port_number(),
            addr := any | broadcast | loopback | in_addr()}.
-type sockaddr_in6() ::
          #{family := inet6,
            port := port_number(),
            addr := any | loopback | in6_addr(),
            flowinfo := in6_flow_info(),
            scope_id := in6_scope_id()}.
-type sockaddr_ll() ::
          #{family := packet,
            protocol := non_neg_integer(),
            ifindex := integer(),
            pkttype := packet_type(),
            hatype := hatype(),
            addr := binary()}.
-type interface_type() ::
          other | hdh1822 | x25ddh | x25 | ether | ppp | loop | ipv4 |
          ipv6 | '6to4' | gif | faith | stf | bridge | cellular |
          non_neg_integer().
-type sockaddr_dl() ::
          #{family := link,
            index := non_neg_integer(),
            type := interface_type(),
            nlen := non_neg_integer(),
            alen := non_neg_integer(),
            slen := non_neg_integer(),
            data := binary()}.
-type sockaddr_unspec() :: #{family := unspec, addr := binary()}.
-type sockaddr_native() :: #{family := integer(), addr := binary()}.
-type sockaddr() ::
          sockaddr_in() |
          sockaddr_in6() |
          sockaddr_un() |
          sockaddr_ll() |
          sockaddr_dl() |
          sockaddr_unspec() |
          sockaddr_native().
-type sockaddr_recv() :: sockaddr() | binary().
-type level() :: socket | protocol().
-type otp_socket_option() ::
          debug | iow | controlling_process | rcvbuf | rcvctrlbuf |
          sndctrlbuf | select_read | meta | use_registry | fd | domain.
-type socket_option() ::
          {Level :: socket,
           Opt ::
               acceptconn | acceptfilter | bindtodevice | broadcast |
               bsp_state | busy_poll | debug | domain | dontroute |
               error | exclusiveaddruse | keepalive | linger | mark |
               maxdg | max_msg_size | oobinline | passcred | peek_off |
               peercred | priority | protocol | rcvbuf | rcvbufforce |
               rcvlowat | rcvtimeo | reuseaddr | reuseport | rxq_ovfl |
               setfib | sndbuf | sndbufforce | sndlowat | sndtimeo |
               timestamp | timestampns | type} |
          {Level :: ip,
           Opt ::
               add_membership | add_source_membership | block_source |
               dontfrag | drop_membership | drop_source_membership |
               freebind | hdrincl | minttl | msfilter | mtu |
               mtu_discover | multicast_all | multicast_if |
               multicast_loop | multicast_ttl | nodefrag | options |
               pktinfo | recvdstaddr | recverr | recvif | recvopts |
               recvorigdstaddr | recvtos | recvttl | retopts |
               router_alert | sndsrcaddr | tos | transparent | ttl |
               unblock_source} |
          {Level :: ipv6,
           Opt ::
               addrform | add_membership | authhdr | auth_level |
               checksum | drop_membership | dstopts | esp_trans_level |
               esp_network_level | faith | flowinfo | hopopts |
               ipcomp_level | join_group | leave_group | mtu |
               mtu_discover | multicast_hops | multicast_if |
               multicast_loop | portrange | pktoptions | recverr |
               recvhoplimit | hoplimit | recvpktinfo | pktinfo |
               recvtclass | router_alert | rthdr | tclass |
               unicast_hops | use_min_mtu | v6only} |
          {Level :: tcp,
           Opt ::
               congestion | cork | info | keepcnt | keepidle |
               keepintvl | maxseg | md5sig | nodelay | noopt | nopush |
               syncnt | user_timeout} |
          {Level :: udp, Opt :: cork} |
          {Level :: sctp,
           Opt ::
               adaption_layer | associnfo | auth_active_key |
               auth_asconf | auth_chunk | auth_key | auth_delete_key |
               autoclose | context | default_send_param |
               delayed_ack_time | disable_fragments | hmac_ident |
               events | explicit_eor | fragment_interleave |
               get_peer_addr_info | initmsg | i_want_mapped_v4_addr |
               local_auth_chunks | maxseg | maxburst | nodelay |
               partial_delivery_point | peer_addr_params |
               peer_auth_chunks | primary_addr | reset_streams |
               rtoinfo | set_peer_primary_addr | status |
               use_ext_recvinfo}.
-type socket() :: {'$socket', socket_handle()}.
-opaque socket_handle() :: reference().
-type msg_flag() ::
          cmsg_cloexec | confirm | ctrunc | dontroute | eor | errqueue |
          more | oob | peek | trunc.
-type msg() :: msg_send() | msg_recv().
-type msg_send() ::
          #{addr => sockaddr(),
            iov := erlang:iovec(),
            ctrl =>
                [cmsg_send() |
                 #{level := level() | integer(),
                   type := integer(),
                   data := binary()}]}.
-type msg_data_recv() ::
          #{addr => sockaddr_recv(),
            iov := erlang:iovec(),
            ctrl :=
                [cmsg_recv() |
                 #{level := level() | integer(),
                   type := integer(),
                   data := binary()}],
            flags := [msg_flag() | integer()]}.
-type msg_notification_recv() ::
          #{addr => sockaddr_recv(),
            notification := sctp_notification(),
            ctrl :=
                [#{level := level() | integer(),
                   type := integer(),
                   data := binary()}],
            flags := [notification | [msg_flag() | integer()]]}.
-type msg_recv() :: msg_data_recv() | msg_notification_recv().
-type sctp_notification() ::
          sctp_assoc_change() |
          sctp_paddr_change() |
          sctp_send_failed() |
          sctp_remote_error() |
          sctp_shutdown_event() |
          sctp_adapt_event() |
          sctp_pdapi_event() |
          sctp_authkey() |
          sctp_sender_dry() |
          sctp_stream_reset_event() |
          sctp_assoc_reset_event() |
          sctp_stream_change_event() |
          sctp_send_failed_event() |
          sctp_notification_generic().
-type sctp_assoc_change() ::
          #{type := assoc_change,
            flags := integer(),
            state := sctp_assoc_change_state(),
            error := sctp_operation_error(),
            outbound_streams := integer(),
            inbound_streams := integer(),
            assoc_id := sctp_assoc_id()}.
-type sctp_assoc_change_state() ::
          comm_up | comm_lost | restart | shutdown_comp |
          cant_str_assoc |
          integer().
-type sctp_operation_error() ::
          unknown | bad_sid | missing_parm | stale_cookie |
          no_resources | bad_addr | unrec_chunk | bad_mandparm |
          unrec_parm | no_usr_data | cookie_shut | restart_new_addrs |
          user_abort | delete_lastaddr | resource_shortage |
          delete_srcaddr | auth_err |
          pos_integer().
-type sctp_paddr_change() ::
          #{type := peer_addr_change,
            flags := pos_integer(),
            addr := socket:sockaddr(),
            state := sctp_peer_addr_change_state(),
            error := pos_integer(),
            assoc_id := sctp_assoc_id()}.
-type sctp_peer_addr_change_state() ::
          addr_available | addr_unreachable | addr_removed |
          addr_added | addr_made_prim | addr_confirmed |
          addr_potentially_failed |
          integer().
-type sctp_send_failed() ::
          #{type := send_failed,
            flags := uint16(),
            error := uint32(),
            info := sctp_snd_rcv_info(),
            assoc_id := sctp_assoc_id(),
            data := binary()}.
-type sctp_remote_error() ::
          #{type := remote_error,
            flags := uint16(),
            error := sctp_operation_error(),
            assoc_id := sctp_assoc_id(),
            remote_causes := [integer()]}.
-type sctp_shutdown_event() ::
          #{type := shutdown_event,
            flags := uint16(),
            assoc_id := sctp_assoc_id()}.
-type sctp_adapt_event() ::
          #{type := adaptation_event,
            flags := uint16(),
            adaption_ind := uint32(),
            assoc_id := sctp_assoc_id()}.
-type sctp_pdapi_event() ::
          #{type := partial_delivery_event,
            flags := uint16(),
            indication := uint16(),
            assoc_id := sctp_assoc_id(),
            stream => uint32(),
            seq => uint32()}.
-type sctp_authkey() ::
          #{type := authkey,
            flags := uint16(),
            keynumber := uint16(),
            altkeynumber := uint16(),
            indication := uint32(),
            assoc_id := sctp_assoc_id()}.
-type sctp_sender_dry() ::
          #{type := sender_dry,
            flags := uint16(),
            assoc_id := sctp_assoc_id()}.
-type sctp_stream_reset_event() ::
          #{type := stream_reset,
            flags := [incoming_ssn | outgoing_ssn | denied | failed],
            assoc_id := sctp_assoc_id(),
            stream_list := [uint16()]}.
-type sctp_assoc_reset_event() ::
          #{type := assoc_reset,
            flags := [denied | failed],
            assoc_id := sctp_assoc_id(),
            local_tsn := uint32(),
            remote_tsn := uint32()}.
-type sctp_stream_change_event() ::
          #{type := stream_change,
            flags := [denied | failed],
            assoc_id := sctp_assoc_id(),
            inbound_streams := uint16(),
            outbound_streams := uint16()}.
-type sctp_send_failed_event() ::
          #{type := send_failed_event,
            flags := [data_unsent | data_sent],
            error := uint32(),
            info := sctp_snd_info(),
            assoc_id := sctp_assoc_id(),
            data := binary()}.
-type sctp_notification_generic() ::
          #{type := uint16(), flags := uint16()}.
-type sctp_snd_info() ::
          #{sid := uint16(),
            flags := uint16(),
            ppid := uint16(),
            context := uint32(),
            assic_id := sctp_assoc_id()}.
-type sctp_snd_rcv_info() ::
          #{stream := uint16(),
            ssn => uint16(),
            flags => sctp_snd_rcv_info_flags(),
            ppid => uint32(),
            context => uint32(),
            time_to_live => uint32(),
            tsn => uint32(),
            cum_tsn => uint32(),
            assoc_id := sctp_assoc_id()}.
-type sctp_snd_rcv_info_flags() :: [unordered | addr_over | abort | eof].
-type cmsg() :: cmsg_recv() | cmsg_send().
-type cmsg_recv() ::
          #{level := socket,
            type := timestamp,
            data := binary(),
            value => timeval()} |
          #{level := socket,
            type := timestampns,
            data := binary(),
            value => timespec()} |
          #{level := socket, type := rights, data := binary()} |
          #{level := socket, type := credentials, data := binary()} |
          #{level := ip,
            type := tos,
            data := binary(),
            value => ip_tos()} |
          #{level := ip,
            type := recvtos,
            data := binary(),
            value := ip_tos()} |
          #{level := ip,
            type := ttl,
            data := binary(),
            value => integer()} |
          #{level := ip,
            type := recvttl,
            data := binary(),
            value := integer()} |
          #{level := ip,
            type := pktinfo,
            data := binary(),
            value => ip_pktinfo()} |
          #{level := ip,
            type := origdstaddr,
            data := binary(),
            value => sockaddr_recv()} |
          #{level := ip,
            type := recverr,
            data := binary(),
            value => extended_err()} |
          #{level := ipv6,
            type := hoplimit,
            data := binary(),
            value => integer()} |
          #{level := ipv6,
            type := pktinfo,
            data := binary(),
            value => ipv6_pktinfo()} |
          #{level := ipv6,
            type := recverr,
            data := binary(),
            value => extended_err()} |
          #{level := ipv6,
            type := tclass,
            data := binary(),
            value => integer()}.
-type native_value() :: integer() | boolean() | binary().
-type cmsg_send() ::
          #{level := sctp, type := sndrcv, value := sctp_snd_rcv_info()} |
          #{level := socket,
            type := timestamp,
            data => native_value(),
            value => timeval()} |
          #{level := socket, type := rights, data := native_value()} |
          #{level := socket,
            type := credentials,
            data := native_value()} |
          #{level := ip,
            type := tos,
            data => native_value(),
            value => iptos_value()} |
          #{level := ip,
            type := ttl,
            data => native_value(),
            value => integer()} |
          #{level := ip,
            type := hoplimit,
            data => native_value(),
            value => integer()} |
          #{level := ipv6,
            type := tclass,
            data => native_value(),
            value => integer()}.
-type ee_origin() :: none | local | icmp | icmp6.
-type icmp_dest_unreach() ::
          net_unreach | host_unreach | port_unreach | frag_needed |
          net_unknown | host_unknown.
-type icmpv6_dest_unreach() ::
          noroute | adm_prohibited | not_neighbour | addr_unreach |
          port_unreach | policy_fail | reject_route.
-type extended_err() ::
          #{error := posix(),
            origin := icmp,
            type := dest_unreach,
            code := icmp_dest_unreach() | 0..255,
            info := 0..4294967295,
            data := 0..4294967295,
            offender := sockaddr_recv()} |
          #{error := posix(),
            origin := icmp,
            type := time_exceeded | 0..255,
            code := 0..255,
            info := 0..4294967295,
            data := 0..4294967295,
            offender := sockaddr_recv()} |
          #{error := posix(),
            origin := icmp6,
            type := dest_unreach,
            code := icmpv6_dest_unreach() | 0..255,
            info := 0..4294967295,
            data := 0..4294967295,
            offender := sockaddr_recv()} |
          #{error := posix(),
            origin := icmp6,
            type := pkt_toobig | time_exceeded | 0..255,
            code := 0..255,
            info := 0..4294967295,
            data := 0..4294967295,
            offender := sockaddr_recv()} |
          #{error := posix(),
            origin := ee_origin() | 0..255,
            type := 0..255,
            code := 0..255,
            info := 0..4294967295,
            data := 0..4294967295,
            offender := sockaddr_recv()}.
-type posix() :: inet:posix().
-type info_keys() ::
          [domain | type | protocol | fd | owner | local_address |
           remote_address | recv | sent | state].
-type ioctl_device_flag() ::
          up | broadcast | debug | loopback | pointopoint | notrailers |
          knowsepoch | running | noarp | promisc | allmulti | master |
          oactive | slave | simplex | link0 | link1 | link2 |
          multicast | portsel | automedia | cantconfig | ppromisc |
          dynamic | monitor | staticarp | dying | renaming | nogroup |
          lower_up | dormant | echo.
-type ioctl_device_map() ::
          #{mem_start := non_neg_integer(),
            mem_end := non_neg_integer(),
            base_addr := non_neg_integer(),
            irq := non_neg_integer(),
            dma := non_neg_integer(),
            port := non_neg_integer()}.
-type select_tag() ::
          accept | connect |
          recv | recvfrom | recvmsg | recvmmsg | send | sendv | sendto |
          sendmsg | sendmmsg | sendfile |
          {recv | recvfrom | recvmsg | recvmmsg | send | sendv |
           sendto | sendmsg | sendmmsg | sendfile,
           ContData :: term()}.
-type completion_tag() ::
          accept | connect |
          recv | recvfrom | recvmsg | recvmmsg | send | sendv | sendto |
          sendmsg | sendmmsg | sendfile.
-type select_handle() :: reference().
-type completion_handle() :: reference().
-type select_info() ::
          {select_info,
           SelectTag :: select_tag(),
           SelectHandle :: select_handle()}.
-type completion_info() ::
          {completion_info,
           CompletionTag :: completion_tag(),
           CompletionHandle :: completion_handle()}.
