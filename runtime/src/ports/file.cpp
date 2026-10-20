#include "file_info.hpp"
#include "file_paths.hpp"
#include "port.hpp"
#include "posix_reason.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The file driver (docs/ports.md#standard-io-and-files): the project library's file module opens it with
// {spawn_driver, "clause_file"} and works through port_control/3 operations, each a synchronous system call on
// the caller's worker. The operations and their replies are a Clause protocol (FileOperation): a reply starts
// with 0 (ok, then the result), 1 (error, then the POSIX reason) or 2 (end of file).
namespace clause::runtime::detail {
namespace {
using Bytes = ByteBuffer;

// The operations of port_control/3 on a file port.
enum class FileOperation : std::uint8_t {
    open = 1,
    read = 2,
    write = 3,
    position = 4,
    read_line = 5,
    close = 6,
    read_file = 10,
    write_file = 11,
    remove = 12,
    rename = 13,
    list_dir = 14,
    make_dir = 15,
    delete_dir = 16,
    // A path's status (step 41Y2).
    read_file_info = 17,
    // Directories and links (step 41P).
    get_cwd = 18,
    set_cwd = 19,
    read_link = 20,
    read_link_info = 21,
    make_link = 22,
    make_symlink = 23,
};

// Bits of the open operation's mode byte.
constexpr unsigned MODE_READ = 1;
constexpr unsigned MODE_WRITE = 2;
constexpr unsigned MODE_APPEND = 4;
constexpr unsigned MODE_EXCLUSIVE = 8;

// Bytes one read of a whole file or of a line asks for.
constexpr std::size_t CHUNK = 4096;
// Bytes read_line reads ahead at a time (read-ahead lines).
constexpr std::size_t LINE_AHEAD = 65536;

// A reply of success with `result`, allocated once.
Bytes ok(std::span<const std::byte> result = {}) {
    Bytes reply;
    reply.reserve(1 + result.size());
    reply.push_back(std::byte{0});
    reply.insert(reply.end(), result.begin(), result.end());
    return reply;
}

// A reply of end of file.
Bytes end_of_file() { return {std::byte{2}}; }

// A reply of an error with its POSIX reason.
Bytes failure(std::string_view reason) {
    Bytes reply{std::byte{1}};
    std::ranges::transform(reason, std::back_inserter(reply), [](char c) { return static_cast<std::byte>(c); });
    return reply;
}

// The POSIX reason of an error code, as OTP names it.
std::string_view reason(std::error_code code) { return posix_reason(code); }

// The error reply of the C runtime's errno.
Bytes errno_failure() { return failure(reason(std::error_code(errno, std::generic_category()))); }

// A path from UTF-8 bytes.
std::filesystem::path path_of(std::span<const std::byte> bytes) {
    std::u8string text(bytes.size(), u8'\0');
    std::memcpy(text.data(), bytes.data(), bytes.size());
    return {text};
}

// The UTF-8 bytes of a path.
Bytes bytes_of(const std::filesystem::path &path) {
    const auto text = path.u8string();
    Bytes result(text.size());
    std::memcpy(result.data(), text.data(), text.size());
    return result;
}

// A big-endian integer of `size` bytes at `at`.
std::uint64_t number(std::span<const std::byte> bytes, std::size_t at, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size && at + index < bytes.size(); ++index) {
        value = (value << 8) | std::to_integer<std::uint64_t>(bytes[at + index]);
    }
    return value;
}

// Two paths of an operation: a 32-bit length, the first path, then the second (or the data) to the end.
std::pair<std::span<const std::byte>, std::span<const std::byte>> split(std::span<const std::byte> bytes) {
    const auto length =
        std::min<std::size_t>(number(bytes, 0, 4), bytes.size() - std::min<std::size_t>(4, bytes.size()));
    const auto rest = bytes.subspan(std::min<std::size_t>(4, bytes.size()));
    return {rest.first(length), rest.subspan(length)};
}

// The access flags of a mode byte: read is the default, write and append write.
int access_flags(unsigned mode) {
    const bool read = (mode & MODE_READ) != 0 || (mode & (MODE_WRITE | MODE_APPEND)) == 0;
    const bool write = (mode & (MODE_WRITE | MODE_APPEND)) != 0;
    return read && write ? O_RDWR : (write ? O_WRONLY : O_RDONLY);
}

// Open flags of the open operation's mode byte: writing creates; write alone truncates (not with append, as OTP);
// append appends; exclusive creates a new file only.
int open_flags(unsigned mode) {
    const auto access = access_flags(mode);
    int flags = access | (access != O_RDONLY ? O_CREAT : 0);
    flags |= access == O_WRONLY && (mode & MODE_WRITE) != 0 && (mode & MODE_APPEND) == 0 ? O_TRUNC : 0;
    flags |= (mode & MODE_APPEND) != 0 ? O_APPEND : 0;
    flags |= (mode & MODE_EXCLUSIVE) != 0 ? O_EXCL | O_CREAT : 0;
#ifdef _WIN32
    flags |= _O_BINARY;
#endif
    return flags;
}

// Open a file with C runtime flags; -1 with errno set on failure.
int open_file(const std::filesystem::path &path, int flags) {
#ifdef _WIN32
    int fd = -1;
    const auto error = _wsopen_s(&fd, path.c_str(), flags, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    errno = error;
    return error == 0 ? fd : -1;
#else
    return ::open(path.c_str(), flags | O_CLOEXEC, 0666);
#endif
}

// The C runtime's read, write, seek and close of a descriptor.
std::int64_t read_fd(int fd, std::span<std::byte> buffer) {
#ifdef _WIN32
    // One call reads at most 1 GiB: the count is an unsigned int here.
    return _read(fd, buffer.data(), static_cast<unsigned>(std::min<std::size_t>(buffer.size(), std::size_t{1} << 30)));
#else
    return ::read(fd, buffer.data(), buffer.size());
#endif
}

std::int64_t write_fd(int fd, std::span<const std::byte> bytes) {
#ifdef _WIN32
    return _write(fd, bytes.data(), static_cast<unsigned>(bytes.size()));
#else
    return ::write(fd, bytes.data(), bytes.size());
#endif
}

std::int64_t seek_fd(int fd, std::int64_t offset, int whence) {
#ifdef _WIN32
    return _lseeki64(fd, offset, whence);
#else
    return ::lseek(fd, offset, whence);
#endif
}

void close_fd(int fd) {
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
}

// Write all of `bytes`; false with errno set on failure.
bool write_all(int fd, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto written = write_fd(fd, bytes);
        if (written < 0) {
            return false;
        }
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return true;
}

// The size of an open descriptor's file, 0 when it has none to report.
std::size_t file_size(int fd) {
#ifdef _WIN32
    struct _stat64 status{};
    return _fstat64(fd, &status) == 0 ? static_cast<std::size_t>(status.st_size) : 0;
#else
    struct stat status{};
    return fstat(fd, &status) == 0 ? static_cast<std::size_t>(status.st_size) : 0;
#endif
}

// The success reply of the whole of an open descriptor, read straight into the reply after its status byte: sized
// from the file's size, never zeroed first (OPT3.4 file I/O), and grown only when the file grew, so the bytes are
// copied once (owned binaries).
std::optional<Bytes> read_all(int fd) {
    Bytes reply(1 + std::max<std::size_t>(file_size(fd), CHUNK));
    reply[0] = std::byte{0};
    std::size_t filled = 1;
    for (;;) {
        // A full reply probes for the end with a small read before growing: a file of the expected size never moves.
        std::array<std::byte, CHUNK> probe{};
        const auto full = filled == reply.size();
        const auto read = full ? read_fd(fd, probe) : read_fd(fd, std::span(reply).subspan(filled));
        if (read < 0) {
            return std::nullopt;
        }
        if (read == 0) {
            reply.resize(filled);
            return reply;
        }
        if (full) {
            reply.insert(reply.end(), probe.begin(), probe.begin() + read);
        }
        filled += static_cast<std::size_t>(read);
    }
}

// The error reply of a path that names a directory where a file is needed, if it does.
std::optional<Bytes> directory(const std::filesystem::path &path) {
    std::error_code code;
    return std::filesystem::is_directory(path, code) ? std::optional{failure("eisdir")} : std::nullopt;
}

// read_file: the whole file.
Bytes read_file(std::span<const std::byte> bytes) {
    const auto path = path_of(bytes);
    if (auto refused = directory(path)) {
        return *refused;
    }
    const auto fd = open_file(path, open_flags(MODE_READ));
    if (fd < 0) {
        return errno_failure();
    }
    auto content = read_all(fd);
    close_fd(fd);
    return content ? std::move(*content) : errno_failure();
}

// write_file: replace the file's content.
Bytes write_file(std::span<const std::byte> bytes) {
    const auto [name, data] = split(bytes);
    const auto path = path_of(name);
    if (auto refused = directory(path)) {
        return *refused;
    }
    const auto fd = open_file(path, open_flags(MODE_WRITE));
    if (fd < 0) {
        return errno_failure();
    }
    const auto reply = write_all(fd, data) ? ok() : errno_failure();
    close_fd(fd);
    return reply;
}

// The reply of a filesystem operation's error code.
Bytes done(std::error_code code) { return code ? failure(reason(code)) : ok(); }

// list_dir: the names of a directory's entries, each followed by a zero byte.
Bytes list_dir(std::span<const std::byte> bytes) {
    std::error_code code;
    Bytes names;
    for (std::filesystem::directory_iterator entry(path_of(bytes), code), end; !code && entry != end;
         entry.increment(code)) {
        const auto name = bytes_of(entry->path().filename());
        names.insert(names.end(), name.begin(), name.end());
        names.push_back(std::byte{0});
    }
    return code ? failure(reason(code)) : ok(names);
}

// delete: a file, never a directory (eperm, as OTP).
Bytes remove_file(std::span<const std::byte> bytes) {
    std::error_code code;
    if (std::filesystem::is_directory(path_of(bytes), code)) {
        return failure("eperm");
    }
    return std::filesystem::remove(path_of(bytes), code) || code ? done(code) : failure("enoent");
}

// rename: the old path, then the new one.
Bytes rename_path(std::span<const std::byte> bytes) {
    std::error_code code;
    const auto [from, to] = split(bytes);
    std::filesystem::rename(path_of(from), path_of(to), code);
    return done(code);
}

// make_dir: a new directory; eexist when the path exists.
Bytes make_dir(std::span<const std::byte> bytes) {
    std::error_code code;
    return std::filesystem::create_directory(path_of(bytes), code) || code ? done(code) : failure("eexist");
}

// del_dir: an empty directory; enoent for a missing path and enotdir for any other file, as OTP.
Bytes delete_dir(std::span<const std::byte> bytes) {
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path_of(bytes), code);
    if (!std::filesystem::exists(status)) {
        return failure("enoent");
    }
    if (!std::filesystem::is_directory(status)) {
        return failure("enotdir");
    }
    std::filesystem::remove(path_of(bytes), code);
    return done(code);
}

// read_file_info: the path's status record (file_info.hpp). Added for step 41Y2.
Bytes read_file_info(std::span<const std::byte> bytes) {
    const auto status = file_status(path_of(bytes));
    return status ? ok(*status) : failure(reason(std::error_code(status.error(), std::generic_category())));
}

// The reply of a directory or link operation (file_paths.hpp, step 41P).
Bytes path_reply(const PathReply &result) { return result ? ok(*result) : failure(reason(result.error())); }

Bytes get_cwd(std::span<const std::byte> bytes) { return path_reply(current_directory(bytes)); }

Bytes set_cwd(std::span<const std::byte> bytes) { return path_reply(change_directory(path_of(bytes))); }

Bytes read_link(std::span<const std::byte> bytes) { return path_reply(link_target(path_of(bytes))); }

Bytes read_link_info(std::span<const std::byte> bytes) { return path_reply(link_status(path_of(bytes))); }

// make_link and make_symlink: the existing path, then the new link.
Bytes make_link(std::span<const std::byte> bytes) {
    const auto [existing, link] = split(bytes);
    return path_reply(make_hard_link(path_of(existing), path_of(link)));
}

Bytes make_symlink(std::span<const std::byte> bytes) {
    const auto [existing, link] = split(bytes);
    return path_reply(make_symbolic_link(path_of(existing), path_of(link)));
}

// The path operations, which need no open file, from read_file on in FileOperation order.
constexpr std::array<Bytes (*)(std::span<const std::byte>), 14> PATH_OPERATIONS{
    read_file,      write_file, remove_file, rename_path, list_dir,       make_dir,  delete_dir,
    read_file_info, get_cwd,    set_cwd,     read_link,   read_link_info, make_link, make_symlink};

// Run a path operation.
Bytes path_operation(FileOperation operation, std::span<const std::byte> bytes) {
    const auto index = static_cast<std::size_t>(operation) - static_cast<std::size_t>(FileOperation::read_file);
    return PATH_OPERATIONS.at(index)(bytes);
}

class FileDriver final : public PortDriver {
  public:
    FileDriver() = default;
    FileDriver(const FileDriver &) = delete;
    FileDriver &operator=(const FileDriver &) = delete;
    FileDriver(FileDriver &&) = delete;
    FileDriver &operator=(FileDriver &&) = delete;

