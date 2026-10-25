#include "file_changes.hpp"
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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
    // Flushing to the device (plan 21 step LIB23): a flag byte, 1 for the data only.
    sync = 7,
    // Cutting the file at its position and access advice (plan 21 step LIB50).
    truncate = 8,
    advise = 9,
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
    // A path's owner, permissions and times (plan 21 step LIB50).
    set_owner = 24,
    set_permissions = 25,
    set_times = 26,
    // Reads and writes at an offset that keep the position, and reserving space (plan 21 step LIB50).
    pread = 30,
    pwrite = 31,
    allocate = 32,
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

// The C runtime's close of a descriptor.
void close_fd(int fd) {
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
}

#ifdef _WIN32
// The CreateFileW disposition of C runtime flags.
DWORD disposition(int flags) {
    if ((flags & O_CREAT) != 0) {
        if ((flags & O_EXCL) != 0) {
            return CREATE_NEW;
        }
        return (flags & O_TRUNC) != 0 ? CREATE_ALWAYS : OPEN_ALWAYS;
    }
    return (flags & O_TRUNC) != 0 ? TRUNCATE_EXISTING : OPEN_EXISTING;
}

// The CreateFileW access of C runtime flags.
DWORD desired_access(int flags) {
    switch (flags & (O_RDONLY | O_WRONLY | O_RDWR)) {
    case O_WRONLY:
        return GENERIC_WRITE;
    case O_RDWR:
        return GENERIC_READ | GENERIC_WRITE;
    default:
        return GENERIC_READ;
    }
}
#endif

// Open a file with C runtime flags; -1 with errno set on failure. On Windows the file is shared for reading, writing
// and deletion, as OTP opens files (plan 21 step LIB55), so an open file can be renamed or deleted.
int system_open(const std::filesystem::path &path, int flags) {
#ifdef _WIN32
    const auto handle =
        CreateFileW(path.c_str(), desired_access(flags), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, disposition(flags), FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const std::error_code error(static_cast<int>(GetLastError()), std::system_category());
        const auto condition = error.default_error_condition();
        errno = condition.category() == std::generic_category() ? condition.value() : EINVAL;
        return -1;
    }
    const auto fd =
        _open_osfhandle(reinterpret_cast<intptr_t>(handle), flags & (_O_APPEND | _O_RDONLY | _O_WRONLY | _O_RDWR));
    if (fd < 0) {
        CloseHandle(handle);
        errno = EMFILE;
    }
    return fd;
#else
    return ::open(path.c_str(), flags | O_CLOEXEC, 0666);
#endif
}

// Whether a path names a directory; errno is kept.
bool names_directory(const std::filesystem::path &path) {
    const auto saved = errno;
    std::error_code code;
    const auto directory = std::filesystem::is_directory(path, code);
    errno = saved;
    return directory;
}

