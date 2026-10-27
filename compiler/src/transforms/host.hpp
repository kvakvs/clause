#pragma once
// Added for the transform host (plan 21 LIB77A): building Clause's transform host executable with this clau
// (docs/transforms.md#transform-host).
#include <filesystem>
#include <span>
#include <string>

namespace clause::transforms {
// Where a host lives: <user cache>/clause/transform-host/<clau>-<identity>/, named by hashes of this clau's path and
// of what the host is built from.
struct HostPlace {
    std::string clau_;
    std::string identity_;
};

// The path, size and modification time of every file at or below `roots`, one line each in path order: what a
// host's identity hashes.
std::string file_listing(std::span<const std::filesystem::path> roots);

// The host executable of `place`, built there by this clau (the loader linked with the whole library, as --shell
// links it) unless it exists; another clau building it is waited for. A new build removes the hosts that older
// builds of the same clau path left. Throws LoaderError when no host can be had.
std::filesystem::path ensure_host(const HostPlace &place);
} // namespace clause::transforms