    // Close a file still open when the port closes.
    ~FileDriver() override {
        if (fd_ >= 0) {
            close_fd(fd_);
        }
    }

    std::expected<void, DriverError> write(std::span<const std::byte>) override {
        return std::unexpected(DriverError{"einval"});
    }

    bool controllable() const noexcept override { return true; }

    std::optional<Bytes> control(std::uint32_t operation, std::span<const std::byte> bytes, Word) override {
        const auto kind = static_cast<FileOperation>(operation);
        if (operation >= static_cast<std::uint32_t>(FileOperation::read_file) &&
            operation <= static_cast<std::uint32_t>(FileOperation::make_symlink)) {
            return path_operation(kind, bytes);
        }
        const Turn turn(*this);
        // Any operation but read_line first gives the file back the bytes read_line read ahead.
        if (kind != FileOperation::read_line) {
            give_back();
        }
        return file_operation(kind, bytes);
    }

  private:
    // Holds the open file for one operation: another caller waits until it ends.
    class Turn {
      public:
        explicit Turn(FileDriver &driver) : driver_(driver) {
            while (driver_.busy_.test_and_set(std::memory_order_acquire)) {
                driver_.busy_.wait(true, std::memory_order_relaxed);
            }
        }

        Turn(const Turn &) = delete;
        Turn &operator=(const Turn &) = delete;
        Turn(Turn &&) = delete;
        Turn &operator=(Turn &&) = delete;

