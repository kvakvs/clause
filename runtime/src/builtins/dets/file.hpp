#pragma once
#include "format.hpp"
#include <string>

// The file of a dets table (plan 21 step LIB53): positioned reads and writes on an open descriptor, or on bytes in
// memory for a ram_file table, which are written out whole when the table is saved. Every failure throws Failure.
namespace clause::runtime::builtins::dets {
// An error of the engine, turned into OTP's reason term where a builtin returns.
struct Failure {
    enum class Shape : std::uint8_t {
        // {Reason, FileName}
        named,
        // {file_error, FileName, Posix}
        file_error,
        // {{bad_object, Where}, FileName}
        bad_object,
        // Reason alone
        bare,
    };
    Shape shape;
    std::string reason;
    std::string file;
};

[[noreturn]] void fail(Failure::Shape shape, std::string reason, const std::string &file);

class File final {
  public:
    enum class Mode : std::uint8_t { read, read_write, create };

    // Open `name` (UTF-8): an existing file to read or update, or a new empty one.
    static File open(const std::string &name, Mode mode);
    // Bytes in memory standing for the file `name`.
    static File memory(const std::string &name, Bytes contents);

    File(File &&other) noexcept;
    File &operator=(File &&other) noexcept;
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    ~File();

    // Up to `size` bytes at `at`, fewer at the end of the file.
    Bytes pread(std::uint64_t at, std::size_t size) const;
    void pwrite(std::uint64_t at, std::span<const std::byte> bytes);
    std::uint64_t size() const;
    void truncate(std::uint64_t at);
    void sync();
    // Write a memory file's bytes to its name; nothing for a file on disk.
    void write_out();
    void close() noexcept;

    const std::string &name() const { return name_; }

    bool in_memory() const { return memory_; }

  private:
    File(std::string name, int descriptor, bool memory, Bytes contents);
    [[noreturn]] void error() const;

    // The name the table was opened with, for error reasons and ram_file saves.
    std::string name_;
    // The C runtime descriptor of a file on disk, -1 when closed or in memory.
    int descriptor_;
    bool memory_;
    // The bytes of a memory file.
    Bytes contents_;
};

// Whether a file of that name exists, and remove or rename one (repairs and compactions write a .TMP file).
bool file_exists(const std::string &name);
void remove_file(const std::string &name);
void rename_file(const std::string &from, const std::string &to);
// The whole of a file, for opening a ram_file table.
Bytes read_whole(const std::string &name);
} // namespace clause::runtime::builtins::dets
