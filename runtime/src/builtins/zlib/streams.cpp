#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include <array>
#include <cstring>
#include <terms.hpp>
#include <type_traits>
#include <utility>
#include <vector>
#include <zlib.h>

// The zlib streams of Clause's private clause_zlib module (plan step 41Q, docs/builtins.md#zlib-module): a stream is
// a handle reference whose buffer holds a Stream, owned by one controlling process; library/erts/zlib.erl answers
// OTP's zlib functions over these builtins. Every builtin runs one stream operation to completion on all its input.
namespace clause::runtime::builtins {
namespace {
// What a stream does now: closed streams reject a new init, as OTP's do.
enum class Mode : std::uint8_t { none, deflate, inflate, closed };

// What inflate does with input after the end of a compressed stream: OTP's eos_behavior.
enum class EndOfStream : std::uint8_t { error = 0, reset = 1, cut = 2 };

// The state of one stream, kept in its handle's buffer; trivially copyable, so it starts as the buffer's bytes.
struct Stream {
    // Tells a stream buffer apart from an atomics array.
    std::uint64_t magic;
    z_stream zstream;
    Mode mode;
    EndOfStream end_of_stream;
    // Set once inflate reached the end of a compressed stream; inflate_end requires it, as OTP's does.
    bool eos_seen;
    // Set when a failed call left input in the stream; ending it then reports data_error, as OTP does.
    bool input_left;
    // The pid word of the controlling process.
    Word owner;
};

static_assert(std::is_trivially_copyable_v<Stream>);

constexpr std::uint64_t STREAM_MAGIC = 0x7A6C69625F737472; // "zlib_str"

// Bytes of output each zlib call may write before it is collected.
constexpr std::size_t CHUNK = 65536;

// Raise error:Reason with the atom `reason`.
[[noreturn]] void fail(ProcessContext &context, std::string_view reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = need(TermFactory(context).atom(reason)).word()};
}

// The pid word of the calling process.
Word self_word(ProcessContext &context) { return need(TermFactory(context).pid(context.identity())).word(); }

// End the zlib state of a stream whose last reference died.
void finalize(std::span<std::byte> bytes) noexcept {
    auto *stream = reinterpret_cast<Stream *>(bytes.data());
    if (stream->mode == Mode::deflate) {
        deflateEnd(&stream->zstream);
    } else if (stream->mode == Mode::inflate) {
        inflateEnd(&stream->zstream);
    }
    stream->mode = Mode::closed;
}

// The stream of a handle the calling process controls; badarg for anything else, not_on_controlling_process for a
// stream of another process.
Stream &stream_of(ProcessContext &context, const Term &handle) {
    const auto buffer = detail::HandleAccess::buffer(handle);
    if (!buffer || (*buffer)->size() != sizeof(Stream)) {
        bad_argument();
    }
    // Only the controlling process changes the stream, so no two calls touch it at once.
    auto *stream = reinterpret_cast<Stream *>(const_cast<std::byte *>((*buffer)->data()));
    if (stream->magic != STREAM_MAGIC) {
        bad_argument();
    }
    if (stream->owner != self_word(context)) {
        fail(context, "not_on_controlling_process");
    }
    return *stream;
}

// The stream in `mode`; not_initialized otherwise.
Stream &active(ProcessContext &context, const Term &handle, Mode mode) {
    auto &stream = stream_of(context, handle);
    if (stream.mode != mode) {
        fail(context, "not_initialized");
    }
    return stream;
}

// A stream that may start deflating or inflating; already_initialized otherwise.
Stream &idle(ProcessContext &context, const Term &handle) {
    auto &stream = stream_of(context, handle);
    if (stream.mode != Mode::none) {
        fail(context, "already_initialized");
    }
    return stream;
}

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// The OTP error of a zlib status that is not progress.
[[noreturn]] void zlib_failure(ProcessContext &context, int status) {
    fail(context, status == Z_DATA_ERROR ? "data_error" : (status == Z_MEM_ERROR ? "mem_error" : "stream_error"));
}

// open(): a new stream controlled by the caller.
TermResult<Term> open(ProcessContext &context) {
    Stream stream{};
    stream.magic = STREAM_MAGIC;
    stream.owner = self_word(context);
    std::array<std::byte, sizeof(Stream)> bytes{};
    std::memcpy(bytes.data(), &stream, sizeof(stream));
    return detail::HandleAccess::make(context.heap(), bytes, finalize);
}

// close(Z): ends any session; a closed stream is not_initialized.
Term close(ProcessContext &context, const Term &handle) {
    auto &stream = stream_of(context, handle);
    if (stream.mode == Mode::closed) {
        fail(context, "not_initialized");
    }
    finalize(std::span(reinterpret_cast<std::byte *>(&stream), sizeof(Stream)));
    return atom(context, "ok");
}

// set_controller(Z, Pid): hand the stream to another process.
Term set_controller(ProcessContext &context, const Term &handle, const PidArgument &pid) {
    stream_of(context, handle).owner = pid.term.word();
    return atom(context, "ok");
}

// A tuple of N small integers that zlib.erl has checked: deflate_init's {Level, Method, WindowBits, MemLevel,
// Strategy}, inflate_init's {WindowBits, EndOfStream}, deflate_params' {Level, Strategy}.
template <std::size_t N> struct Integers {
    std::array<std::int64_t, N> values;
};
} // namespace

