#pragma once
#include "../../scheduler/executor.hpp"

// Port conversions shared by the port builtins and sends (docs/ports.md#builtins-and-port-messages).
namespace clause::runtime::builtins {
// The request a message sent to a port makes: {Pid, close}, {Pid, {command, Data}} with iodata Data or
// {Pid, {connect, NewPid}}; malformed for anything else.
detail::PortRequest port_request(const Term &message);
} // namespace clause::runtime::builtins
