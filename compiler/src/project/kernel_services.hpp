#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

// The kernel services kernel_sup starts, which a program may exclude (exclude_kernel_services,
// --exclude-kernel-service). Added for opting out of kernel services (plan 30 step DIST4b1e).
namespace clause::project {
struct KernelService {
    // kernel_sup's child id, as OTP names it.
    std::string_view id;
    // The module whose start_link/0 starts it; empty for kernel_safe_sup, a supervisor of kernel's own.
    std::string_view module;
};

inline constexpr std::array KERNEL_SERVICES{KernelService{"rex", "rpc"}, KernelService{"global_name_server", "global"},
                                            KernelService{"global_group", "global_group"},
                                            KernelService{"kernel_safe_sup", ""}};

// Why `id` cannot be excluded, or nothing when it can.
inline std::optional<std::string> kernel_service_error(const std::string_view id) {
    if (id == "net_sup") {
        return "net_sup cannot be excluded: distribution needs it";
    }
    if (std::ranges::any_of(KERNEL_SERVICES, [id](const KernelService &service) { return service.id == id; })) {
        return std::nullopt;
    }
    return "unknown kernel service " + std::string(id) +
           " (expected rex, global_name_server, global_group or kernel_safe_sup)";
}
} // namespace clause::project
