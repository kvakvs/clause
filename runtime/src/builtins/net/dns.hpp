#pragma once
#include "net.hpp"

// The DNS message codec of inet_res (plan 21 step LIB60B; dns.cpp).
namespace clause::runtime::builtins::net::dns {
// clause_net:dns_decode(Binary): {ok, #dns_rec{}} as inet_dns decodes a reply, or {error, formerr}.
Term decode(ProcessContext &context, const Term &binary);
// clause_net:dns_encode(#dns_rec{}): the message as a binary; badarg for a record it cannot write.
Term encode(ProcessContext &context, const Term &record);
} // namespace clause::runtime::builtins::net::dns