// Whether an open descriptor is a directory, which only POSIX opens (for reading).
bool directory_descriptor([[maybe_unused]] int fd) {
#ifdef _WIN32
    return false;
#else
    struct stat status{};
    return fstat(fd, &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

// Open a file, never a directory: -1 with errno set on failure, EISDIR for a directory, as OTP. The path's status is
// read only once the open failed, or a directory opened for reading (path operation diet).
int open_file(const std::filesystem::path &path, int flags) {
    const auto fd = system_open(path, flags);
    if (fd < 0) {
        if (names_directory(path)) {
            errno = EISDIR;
        }
        return -1;
    }
    if ((flags & (O_WRONLY | O_RDWR)) == 0 && directory_descriptor(fd)) {
        close_fd(fd);
        errno = EISDIR;
        return -1;
    }
    return fd;
}

// The C runtime's read, write and seek of a descriptor.
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

// Flush a descriptor's file to its device, its data only when `data_only` and POSIX can; false with errno set.
// Created for file:sync/1 and datasync/1 (plan 21 step LIB23).
bool sync_fd(int fd, [[maybe_unused]] bool data_only) {
#ifdef _WIN32
    return _commit(fd) == 0;
#elif defined(__APPLE__)
    return ::fsync(fd) == 0;
#else
    return (data_only ? ::fdatasync(fd) : ::fsync(fd)) == 0;
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

// The success reply of the whole of an open descriptor, read straight into the reply after its status byte, never
// zeroed first (OPT3.4 file I/O), so the bytes are copied once (owned binaries). As OTP, a file of known size is read
// up to that size with no read probing for its end (path operation diet); a file of no reported size is read until
// a read gives nothing, its buffer doubling.
std::optional<Bytes> read_all(int fd) {
    const auto size = file_size(fd);
    Bytes reply(1 + (size == 0 ? CHUNK : size));
    reply[0] = std::byte{0};
    std::size_t filled = 1;
    for (;;) {
        const auto read = read_fd(fd, std::span(reply).subspan(filled));
        if (read < 0) {
            return std::nullopt;
        }
        filled += static_cast<std::size_t>(read);
        if (read == 0 || filled == 1 + size) {
            reply.resize(filled);
            return reply;
        }
        if (filled == reply.size()) {
            reply.resize(2 * reply.size());
        }
    }
}

// read_file: the whole file.
Bytes read_file(std::span<const std::byte> bytes) {
    const auto path = path_of(bytes);
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
    const auto fd = open_file(path_of(name), open_flags(MODE_WRITE));
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

// The C runtime's unlink of a file; 0, or -1 with errno set on failure (a directory fails).
int unlink_file(const std::filesystem::path &path) {
#ifdef _WIN32
    return _wunlink(path.c_str());
#else
    return ::unlink(path.c_str());
#endif
}

// delete: a file, never a directory (eperm, as OTP); the path's status is read only once the delete failed (path
// operation diet).
Bytes remove_file(std::span<const std::byte> bytes) {
    const auto path = path_of(bytes);
    if (unlink_file(path) == 0) {
        return ok();
    }
    return names_directory(path) ? failure("eperm") : errno_failure();
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

// A 32-bit big-endian signed integer at `at`.
std::int32_t signed32(std::span<const std::byte> bytes, std::size_t at) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(number(bytes, at, 4)));
}

// A 64-bit big-endian signed integer at `at`.
std::int64_t signed64(std::span<const std::byte> bytes, std::size_t at) {
    return std::bit_cast<std::int64_t>(number(bytes, at, 8));
}

// The path after a fixed-size head of `size` bytes.
std::filesystem::path path_after(std::span<const std::byte> bytes, std::size_t size) {
    return path_of(bytes.subspan(std::min(size, bytes.size())));
}

// set_owner: the 32-bit user and group ids (-1 keeps one), then the path (plan 21 step LIB50).
Bytes owner_operation(std::span<const std::byte> bytes) {
    return done(set_owner(path_after(bytes, 8), FileOwner{signed32(bytes, 0), signed32(bytes, 4)}));
}

// set_permissions: the 32-bit mode, then the path (plan 21 step LIB50).
Bytes permissions_operation(std::span<const std::byte> bytes) {
    return done(set_permissions(path_after(bytes, 4), static_cast<std::uint32_t>(number(bytes, 0, 4))));
}

// set_times: the access, modification and creation times as 64-bit POSIX seconds, then the path (plan 21 step LIB50).
Bytes times_operation(std::span<const std::byte> bytes) {
    return done(
        set_times(path_after(bytes, 24), FileTimes{signed64(bytes, 0), signed64(bytes, 8), signed64(bytes, 16)}));
}

// The path operations, which need no open file, from read_file on in FileOperation order.
constexpr std::array<Bytes (*)(std::span<const std::byte>), 17> PATH_OPERATIONS{
    read_file,      write_file,   remove_file,     rename_path,
    list_dir,       make_dir,     delete_dir,      read_file_info,
    get_cwd,        set_cwd,      read_link,       read_link_info,
    make_link,      make_symlink, owner_operation, permissions_operation,
    times_operation};

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
            operation <= static_cast<std::uint32_t>(FileOperation::set_times)) {
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

    // A handler of an operation on the open file.
    using Handler = Bytes (FileDriver::*)(std::span<const std::byte>);

    // An operation on the open file; none for an unknown operation.
    std::optional<Bytes> file_operation(FileOperation operation, std::span<const std::byte> bytes) {
        static constexpr std::array<std::pair<FileOperation, Handler>, 12> OPERATIONS{{
            {FileOperation::open, &FileDriver::open},
            {FileOperation::read, &FileDriver::read_operation},
            {FileOperation::write, &FileDriver::write_bytes},
            {FileOperation::position, &FileDriver::position},
            {FileOperation::read_line, &FileDriver::read_line_operation},
            {FileOperation::close, &FileDriver::close_operation},
            {FileOperation::sync, &FileDriver::sync},
            {FileOperation::truncate, &FileDriver::truncate_operation},
            {FileOperation::advise, &FileDriver::advise_operation},
            {FileOperation::allocate, &FileDriver::allocate_operation},
            {FileOperation::pread, &FileDriver::pread_operation},
            {FileOperation::pwrite, &FileDriver::pwrite_operation},
        }};
        const auto found = std::ranges::find(OPERATIONS, operation, &std::pair<FileOperation, Handler>::first);
        if (found == OPERATIONS.end()) {
            return std::nullopt;
        }
        return (this->*found->second)(bytes);
    }

    // read: a 64-bit size.
    Bytes read_operation(std::span<const std::byte> bytes) {
        return read(static_cast<std::size_t>(number(bytes, 0, 8)));
    }

    Bytes read_line_operation(std::span<const std::byte>) {
        return mode_allows(MODE_READ) ? read_line() : failure("ebadf");
    }

    Bytes close_operation(std::span<const std::byte>) {
        close_fd(std::exchange(fd_, -1));
        return ok();
    }

    // truncate, advise (a range and an advice byte) and allocate (a range): plan 21 step LIB50.
    Bytes truncate_operation(std::span<const std::byte>) { return done(truncate_at_position(fd_)); }

    Bytes advise_operation(std::span<const std::byte> bytes) {
        advise_access(fd_, range(bytes), bytes.size() > 16 ? std::to_integer<unsigned>(bytes[16]) : 0);
        return ok();
    }

    Bytes allocate_operation(std::span<const std::byte> bytes) { return done(allocate_space(fd_, range(bytes))); }

    // pread: a 64-bit offset and size; pwrite: a 64-bit offset and the bytes (plan 21 step LIB50).
    Bytes pread_operation(std::span<const std::byte> bytes) {
        return at_offset(signed64(bytes, 0), [&] { return read(static_cast<std::size_t>(number(bytes, 8, 8))); });
    }

    Bytes pwrite_operation(std::span<const std::byte> bytes) {
        return at_offset(signed64(bytes, 0),
                         [&] { return write_bytes(bytes.subspan(std::min<std::size_t>(8, bytes.size()))); });
    }

    // A signed 64-bit offset and length (advise and allocate, plan 21 step LIB50).
    static FileRange range(std::span<const std::byte> bytes) {
        return FileRange{signed64(bytes, 0), signed64(bytes, 8)};
    }

    // Run a read or write at `offset` and move back to where the file was (pread, pwrite: plan 21 step LIB50).
    template <typename Operation> Bytes at_offset(std::int64_t offset, Operation operation) {
        const auto current = seek_fd(fd_, 0, SEEK_CUR);
        if (current < 0 || seek_fd(fd_, offset, SEEK_SET) < 0) {
            return errno_failure();
        }
        auto reply = operation();
        seek_fd(fd_, current, SEEK_SET);
        return reply;
    }

    // write: the bytes, when the file was opened for writing.
    Bytes write_bytes(std::span<const std::byte> bytes) {
        if (!mode_allows(MODE_WRITE | MODE_APPEND)) {
            return failure("ebadf");
        }
        return write_all(fd_, bytes) ? ok() : errno_failure();
    }

    // sync: the flag byte, 1 to flush the data only. Created for file:sync/1 and datasync/1 (plan 21 step LIB23).
    Bytes sync(std::span<const std::byte> bytes) {
        const bool data_only = !bytes.empty() && bytes[0] != std::byte{0};
        return sync_fd(fd_, data_only) ? ok() : errno_failure();
    }

    // open: the mode byte, then the path.
    Bytes open(std::span<const std::byte> bytes) {
        if (bytes.empty() || fd_ >= 0) {
            return failure("einval");
        }
        mode_ = std::to_integer<unsigned>(bytes[0]);
        fd_ = open_file(path_of(bytes.subspan(1)), open_flags(mode_));
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
