#include "typed.hpp"
#include <array>
#include <optional>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// The terminal queries of clause_io (plan 21 step LIB63, docs/builtins.md#io-module): whether a standard stream is a
// terminal, and the window size of one, as OTP's prim_tty answers them for io:getopts/0,1, io:columns/0,1,
// io:rows/0,1 and io_ansi:enabled/0,1.
namespace clause::runtime::builtins {
namespace {
// The standard stream an atom names: 0 stdin, 1 stdout, 2 stderr; badarg for any other.
int stream_number(const AtomArgument &stream) {
    constexpr std::array<std::string_view, 3> NAMES{"stdin", "stdout", "stderr"};
    for (std::size_t index = 0; index < NAMES.size(); ++index) {
        if (stream.spelling == NAMES.at(index)) {
            return static_cast<int>(index);
        }
    }
    bad_argument();
}

#ifdef _WIN32
HANDLE stream_handle(int number) {
    constexpr std::array<DWORD, 3> HANDLES{STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    return GetStdHandle(HANDLES.at(static_cast<std::size_t>(number)));
}
#endif

// Whether the stream is a terminal: a console on Windows.
bool terminal(int number) {
#ifdef _WIN32
    const auto handle = stream_handle(number);
    DWORD mode = 0;
    return handle != INVALID_HANDLE_VALUE && GetFileType(handle) == FILE_TYPE_CHAR && GetConsoleMode(handle, &mode);
#else
    return ::isatty(number) == 1;
#endif
}

// The columns and rows of the terminal window of the stream, if it is a terminal.
std::optional<std::pair<int, int>> window(int number) {
    if (!terminal(number)) {
        return std::nullopt;
    }
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(stream_handle(number), &info)) {
        return std::nullopt;
    }
    return std::pair{info.srWindow.Right - info.srWindow.Left + 1, info.srWindow.Bottom - info.srWindow.Top + 1};
#else
    winsize size{};
    if (::ioctl(number, TIOCGWINSZ, &size) != 0 || size.ws_col == 0) {
        return std::nullopt;
    }
    return std::pair{static_cast<int>(size.ws_col), static_cast<int>(size.ws_row)};
#endif
}

// isatty(Stream): whether stdin, stdout or stderr is a terminal.
TermResult<Term> isatty(ProcessContext &context, const AtomArgument &stream) {
    return TermFactory(context).boolean(terminal(stream_number(stream)));
}

// window_size(Stream): {Columns, Rows} of a terminal stream, or {error, enotsup}.
TermResult<Term> window_size(ProcessContext &context, const AtomArgument &stream) {
    TermFactory factory(context);
    const auto size = window(stream_number(stream));
    if (!size) {
        const std::array error{atom(context, "error"_atom), atom(context, "enotsup"_atom)};
        return factory.tuple(error);
    }
    const std::array parts{need(factory.integer(size->first)), need(factory.integer(size->second))};
    return factory.tuple(parts);
}

constexpr std::array CLAUSE_IO_BUILTINS{
    typed_entry<isatty>("clause_io", "isatty"),
    typed_entry<window_size>("clause_io", "window_size"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> clause_io_builtins() noexcept { return builtins::CLAUSE_IO_BUILTINS; }
} // namespace clause::runtime
