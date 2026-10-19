#include "file.hpp"
#include "../../ports/posix_reason.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The positioned file operations of dets tables (plan 21 step LIB53) over the C runtime's descriptors.
namespace clause::runtime::builtins::dets {
namespace {
std::filesystem::path path_of(const std::string &name) {
    return {std::u8string(reinterpret_cast<const char8_t *>(name.data()), name.size())};
}

std::string errno_reason() {
    return std::string(detail::posix_reason(std::error_code(errno, std::generic_category())));
}

int open_flags(File::Mode mode) {
    int flags = mode == File::Mode::read ? O_RDONLY : O_RDWR;
    flags |= mode == File::Mode::create ? O_CREAT | O_TRUNC : 0;
#ifdef _WIN32
    flags |= _O_BINARY;
#endif
    return flags;
}

int open_descriptor(const std::string &name, int flags) {
#ifdef _WIN32
    int descriptor = -1;
    const auto error = _wsopen_s(&descriptor, path_of(name).c_str(), flags, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    errno = error;
    return error == 0 ? descriptor : -1;
#else
    return ::open(path_of(name).c_str(), flags | O_CLOEXEC, 0666);
#endif
}

std::int64_t seek(int descriptor, std::uint64_t at) {
#ifdef _WIN32
    return _lseeki64(descriptor, static_cast<std::int64_t>(at), SEEK_SET);
#else
    return ::lseek(descriptor, static_cast<off_t>(at), SEEK_SET);
#endif
}

std::int64_t read_some(int descriptor, std::span<std::byte> buffer) {
#ifdef _WIN32
    return _read(descriptor, buffer.data(), static_cast<unsigned>(std::min<std::size_t>(buffer.size(), 1U << 30U)));
#else
    return ::read(descriptor, buffer.data(), buffer.size());
#endif
}

std::int64_t write_some(int descriptor, std::span<const std::byte> bytes) {
#ifdef _WIN32
    return _write(descriptor, bytes.data(), static_cast<unsigned>(std::min<std::size_t>(bytes.size(), 1U << 30U)));
#else
    return ::write(descriptor, bytes.data(), bytes.size());
#endif
}

// Read until `buffer` is full or the file ends; the bytes read, or -1.
std::int64_t read_fully(int descriptor, std::span<std::byte> buffer) {
    std::size_t filled = 0;
    while (filled < buffer.size()) {
        const auto count = read_some(descriptor, buffer.subspan(filled));
        if (count <= 0) {
            return count < 0 ? -1 : static_cast<std::int64_t>(filled);
        }
        filled += static_cast<std::size_t>(count);
    }
    return static_cast<std::int64_t>(filled);
}

bool write_fully(int descriptor, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto count = write_some(descriptor, bytes);
        if (count <= 0) {
            return false;
        }
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
    return true;
}
} // namespace

void fail(Failure::Shape shape, std::string reason, const std::string &file) {
    throw Failure{.shape = shape, .reason = std::move(reason), .file = file};
}

File::File(std::string name, int descriptor, bool memory, Bytes contents)
    : name_(std::move(name)), descriptor_(descriptor), memory_(memory), contents_(std::move(contents)) {}

File File::open(const std::string &name, Mode mode) {
    const auto descriptor = open_descriptor(name, open_flags(mode));
    if (descriptor < 0) {
        fail(Failure::Shape::file_error, errno_reason(), name);
    }
    return {name, descriptor, false, {}};
}

File File::memory(const std::string &name, Bytes contents) { return {name, -1, true, std::move(contents)}; }

File::File(File &&other) noexcept
    : name_(std::move(other.name_)), descriptor_(std::exchange(other.descriptor_, -1)), memory_(other.memory_),
      contents_(std::move(other.contents_)) {}

File &File::operator=(File &&other) noexcept {
    if (this != &other) {
        close();
        name_ = std::move(other.name_);
        descriptor_ = std::exchange(other.descriptor_, -1);
        memory_ = other.memory_;
        contents_ = std::move(other.contents_);
    }
    return *this;
}

File::~File() { close(); }

void File::error() const { fail(Failure::Shape::file_error, errno_reason(), name_); }

Bytes File::pread(std::uint64_t at, std::size_t size) const {
    if (memory_) {
        const auto from = std::min<std::uint64_t>(at, contents_.size());
        const auto count = std::min<std::uint64_t>(size, contents_.size() - from);
        const auto begin = contents_.begin() + static_cast<std::ptrdiff_t>(from);
        return {begin, begin + static_cast<std::ptrdiff_t>(count)};
    }
    Bytes buffer(size);
    if (seek(descriptor_, at) < 0) {
        error();
    }
    const auto count = read_fully(descriptor_, buffer);
    if (count < 0) {
        error();
    }
    buffer.resize(static_cast<std::size_t>(count));
    return buffer;
}

void File::pwrite(std::uint64_t at, std::span<const std::byte> bytes) {
    if (memory_) {
        contents_.resize(std::max<std::uint64_t>(contents_.size(), at + bytes.size()));
        std::ranges::copy(bytes, contents_.begin() + static_cast<std::ptrdiff_t>(at));
        return;
    }
    if (seek(descriptor_, at) < 0 || !write_fully(descriptor_, bytes)) {
        error();
    }
}

std::uint64_t File::size() const {
    if (memory_) {
        return contents_.size();
    }
#ifdef _WIN32
    struct _stat64 status{};
    const auto found = _fstat64(descriptor_, &status) == 0;
#else
    struct stat status{};
    const auto found = fstat(descriptor_, &status) == 0;
#endif
    if (!found) {
        error();
    }
    return static_cast<std::uint64_t>(status.st_size);
}

void File::truncate(std::uint64_t at) {
    if (memory_) {
        contents_.resize(std::min<std::uint64_t>(at, contents_.size()));
        return;
    }
#ifdef _WIN32
    const auto done = _chsize_s(descriptor_, static_cast<std::int64_t>(at)) == 0;
#else
    const auto done = ::ftruncate(descriptor_, static_cast<off_t>(at)) == 0;
#endif
    if (!done) {
        error();
    }
}

void File::sync() {
    if (memory_) {
        return;
    }
#ifdef _WIN32
    const auto done = _commit(descriptor_) == 0;
#else
    const auto done = ::fsync(descriptor_) == 0;
#endif
    if (!done) {
        error();
    }
}

void File::write_out() {
    if (!memory_) {
        return;
    }
    auto out = open(name_, Mode::create);
    out.pwrite(0, contents_);
    out.sync();
}

void File::close() noexcept {
    if (descriptor_ >= 0) {
#ifdef _WIN32
        _close(descriptor_);
#else
        ::close(descriptor_);
#endif
        descriptor_ = -1;
    }
}

bool file_exists(const std::string &name) {
    std::error_code code;
    return std::filesystem::exists(path_of(name), code);
}

void remove_file(const std::string &name) {
    std::error_code code;
    std::filesystem::remove(path_of(name), code);
}

void rename_file(const std::string &from, const std::string &to) {
    std::error_code code;
    std::filesystem::rename(path_of(from), path_of(to), code);
    if (code) {
        fail(Failure::Shape::file_error, std::string(detail::posix_reason(code)), from);
    }
}

Bytes read_whole(const std::string &name) {
    const auto file = File::open(name, File::Mode::read);
    return file.pread(0, static_cast<std::size_t>(file.size()));
}
} // namespace clause::runtime::builtins::dets
