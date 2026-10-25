%% OTP 29 inet_res types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([hostent/0,nameserver/0,res_error/0,res_option/0]).
-type res_option() ::
          {alt_nameservers, [nameserver()]} |
          {edns, 0 | false} |
          {inet6, boolean()} |
          {nameservers, [nameserver()]} |
          {recurse, boolean()} |
          {retry, integer()} |
          {timeout, integer()} |
          {udp_payload_size, integer()} |
          {dnssec_ok, boolean()} |
          {usevc, boolean()} |
          {random, boolean()} |
          {nxdomain_reply, boolean()}.
-type nameserver() :: {inet:ip_address(), Port :: 1..65535}.
-type res_error() ::
          formerr | qfmterror | servfail | nxdomain | notimp | refused |
          badvers | timeout.
-type dns_name() :: string().
-type dns_rr_type() ::
          a | aaaa | caa | cname | gid | hinfo | ns | mb | md | mg |
          mf | minfo | mx | naptr | null | ptr | soa | spf | srv | txt |
          uid | uinfo | unspec | uri | wks.
-type dns_data() ::
          dns_name() |
          inet:ip4_address() |
          inet:ip6_address() |
          {MName :: dns_name(),
           RName :: dns_name(),
           Serial :: integer(),
           Refresh :: integer(),
           Retry :: integer(),
           Expiry :: integer(),
           Minimum :: integer()} |
          {inet:ip4_address(), Proto :: integer(), BitMap :: binary()} |
          {CpuString :: string(), OsString :: string()} |
          {RM :: dns_name(), EM :: dns_name()} |
          {Prio :: integer(), dns_name()} |
          {Prio :: integer(),
           Weight :: integer(),
           Port :: integer(),
           dns_name()} |
          {Order :: integer(),
           Preference :: integer(),
           Flags :: string(),
           Services :: string(),
           Regexp :: string(),
           dns_name()} |
          [string()] |
          binary().
-type hostent() ::
          inet:hostent() |
          {hostent,
           H_name :: inet:hostname(),
           H_aliases :: [inet:hostname()],
           H_addrtype :: dns_rr_type(),
           H_length :: non_neg_integer(),
           H_addr_list :: [dns_data()]}.