        // Give the file to the next waiting caller.
        ~Turn() {
            driver_.busy_.clear(std::memory_order_release);
            driver_.busy_.notify_one();
        }

      private:
        // The driver whose open file this operation holds.
        FileDriver &driver_;
    };

    // An operation on the open file; none for an unknown operation.
    std::optional<Bytes> file_operation(FileOperation operation, std::span<const std::byte> bytes) {
        switch (operation) {
        case FileOperation::open:
            return open(bytes);
        case FileOperation::read:
            return read(static_cast<std::size_t>(number(bytes, 0, 8)));
        case FileOperation::write:
            return mode_allows(MODE_WRITE | MODE_APPEND) ? (write_all(fd_, bytes) ? ok() : errno_failure())
                                                         : failure("ebadf");
        case FileOperation::position:
            return position(bytes);
        case FileOperation::read_line:
            return mode_allows(MODE_READ) ? read_line() : failure("ebadf");
        case FileOperation::close:
            close_fd(std::exchange(fd_, -1));
            return ok();
        default:
            return std::nullopt;
        }
    }

    // open: the mode byte, then the path.
    Bytes open(std::span<const std::byte> bytes) {
        if (bytes.empty() || fd_ >= 0) {
            return failure("einval");
        }
        mode_ = std::to_integer<unsigned>(bytes[0]);
        const auto path = path_of(bytes.subspan(1));
        if (auto refused = directory(path)) {
            return *refused;
        }
        fd_ = open_file(path, open_flags(mode_));
        return fd_ < 0 ? errno_failure() : ok();
    }

