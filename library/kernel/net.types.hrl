%% OTP 29 net types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([address_info/0,
              ifaddrs/0,
              ifaddrs_flag/0,
              ifaddrs_flags/0,
              name_info/0,
              name_info_flag/0,
              name_info_flag_ext/0,
              name_info_flags/0,
              network_interface_index/0,
              network_interface_name/0]).
-type ifaddrs_flag() ::
          up | broadcast | debug | loopback | pointopoint | notrailers |
          running | noarp | promisc | master | slave | multicast |
          portsel | automedia | dynamic.
-type ifaddrs_flags() :: [ifaddrs_flag()].
-type ifaddrs() ::
          #{name := string(),
            flags := ifaddrs_flags(),
            addr => socket:sockaddr(),
            netmask => socket:sockaddr(),
            broadaddr => socket:sockaddr(),
            dstaddr => socket:sockaddr()}.
-type name_info_flags() :: [name_info_flag() | name_info_flag_ext()].
-type name_info_flag() ::
          namereqd | dgram | nofqdn | numerichost | numericserv.
-type name_info_flag_ext() :: idn.
-type name_info() :: #{host := string(), service := string()}.
-type address_info() ::
          #{family := socket:domain(),
            socktype := any | socket:type() | integer(),
            protocol := socket:protocol(),
            address := socket:sockaddr()}.
-type network_interface_name() :: string().
-type network_interface_index() :: non_neg_integer().
