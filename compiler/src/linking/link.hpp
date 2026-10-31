#pragma once
#include "../codegen/output.hpp"
#include "crypto.hpp"
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clause::linking {
struct LinkRequest {
    // Requested executable path; Windows targets gain ".exe" when the name has no extension.
    std::filesystem::path output;
    // Normalized triple of the objects; selects the Clang target, object suffix and runtime check.
    std::string target_triple;
    // Serialized module objects followed by the startup object, linked in batch order.
    std::span<const codegen::OutputBuffer> objects;
    // Explicit Clang driver and runtime archive; absent values use PATH and the runtime beside this compiler.
    std::optional<std::filesystem::path> linker;
    std::optional<std::filesystem::path> runtime_library;
    // Sources and manifests the executable must never replace.
    std::span<const std::filesystem::path> protected_inputs;
    // Create a missing output directory (manifest outputs) instead of rejecting it (explicit -o).
    bool create_directory = false;
    // Ask the linker to drop unreferenced sections (-Os); objects then carry one section per symbol.
    bool strip_unused = false;
    // Keep the objects' line tables (-g): in the executable, or for MSVC targets in a PDB beside it.
    bool debug_info = false;
    // The objects are bitcode: link with LLD's link-time optimization (Windows MSVC and ELF targets only).
    bool lto = false;
    // Also link clause-epmd for the target beside the executable (--epmd-executable). Added for clause-epmd beside
    // programs (plan 30 step DIST3c).
    bool epmd_executable = false;
    // Link the crypto runtime archive and OpenSSL's libcrypto (the program calls crypto, or --with-crypto). Added for
    // the crypto runtime archive (plan 21 step LIB79).
    std::optional<CryptoLink> crypto = std::nullopt;
};

// Own a uniquely created directory beside an output; it is removed with its contents on destruction.
class StagingDirectory {
  public:
    explicit StagingDirectory(const std::filesystem::path &parent);
    ~StagingDirectory();
    StagingDirectory(StagingDirectory &&other) noexcept;
    StagingDirectory(const StagingDirectory &) = delete;
    StagingDirectory &operator=(const StagingDirectory &) = delete;
    StagingDirectory &operator=(StagingDirectory &&) = delete;

    // Directory holding the staged objects, linker log and linked executable.
    [[nodiscard]] const std::filesystem::path &path() const { return directory; }

  private:
    // Empty after a move, so only the last owner removes the directory.
    std::filesystem::path directory;
};

struct StagedExecutable {
    // Keep the linked file private until every executable of the invocation is ready.
    StagingDirectory staging;
    // Linked file inside the staging directory and its final destination (".exe" already applied).
    std::filesystem::path staged;
    std::filesystem::path output;
    // Inputs the destination is checked against again just before replacement.
    std::vector<std::filesystem::path> protected_inputs;
    // Output of the successful linker run, forwarded as warnings by the caller.
    std::string warnings;
    // Staged debug symbols published beside the output: a PDB (MSVC targets) or a dSYM bundle (Darwin targets)
    // with debug information; empty otherwise.
    std::filesystem::path symbols = {};
    // Staged files published beside the output, with their destinations: a pinned program's FIPS provider (added
    // for crypto's FIPS mode, plan 21 step LIB79F) and clause-epmd (plan 30 step DIST3c).
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> companions = {};
};

struct LinkTools {
    // Final executable path (".exe" applied), Clang driver (empty when none is found) and runtime archive.
    std::filesystem::path output;
    std::string linker;
    std::filesystem::path runtime_library;
};

// Resolve what linking `request` would use, without checking the destination or the runtime archive.
LinkTools resolve_link_tools(const LinkRequest &request);
// Link in a private staging directory beside the output without touching the output.
// Failures throw std::runtime_error; the staging directory is removed.
StagedExecutable stage_executable(const LinkRequest &request);
// Replace the destination with a staged executable, then its PDB beside it; a failure keeps an existing output
// unchanged.
void publish_executable(const StagedExecutable &executable);
// Stage and immediately publish one executable; returns linker warnings.
std::string link_executable(const LinkRequest &request);
// The project library root (library/ of this build tree, one directory per OTP application), relative to this
// compiler.
std::filesystem::path library_directory();
// The library modules this build generates (public_key's ASN.1 modules), one directory per application. Added for
// public_key.
std::filesystem::path generated_library_directory();
} // namespace clause::linking