    // Whether the file was opened for one of the `modes`; reading is the default mode.
    bool mode_allows(unsigned modes) const noexcept {
        const auto mode = (mode_ & (MODE_READ | MODE_WRITE | MODE_APPEND)) == 0 ? MODE_READ : mode_;
        return (mode & modes) != 0;
    }

    // read: up to `size` bytes, or end of file.
    Bytes read(std::size_t size) {
        if (!mode_allows(MODE_READ)) {
            return failure("ebadf");
        }
        // The bytes go straight into the reply after its status byte (owned binaries).
        Bytes reply(1 + size);
        reply[0] = std::byte{0};
        const auto read = read_fd(fd_, std::span(reply).subspan(1));
        if (read < 0) {
            return errno_failure();
        }
        if (read == 0 && size > 0) {
            return end_of_file();
        }
        reply.resize(1 + static_cast<std::size_t>(read));
        return reply;
    }

    // read_line: the next line with its newline, the rest of the file without one, or end of file. Lines come out
    // of a read-ahead buffer, refilled a chunk at a time, so consecutive lines cost no system call each (read-ahead
    // lines); the file position as other operations see it is just past the line.
    Bytes read_line() {
        Bytes reply;
        for (;;) {
            if (take_line(reply)) {
                return reply;
            }
            const auto read = refill();
            if (read < 0) {
                return errno_failure();
            }
            if (read == 0) {
                return reply.empty() ? end_of_file() : reply;
            }
        }
    }