template <std::size_t N> struct Argument<Integers<N>> {
    static TermResult<std::optional<Integers<N>>> convert(const Term &term) {
        if (!term.is_tuple() || term.tuple_size().value_or(0) != N) {
            return std::optional<Integers<N>>{};
        }
        Integers<N> integers{};
        for (std::size_t index = 0; index < N; ++index) {
            const auto element = term.tuple_element(index);
            const auto value = element ? small(element->word()) : std::nullopt;
            if (!value) {
                return std::optional<Integers<N>>{};
            }
            integers.values[index] = *value;
        }
        return std::optional{integers};
    }
};

namespace {
// deflate_init(Z, {Level, Method, WindowBits, MemLevel, Strategy}).
Term deflate_init(ProcessContext &context, const Term &handle, const Integers<5> &parameters) {
    auto &stream = idle(context, handle);
    const auto &[level, method, bits, memory, strategy] = parameters.values;
    const auto status = deflateInit2(&stream.zstream, static_cast<int>(level), static_cast<int>(method),
                                     static_cast<int>(bits), static_cast<int>(memory), static_cast<int>(strategy));
    if (status != Z_OK) {
        zlib_failure(context, status);
    }
    stream.mode = Mode::deflate;
    stream.input_left = false;
    return atom(context, "ok");
}

// inflate_init(Z, {WindowBits, EndOfStream}).
Term inflate_init(ProcessContext &context, const Term &handle, const Integers<2> &parameters) {
    const auto &[bits, end_of_stream] = parameters.values;
    auto &stream = idle(context, handle);
    const auto status = inflateInit2(&stream.zstream, static_cast<int>(bits));
    if (status != Z_OK) {
        zlib_failure(context, status);
    }
    stream.mode = Mode::inflate;
    stream.end_of_stream = static_cast<EndOfStream>(end_of_stream);
    stream.eos_seen = false;
    stream.input_left = false;
    return atom(context, "ok");
}

// Run deflate over all of `input` with `flush`: the output, or the error of a failed call.
TermResult<Term> deflate(ProcessContext &context, const Term &handle, const BinaryArgument &input, std::int64_t flush) {
    auto &stream = active(context, handle, Mode::deflate);
    auto &z = stream.zstream;
    z.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(input.bytes.data()));
    z.avail_in = static_cast<uInt>(input.bytes.size());
    std::vector<std::byte> output;
    for (;;) {
        const auto at = output.size();
        output.resize(at + CHUNK);
        z.next_out = reinterpret_cast<Bytef *>(output.data() + at);
        z.avail_out = static_cast<uInt>(CHUNK);
        const auto status = ::deflate(&z, static_cast<int>(flush));
        output.resize(at + CHUNK - z.avail_out);
        if (status == Z_STREAM_ERROR || (status == Z_BUF_ERROR && z.avail_in != 0)) {
            stream.input_left = true;
            fail(context, "stream_error");
        }
        const bool full = z.avail_out == 0;
        if (status == Z_STREAM_END || (!full && z.avail_in == 0) || status == Z_BUF_ERROR) {
            break;
        }
    }
    z.next_in = nullptr;
    z.next_out = nullptr;
    return TermFactory(context).binary(output);
}

