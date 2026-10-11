#pragma once
#include "../../scheduler/executor.hpp"

// Port conversions shared by the port builtins and sends (docs/ports.md#builtins-and-port-messages).
namespace clause::runtime::builtins {
// The request a message sent to a port makes: {Pid, close}, {Pid, {command, Data}} with iodata Data or
// {Pid, {connect, NewPid}}; malformed for anything else.
detail::PortRequest port_request(const Term &message);
// The bytes of iodata: a binary, or a list of bytes, binaries and iodata lists with a binary or [] tail; none for
// anything else. A byte is valid only as a list element. Shared with md5/1 and md5_update/2 (step 41G3).
std::optional<std::vector<std::byte>> iodata_bytes(const Term &data);
} // namespace clause::runtime::builtins