    // Read the next chunk ahead; the bytes read, or a negative count after an error.
    std::int64_t refill() {
        ahead_.resize(LINE_AHEAD);
        const auto read = read_fd(fd_, ahead_);
        ahead_.resize(read < 0 ? 0 : static_cast<std::size_t>(read));
        ahead_at_ = 0;
        return read;
    }

    // Move buffered bytes up to and including a newline to a line's success reply, which the first of them start with
    // its status byte, sized to them (a line read in one piece costs one allocation); true once the line ended.
    bool take_line(Bytes &reply) {
        const auto rest = std::span(ahead_).subspan(ahead_at_);
        const auto newline = std::ranges::find(rest, std::byte{'\n'});
        const auto end = newline == rest.end() ? rest.end() : newline + 1;
        const auto size = static_cast<std::size_t>(end - rest.begin());
        if (reply.empty() && size > 0) {
            reply.reserve(1 + size);
            reply.push_back(std::byte{0});
        }
        reply.insert(reply.end(), rest.begin(), end);
        ahead_at_ += size;
        return newline != rest.end();
    }

    // Seek back over the bytes read_line read ahead but did not return, so the file is where its caller expects.
    void give_back() {
        if (ahead_at_ < ahead_.size() && fd_ >= 0) {
            seek_fd(fd_, -static_cast<std::int64_t>(ahead_.size() - ahead_at_), SEEK_CUR);
        }
        ahead_.clear();
        ahead_at_ = 0;
    }

    // position: the whence byte (0 from the start, 1 from the current position, 2 from the end) and a signed
    // 64-bit offset; the new position.
    Bytes position(std::span<const std::byte> bytes) {
        const auto whence = bytes.empty() ? 0 : std::to_integer<int>(bytes[0]);
        const auto offset = std::bit_cast<std::int64_t>(number(bytes, 1, 8));
        const auto current = seek_fd(fd_, 0, SEEK_CUR);
        const auto base = origin(whence, current);
        if (current < 0 || base + offset < 0) {
            return failure("einval");
        }
        const auto moved = seek_fd(fd_, base + offset, SEEK_SET);
        if (moved < 0) {
            return errno_failure();
        }
        std::array<std::byte, 8> result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[7 - index] = static_cast<std::byte>((static_cast<std::uint64_t>(moved) >> (8 * index)) & 0xff);
        }
        return ok(result);
    }

    // The position a whence byte counts from: 0 the start, 1 `current`, 2 the end; the file stays at `current`.
    std::int64_t origin(int whence, std::int64_t current) {
        if (whence != 2) {
            return whence == 1 ? current : 0;
        }
        const auto end = seek_fd(fd_, 0, SEEK_END);
        seek_fd(fd_, current, SEEK_SET);
        return end;
    }

    // Set while one process's operation on the open file runs; another process waits for it to clear.
    std::atomic_flag busy_;
    // The open file, or -1, and the mode byte it was opened with.
    int fd_ = -1;
    unsigned mode_ = 0;
    // Bytes read_line read past the lines it returned, and the next one to return (read-ahead lines).
    Bytes ahead_;
    std::size_t ahead_at_ = 0;
};
} // namespace

std::unique_ptr<PortDriver> file_driver() { return std::make_unique<FileDriver>(); }
} // namespace clause::runtime::detail
