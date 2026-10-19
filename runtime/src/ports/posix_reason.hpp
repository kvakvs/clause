#pragma once
#include <string_view>
#include <system_error>

namespace clause::runtime::detail {
// The POSIX reason of an error code as OTP names it (enoent, eacces, ...), eio for codes without a name; shared by
// the file driver and the dets file layer (plan 21 step LIB53).
std::string_view posix_reason(std::error_code code);
} // namespace clause::runtime::detail
