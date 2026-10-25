#pragma once
#include "input.hpp"
#include <byte_buffer.hpp>
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <signals.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Ports (docs/ports.md): the port record the executor keeps in its port table and the interface of the drivers
// behind ports.
namespace clause::runtime::detail {
// A failed driver operation: the POSIX error name a port closes with or an operation reports (epipe, enoent, ...).
struct DriverError final {
    std::string reason;
};

// An operating system handle: a HANDLE on Windows, a file descriptor elsewhere; -1 is none.
using NativeHandle = std::intptr_t;

// A handle the I/O thread reads or writes for a port: whether it closes it when done, and whether Windows can
// overlap its I/O (pipes the runtime created); a handle it cannot overlap is read by a blocking thread.
struct Descriptor final {
    NativeHandle handle = -1;
    bool owned = false;
    bool overlapped = false;
};

// The native handle of a C runtime descriptor (the descriptor itself outside Windows).
NativeHandle native_descriptor(int fd) noexcept;

// The resource behind one port. The executor calls a driver under its mutex; a driver never calls back into it.
class PortDriver {
  public:
    PortDriver() = default;
    PortDriver(const PortDriver &) = delete;
    PortDriver &operator=(const PortDriver &) = delete;
    PortDriver(PortDriver &&) = delete;
    PortDriver &operator=(PortDriver &&) = delete;
    // Release the resource; a closed port's driver is destroyed at once.
    virtual ~PortDriver() = default;
    // Write one output of the port (port_command, {command, Data}), framing already applied.
    virtual std::expected<void, DriverError> write(std::span<const std::byte> bytes) = 0;

    // The handle the I/O thread reads the port's input from, if the driver has one.
    virtual std::optional<Descriptor> input() const noexcept { return std::nullopt; }

    // The handle the I/O thread writes queued output to; none when write() writes at once.
    virtual std::optional<Descriptor> queued_output() const noexcept { return std::nullopt; }

    // The spawned program whose exit the I/O thread reports (a process handle or pid), if any.
    virtual std::optional<std::int64_t> child() const noexcept { return std::nullopt; }

    // The operating system process id of a spawned program, for port_info's os_pid.
    virtual std::optional<std::int64_t> os_pid() const noexcept { return std::nullopt; }

    // Whether port_control/3 works on this driver; others make it badarg.
    virtual bool controllable() const noexcept { return false; }

    // Answer port_control(Port, Operation, Data) of the process `caller` (a pid word); none for an operation the
    // driver does not have (badarg). Called without the executor's lock, possibly from two workers at once.
    virtual std::optional<ByteBuffer> control(std::uint32_t /*operation*/, std::span<const std::byte>,
                                              Word /*caller*/) {
        return std::nullopt;
    }

    // Learn the port's word once the port exists; drivers that report events need it.
    virtual void attach(Word /*port*/) noexcept {}
};

// One open port of the executor's port table.
struct Port final {
    // The port's number (its word is port_word(number)).
    Word number = 0;
    // The connected process: its opener, or the process it was connected to since.
    Word connected = 0;
    // Pid words of linked processes, oldest link first.
    std::vector<Word> links;
    // Monitors held on the port: reference to the monitoring process.
    Signals::Monitors watchers;
    // Registered name (an atom word), or 0.
    Word name = 0;
    // Name reported by port_info/2: the command, file or "In/Out" of the port.
    std::string spelling;
    PortOptions options;
    // Bytes read from and written to the port.
    std::size_t input = 0;
    std::size_t output = 0;
    // Input ended (or the port reads none) while option exit_status waits for the program's status, which is kept
    // here once it exited: {exit_status, S} goes out before the end of input is acted on.
    bool input_ended = false;
    std::optional<std::int64_t> exit_status;
    // Shared so port_control/3 can run outside the executor's lock while the port may close meanwhile.
    std::shared_ptr<PortDriver> driver;
    // The port's task (docs/ports.md#port-tasks): input and events handed over but not acted on yet, oldest first;
    // the framing of its input; units framed but not delivered yet; and whether it waits in the port queue.
    std::deque<PortWork> pending;
    InputDecoder decoder{PortOptions{}};
    std::deque<PortInput> units;
    bool queued = false;
    // Raw input bytes in `pending`, and whether the I/O thread was told to stop reading because they are too many.
    std::size_t held_bytes = 0;
    bool reading_paused = false;
    // Set while the task waits for its connected process to take the messages it has (docs/ports.md#busy-ports).
    bool throttled = false;
    // Output queued for the I/O thread and not written yet; busy from the high limit until below the low one.
    std::size_t queued_output = 0;
    bool busy = false;
    // Pids of the processes suspended until the port is no longer busy, oldest first.
    std::vector<Word> suspended;
};

// Apply the output framing of `options` to `bytes`: a {packet, N} header; nothing for stream and line ports.
// Empty when the data is too long for the header.
std::optional<std::vector<std::byte>> framed(const PortOptions &options, std::vector<std::byte> bytes);

// A request to run a program: {spawn, Command} runs Command through the system's command processor rules (a
// shell command line, or on Windows a command line CreateProcess searches), {spawn_executable, File} runs File
// with the {args, ...} of the options.
struct SpawnRequest final {
    bool executable = false;
    std::string command;
};

// The driver of a spawned program, its stdin and stdout pipes owned by the I/O thread once registered; the POSIX
// reason (enoent, eacces, ...) when the program cannot be started (docs/ports.md#drivers).
std::expected<std::unique_ptr<PortDriver>, DriverError> spawn_driver(const SpawnRequest &request,
                                                                     const PortOptions &options);

// The driver of {spawn_driver, "clause_file"}: files of the project library's file module, through the
// port_control/3 protocol of ports/file.cpp.
std::unique_ptr<PortDriver> file_driver();

#ifdef _WIN32
// The driver of {spawn_driver, "clause_registry"}: win32reg's registry access (ports/registry_windows.cpp).
std::unique_ptr<PortDriver> registry_driver();
#endif

// The driver of an {fd, In, Out} port: input read from In by the I/O thread, output written at once to Out
// (docs/ports.md#drivers).
std::unique_ptr<PortDriver> fd_driver(int in, int out);
} // namespace clause::runtime::detail
