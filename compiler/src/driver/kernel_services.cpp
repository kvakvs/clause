#include "kernel_services.hpp"
#include "../project/kernel_services.hpp"
#include "../project/paths.hpp"

namespace clause::cli {
std::string kernel_services_source(const std::span<const std::string> excluded) {
    std::string ids;
    std::string funs;
    for (const auto &service : project::KERNEL_SERVICES) {
        if (std::ranges::find(excluded, service.id) != excluded.end()) {
            continue;
        }
        ids.append(ids.empty() ? "" : ", ").append(service.id);
        if (!service.module.empty()) {
            funs.append(funs.empty() ? "" : ", ").append("fun ").append(service.module).append(":start_link/0");
        }
    }
    // The literal funs link the included services' modules into the program.
    return "-module(clause_kernel_services).\n-export([services/0]).\nservices() ->\n    _ = [" + funs + "],\n    [" +
           ids + "].\n";
}

std::filesystem::path kernel_services_path() { return project::native_path("clause_kernel_services.erl"); }
} // namespace clause::cli