// Whether inflate goes on with input left after a stream's end: a reset starts the next stream, a cut drops the
// input, anything else is data_error (OTP's eos_behavior).
bool continue_after_end(ProcessContext &context, Stream &stream) {
    switch (stream.end_of_stream) {
    case EndOfStream::reset:
        inflateReset(&stream.zstream);
        stream.eos_seen = false;
        return true;
    case EndOfStream::cut:
        stream.zstream.avail_in = 0;
        return false;
    case EndOfStream::error:
        break;
    }
    stream.input_left = true;
    fail(context, "data_error");
}

// {Status, Output} of inflate: finished at a stream's end, {need_dictionary, Adler} or continue for more input.
TermResult<Term> inflate_result(ProcessContext &context, const Term &status, const std::vector<std::byte> &output) {
    const std::array parts{status, need(TermFactory(context).binary(output))};
    return TermFactory(context).tuple(parts);
}

// {{need_dictionary, Adler}, Output, Rest}: the input not yet inflated comes back for the call after the dictionary.
TermResult<Term> needs_dictionary(ProcessContext &context, z_stream &z, const std::vector<std::byte> &output) {
    const std::array dictionary{atom(context, "need_dictionary"),
                                need(TermFactory(context).integer(static_cast<std::int64_t>(z.adler)))};
    const std::span rest(reinterpret_cast<const std::byte *>(z.next_in), z.avail_in);
    const std::array parts{need(TermFactory(context).tuple(dictionary)), need(TermFactory(context).binary(output)),
                           need(TermFactory(context).binary(rest))};
    z.avail_in = 0;
    return TermFactory(context).tuple(parts);
}

// What one inflate call left the stream to do.
enum class Step : std::uint8_t { more, finished, waiting, dictionary };

// After the end of a compressed stream: finished, or more when the rest of the input starts the next stream.
Step stream_end(ProcessContext &context, Stream &stream) {
    stream.eos_seen = true;
    return stream.zstream.avail_in == 0 || !continue_after_end(context, stream) ? Step::finished : Step::more;
}

// One inflate call into a new chunk of `output`.
Step inflate_step(ProcessContext &context, Stream &stream, std::vector<std::byte> &output) {
    auto &z = stream.zstream;
    const auto at = output.size();
    output.resize(at + CHUNK);
    z.next_out = reinterpret_cast<Bytef *>(output.data() + at);
    z.avail_out = static_cast<uInt>(CHUNK);
    const auto status = ::inflate(&z, Z_NO_FLUSH);
    output.resize(at + CHUNK - z.avail_out);
    if (status == Z_NEED_DICT) {
        return Step::dictionary;
    }
    if (status == Z_STREAM_END) {
        return stream_end(context, stream);
    }
    if (status == Z_DATA_ERROR || status == Z_MEM_ERROR || status == Z_STREAM_ERROR) {
        zlib_failure(context, status);
    }
    return status == Z_BUF_ERROR || (z.avail_in == 0 && z.avail_out != 0) ? Step::waiting : Step::more;
}

// Run inflate over all of `input`: {finished | continue | {need_dictionary, Adler}, Output}.
TermResult<Term> inflate(ProcessContext &context, const Term &handle, const BinaryArgument &input) {
    auto &stream = active(context, handle, Mode::inflate);
    auto &z = stream.zstream;
    std::vector<std::byte> output;
    z.next_in = reinterpret_cast<Bytef *>(const_cast<std::byte *>(input.bytes.data()));
    z.avail_in = static_cast<uInt>(input.bytes.size());
    if (stream.eos_seen && (z.avail_in == 0 || !continue_after_end(context, stream))) {
        return inflate_result(context, atom(context, "finished"), output);
    }
    for (;;) {
        switch (inflate_step(context, stream, output)) {
        case Step::more:
            break;
        case Step::dictionary:
            return needs_dictionary(context, z, output);
        case Step::finished:
            return inflate_result(context, atom(context, "finished"), output);
        case Step::waiting:
            return inflate_result(context, atom(context, "continue"), output);
        }
    }
}

// deflate_end(Z) and inflate_end(Z): data_error when deflate was not finished.
Term deflate_end(ProcessContext &context, const Term &handle) {
    auto &stream = active(context, handle, Mode::deflate);
    stream.mode = Mode::none;
    if (deflateEnd(&stream.zstream) == Z_DATA_ERROR || std::exchange(stream.input_left, false)) {
        fail(context, "data_error");
    }
    return atom(context, "ok");
}

// inflate_end(Z): data_error unless the stream's end was seen.
Term inflate_end(ProcessContext &context, const Term &handle) {
    auto &stream = active(context, handle, Mode::inflate);
    stream.mode = Mode::none;
    inflateEnd(&stream.zstream);
    if (!stream.eos_seen || std::exchange(stream.input_left, false)) {
        fail(context, "data_error");
    }
    return atom(context, "ok");
}

