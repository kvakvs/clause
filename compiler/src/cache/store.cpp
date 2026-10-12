// Added for build caches (plan step 62D2): keys, hashes and miss texts shared by every cache store backend.
#include "store.hpp"
#include <array>
#include <fstream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/xxhash.h>
#include <sstream>

namespace clause::cache {
namespace {
std::optional<std::string> read_all(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}
} // namespace

std::string_view miss_text(const Miss miss) {
    static constexpr std::array<std::string_view, 4> TEXTS{"hit", "no entry", "inputs changed", "damaged entry"};
    return TEXTS.at(static_cast<std::size_t>(miss));
}

std::string hash_text(std::string_view bytes) {
    const auto hash = llvm::xxh3_128bits(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    static constexpr std::string_view DIGITS = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (const auto part : {hash.high64, hash.low64}) {
        for (int shift = 60; shift >= 0; shift -= 4) {
            result.push_back(DIGITS[(part >> static_cast<unsigned>(shift)) & 0xFU]);
        }
    }
    return result;
}

std::string program_identity() {
    static int anchor = 0;
    auto path = llvm::sys::fs::getMainExecutable(nullptr, &anchor);
    llvm::sys::fs::file_status status;
    if (llvm::sys::fs::status(path, status)) {
        return path;
    }
    const auto modified = status.getLastModificationTime().time_since_epoch().count();
    return path + "\n" + std::to_string(status.getSize()) + "\n" + std::to_string(modified);
}

const Part *find_part(const Value &value, const std::string_view name) {
    for (const auto &part : value.parts_) {
        if (part.name_ == name) {
            return &part;
        }
    }
    return nullptr;
}

KeyBuilder &KeyBuilder::add(std::string_view part) {
    // Little-endian length, then the bytes.
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes_.push_back(static_cast<char>((std::uint64_t{part.size()} >> shift) & 0xFFU));
    }
    bytes_.append(part);
    return *this;
}

KeyBuilder &KeyBuilder::add_file(const std::filesystem::path &path) {
    const auto text = path.generic_u8string();
    add({reinterpret_cast<const char *>(text.data()), text.size()});
    const auto content = read_all(path);
    return add(content ? hash_text(*content) : std::string("missing"));
}
} // namespace clause::cache
