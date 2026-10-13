#include "inspection.hpp"
#include "../codegen/assembly_listing.hpp"
#include "../codegen/optimization.hpp"
#include "../codegen/serialization.hpp"
#include "../project/paths.hpp"
#include "display.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace clause::cli {
namespace {
// Capture only requested stages so optimized-only inspection never retains an unnecessary pre-pipeline copy.
bool capture(codegen::Compilation &compilation, const bool requested, std::vector<codegen::OutputBuffer> &outputs) {
    if (!requested) {
        return true;
    }
    auto snapshot = codegen::snapshot_ir(compilation);
    if (!snapshot) {
        return false;
    }
    outputs = std::move(*snapshot);
    return true;
}

// The snapshots of one inspection, all in input order; empty when not requested.
struct Snapshots {
    // IR before and after the selected pipeline (--print-ir, --print-optimized-ir).
    std::vector<codegen::OutputBuffer> before_;
    std::vector<codegen::OutputBuffer> after_;
    // Added for --print-asm.
    std::vector<codegen::OutputBuffer> assembly_;
    // The target's assembly comment marker, opening assembly headers.
    std::string assembly_comment_;
};

// Separate concatenated modules with safe comments (`comment` is the snapshot's own syntax) while leaving single
// snapshots plain assembly.
void print_snapshot(const codegen::OutputBuffer &output, const std::filesystem::path &source,
                    const std::string_view target, const std::string_view stage, const std::string_view comment,
                    const bool headers) {
    if (headers) {
        std::cout << comment << " clau target=" << quote_text(target) << " module=" << quote_text(output.module_name)
                  << " source=" << quote_text(project::path_text(source)) << " stage=" << stage << '\n';
    }
    if (output.bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("IR snapshot exceeds supported stream size");
    }
    std::cout.write(reinterpret_cast<const char *>(output.bytes.data()),
                    static_cast<std::streamsize>(output.bytes.size()));
    std::cout << '\n';
    if (!std::cout) {
        throw std::runtime_error("cannot write IR inspection output");
    }
}

// Preserve source order with each module's before/after/assembly adjacent, including separate project targets.
void print_snapshots(const codegen::Compilation &compilation, const FrontendRequest &request,
                     const Snapshots &snapshots) {
    const auto &inputs = compilation.request().inputs;
    const auto stages = static_cast<int>(!snapshots.before_.empty()) + static_cast<int>(!snapshots.after_.empty()) +
                        static_cast<int>(!snapshots.assembly_.empty());
    const bool headers = request.multiple_targets || inputs.size() > 1 || stages > 1;
    const auto &target = request.project_target;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto &source = inputs[i].source_path;
        if (!snapshots.before_.empty()) {
            print_snapshot(snapshots.before_.at(i), source, target, "before", ";", headers);
        }
        if (!snapshots.after_.empty()) {
            print_snapshot(snapshots.after_.at(i), source, target, "after", ";", headers);
        }
        if (!snapshots.assembly_.empty()) {
            print_snapshot(snapshots.assembly_.at(i), source, target, "assembly", snapshots.assembly_comment_,
                           headers);
        }
    }
}

// Capture annotated assembly after optimization. Added for --print-asm.
bool capture_assembly(codegen::Compilation &compilation, const bool requested, Snapshots &snapshots) {
    if (!requested) {
        return true;
    }
    auto listing = codegen::snapshot_assembly(compilation);
    if (!listing) {
        return false;
    }
    snapshots.assembly_ = std::move(*listing);
    snapshots.assembly_comment_ = codegen::assembly_comment(compilation);
    return true;
}
} // namespace

bool inspect_ir(codegen::Compilation &compilation, const FrontendRequest &request) {
    const auto &backend = request.backend;
    Snapshots snapshots;
    if (!capture(compilation, backend.print_ir, snapshots.before_)) {
        return false;
    }
    if ((backend.print_optimized_ir || backend.print_asm) && !codegen::optimize(compilation)) {
        return false;
    }
    if (!capture(compilation, backend.print_optimized_ir, snapshots.after_) ||
        !capture_assembly(compilation, backend.print_asm, snapshots)) {
        return false;
    }
    print_snapshots(compilation, request, snapshots);
    return true;
}
} // namespace clause::cli