Term deflate_reset(ProcessContext &context, const Term &handle) {
    deflateReset(&active(context, handle, Mode::deflate).zstream);
    return atom(context, "ok");
}

Term inflate_reset(ProcessContext &context, const Term &handle) {
    auto &stream = active(context, handle, Mode::inflate);
    inflateReset(&stream.zstream);
    stream.eos_seen = false;
    return atom(context, "ok");
}

// deflate_params(Z, {Level, Strategy}): the output compressed with the old parameters.
TermResult<Term> deflate_params(ProcessContext &context, const Term &handle, const Integers<2> &parameters) {
    const auto &[level, strategy] = parameters.values;
    auto &z = active(context, handle, Mode::deflate).zstream;
    std::vector<std::byte> output(CHUNK);
    z.next_out = reinterpret_cast<Bytef *>(output.data());
    z.avail_out = static_cast<uInt>(CHUNK);
    const auto status = deflateParams(&z, static_cast<int>(level), static_cast<int>(strategy));
    output.resize(CHUNK - z.avail_out);
    z.next_out = nullptr;
    if (status != Z_OK && status != Z_BUF_ERROR) {
        zlib_failure(context, status);
    }
    return TermFactory(context).binary(output);
}

// deflate_set_dictionary(Z, Dictionary): its Adler-32 checksum.
TermResult<Term> deflate_set_dictionary(ProcessContext &context, const Term &handle, const BinaryArgument &dictionary) {
    auto &z = active(context, handle, Mode::deflate).zstream;
    const auto status = deflateSetDictionary(&z, reinterpret_cast<const Bytef *>(dictionary.bytes.data()),
                                             static_cast<uInt>(dictionary.bytes.size()));
    if (status != Z_OK) {
        zlib_failure(context, status);
    }
    return TermFactory(context).integer(static_cast<std::int64_t>(z.adler));
}

Term inflate_set_dictionary(ProcessContext &context, const Term &handle, const BinaryArgument &dictionary) {
    auto &z = active(context, handle, Mode::inflate).zstream;
    const auto status = inflateSetDictionary(&z, reinterpret_cast<const Bytef *>(dictionary.bytes.data()),
                                             static_cast<uInt>(dictionary.bytes.size()));
    if (status != Z_OK) {
        zlib_failure(context, status);
    }
    return atom(context, "ok");
}

// inflate_get_dictionary(Z): the sliding window as a binary.
TermResult<Term> inflate_get_dictionary(ProcessContext &context, const Term &handle) {
    auto &z = active(context, handle, Mode::inflate).zstream;
    std::vector<std::byte> dictionary(32768);
    auto size = static_cast<uInt>(dictionary.size());
    if (inflateGetDictionary(&z, reinterpret_cast<Bytef *>(dictionary.data()), &size) != Z_OK) {
        fail(context, "stream_error");
    }
    dictionary.resize(size);
    return TermFactory(context).binary(dictionary);
}

constexpr std::array ZLIB_BUILTINS{
    typed_entry<open>("clause_zlib", "open"),
    typed_entry<close>("clause_zlib", "close"),
    typed_entry<set_controller>("clause_zlib", "set_controller"),
    typed_entry<deflate_init>("clause_zlib", "deflate_init"),
    typed_entry<inflate_init>("clause_zlib", "inflate_init"),
    typed_entry<deflate>("clause_zlib", "deflate"),
    typed_entry<inflate>("clause_zlib", "inflate"),
    typed_entry<deflate_end>("clause_zlib", "deflate_end"),
    typed_entry<inflate_end>("clause_zlib", "inflate_end"),
    typed_entry<deflate_reset>("clause_zlib", "deflate_reset"),
    typed_entry<inflate_reset>("clause_zlib", "inflate_reset"),
    typed_entry<deflate_params>("clause_zlib", "deflate_params"),
    typed_entry<deflate_set_dictionary>("clause_zlib", "deflate_set_dictionary"),
    typed_entry<inflate_set_dictionary>("clause_zlib", "inflate_set_dictionary"),
    typed_entry<inflate_get_dictionary>("clause_zlib", "inflate_get_dictionary"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> zlib_builtins() noexcept { return builtins::ZLIB_BUILTINS; }
} // namespace clause::runtime
