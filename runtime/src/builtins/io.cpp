#include "../scheduler/executor.hpp"
#include "../terms/service_errors.hpp"
#include "io_format.hpp"
#include "io_lib/key_orders.hpp"
#include "text.hpp"
#include "typed.hpp"
#include <array>
#include <clause/abi/term.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/output.hpp>
#include <cstdio>
#include <terms.hpp>

// The io builtins (docs/io.md): io:format/1,2,3, io:fwrite/1,2,3, io:put_chars/1,2 and io:nl/0,1 write UTF-8 to
// standard output, standard error or an I/O server process, and return ok. Text the device rejects raises badarg (a
// thrown BuiltinFailure), writing nothing.
namespace clause::runtime::builtins {
namespace {
// The IoDevice argument of an io builtin, resolved by device() (step 41U).
struct IoDevice {
    // The argument as passed.
    Term term;
};
} // namespace

// Any term as an io device; device() checks it.
template <> struct Argument<IoDevice> {
    static TermResult<std::optional<IoDevice>> convert(const Term &term) { return IoDevice{term}; }
};

namespace {
// Where an io builtin writes (step 41U): standard output, standard error, or the pid of an I/O server.
struct Device {
    // Standard error instead of standard output; ignored when `server` is set.
    bool error_stream = false;
    // The pid of the I/O server (an open file of the file module), or 0.
    Word server = 0;
};

// Write `text` as UTF-8 and return ok; a rejected write is the output_failure runtime failure.
Word emit(ProcessContext &context, std::u32string_view text) {
    if (!write_output(context.standard_output(), utf8(text))) {
        context.generated_calls().fail_service(abi::v1::Status::output_failure);
        return 0;
    }
    return publish(context, context.atom_storage().intern("ok"_atom));
}

// Raise error:Reason for an atom reason, as OTP's io raises terminated and calling_self (step 41U).
[[noreturn]] void raise_atom(ProcessContext &context, const AtomName reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, reason).word()};
}

// The device of an io call: standard_io and user are standard output, standard_error is standard error, another
// atom names a registered I/O server (badarg when none is), a pid is an I/O server; anything else has no clause.
Device device(ProcessContext &context, const IoDevice &argument) {
    const auto &device = argument.term;
    Word server = device.word();
    if (device.is_atom()) {
        const auto name = device.atom_spelling_unchecked();
        if (name == "standard_io" || name == "user" || name == "standard_error") {
            return {.error_stream = name == "standard_error"};
        }
        server = detail::Executor::of(context).whereis(context, device.word());
        if (server == 0) {
            bad_argument();
        }
    } else if (!device.is_pid()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
    }
    if (server == need(TermFactory(context).pid(context.identity())).word()) {
        raise_atom(context, "calling_self"_atom);
    }
    if (!detail::Executor::alive(context, server)) {
        raise_atom(context, "terminated"_atom);
    }
    return {.server = server};
}

// Write `text` to standard error after flushing standard output, so the two keep their order; returns ok.
Word emit_error(ProcessContext &context, std::u32string_view text) {
    const auto bytes = utf8(text);
    std::fflush(stdout);
    if (std::fwrite(bytes.data(), 1, bytes.size(), stderr) != bytes.size()) {
        context.generated_calls().fail_service(abi::v1::Status::output_failure);
        return 0;
    }
    std::fflush(stderr);
    return publish(context, context.atom_storage().intern("ok"_atom));
}

// Hand `text` to an I/O server: continue as clause_io:put_chars(Server, Utf8Binary), an I/O protocol put_chars request
// whose reply the call returns (plan 21 step LIB63).
Word emit_server(ProcessContext &context, Word server, std::u32string_view text) {
    TermFactory factory(context);
    const auto bytes = utf8(text);
    const auto binary = need(factory.binary(std::as_bytes(std::span{bytes.data(), bytes.size()})));
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(context.code_server().function_frame(
        need(factory.atom("clause_io"_atom)), need(factory.atom("put_chars"_atom)), 2));
    if (!frame) {
        bad_argument();
    }
    context.stack().trap(*frame, std::array{server, binary.word()});
    return 0;
}

// Write `text` to the device.
Word emit_to(ProcessContext &context, const Device &target, std::u32string_view text) {
    if (target.server != 0) {
        return emit_server(context, target.server, text);
    }
    return target.error_stream ? emit_error(context, text) : emit(context, text);
}

// Write formatted text, unless the format met maps ordered by comparison funs whose keys are still to be sorted (plan
// 21 step LIB4F): the call then runs again.
Word emit_formatted(ProcessContext &context, const Device &target, std::u32string_view text) {
    return io_lib::keys_missing() ? 0 : emit_to(context, target, text);
}

// io:format(Format): io:format(Format, []).
Word format1(ProcessContext &context, const Term &format) {
    return emit_formatted(context, {}, format_text(context, format, std::nullopt));
}

// io:format(Format, Args).
Word format2(ProcessContext &context, const Term &format, const Term &arguments) {
    return emit_formatted(context, {}, format_text(context, format, arguments));
}

// io:format(IoDevice, Format, Args) (step 41U).
Word format3(ProcessContext &context, const IoDevice &io_device, const Term &format, const Term &arguments) {
    const auto target = device(context, io_device);
    return emit_formatted(context, target, format_text(context, format, arguments));
}

// io:put_chars(Chardata).
Word put_chars(ProcessContext &context, const Term &chardata) { return emit(context, characters(chardata)); }

// io:put_chars(IoDevice, Chardata) (step 41U).
Word put_chars2(ProcessContext &context, const IoDevice &io_device, const Term &chardata) {
    const auto target = device(context, io_device);
    return emit_to(context, target, characters(chardata));
}

// io:nl() (step 41U).
Word nl0(ProcessContext &context) { return emit(context, U"\n"); }

// io:nl(IoDevice) (step 41U).
Word nl1(ProcessContext &context, const IoDevice &io_device) {
    const auto target = device(context, io_device);
    return emit_to(context, target, U"\n");
}

constexpr std::array IO_BUILTINS{
    io_lib::ordering_entry<format1, true>("io", "format"),
    io_lib::ordering_entry<format2, true>("io", "format"),
    typed_entry<put_chars>("io", "put_chars"),
    io_lib::ordering_entry<format3, true>("io", "format"),
    io_lib::ordering_entry<format1, true>("io", "fwrite"),
    io_lib::ordering_entry<format2, true>("io", "fwrite"),
    io_lib::ordering_entry<format3, true>("io", "fwrite"),
    typed_entry<put_chars2>("io", "put_chars"),
    typed_entry<nl0>("io", "nl"),
    typed_entry<nl1>("io", "nl"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> io_builtins() noexcept { return builtins::IO_BUILTINS; }
} // namespace clause::runtime
