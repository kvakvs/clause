#include "ports.hpp"
#include "../../process/identities.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include <array>
#include <charconv>
#include <string>

// The port builtins of the bridge (docs/ports.md#builtins-and-port-messages): open_port/2, port_close/1,
// port_command/2,3, port_connect/2, port_control/3, port_call/2,3, port_info/1,2, port_to_list/1, list_to_port/1 and
// ports/0, plus the data and option conversions they share.
namespace clause::runtime::builtins {
namespace {
using detail::Framing;
using detail::PortOptions;

// The atom `name`.
Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// Raise error:Reason for an atom reason, as open_port/2 raises enoent and port_command/3 notsup.
[[noreturn]] void raise_atom(ProcessContext &context, std::string_view reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, reason).word()};
}

// A proper list of the characters of UTF-8 `text`.
TermResult<Term> char_list(ProcessContext &context, std::string_view text) {
    std::vector<Term> items;
    for (const auto code : code_points(text)) {
        items.push_back(need(Term::from_word(need(encode_integer(code)))));
    }
    return TermFactory(context).list(items);
}

// The bytes of iodata: a binary, or a list of bytes, binaries and iodata lists with a binary or [] tail; none for
// anything else. A byte is valid only as a list element.
std::optional<std::vector<std::byte>> iodata_bytes(const Term &data) {
    std::vector<std::byte> bytes;
    // Terms still to read, each with whether it is a list element.
    std::vector<std::pair<Term, bool>> pending{{data, false}};
    while (!pending.empty()) {
        const auto [item, element] = pending.back();
        pending.pop_back();
        const auto byte = element ? small(item.word()) : std::nullopt;
        if (byte && *byte >= 0 && *byte <= 255) {
            bytes.push_back(static_cast<std::byte>(*byte));
        } else if (item.is_binary()) {
            const auto part = need(item.binary_bytes());
            bytes.insert(bytes.end(), part.begin(), part.end());
        } else if (item.is_cons()) {
            pending.emplace_back(need(item.tail()), false);
            pending.emplace_back(need(item.head()), true);
        } else if (!item.is_nil()) {
            return std::nullopt;
        }
    }
    return bytes;
}

// The port a port argument names: a port, or an atom registered to one; badarg for anything else.
Word port_of(ProcessContext &context, const Term &port) {
    if (port.is_port()) {
        return port.word();
    }
    const auto word = port.is_atom() ? detail::Executor::of(context).whereis(context, port.word()) : 0;
    if (TermTag{word}.get_kind() != TermKind::local_port) {
        bad_argument();
    }
    return word;
}

// The string of a proper list of characters or of a binary, as UTF-8; none for anything else.
std::optional<std::string> text_of(const Term &value) {
    std::string text;
    if (value.is_binary()) {
        const auto bytes = need(value.binary_bytes());
        std::ranges::transform(bytes, std::back_inserter(text), [](std::byte b) { return static_cast<char>(b); });
        return text;
    }
    auto list = value;
    for (; list.is_cons(); list = need(list.tail())) {
        const auto code = small(need(list.head()).word());
        if (!code || !unicode_character(*code)) {
            return std::nullopt;
        }
        encode(static_cast<char32_t>(*code), text);
    }
    return list.is_nil() ? std::optional{text} : std::nullopt;
}

// An atom option of open_port/2 and how it changes the options.
struct AtomOption {
    std::string_view name;
    void (*apply)(PortOptions &options);
};

constexpr std::array ATOM_OPTIONS{
    AtomOption{"binary", [](PortOptions &options) { options.binary = true; }},
    AtomOption{"stream", [](PortOptions &options) { options.framing = Framing::stream; }},
    AtomOption{"eof", [](PortOptions &options) { options.eof = true; }},
    AtomOption{"exit_status", [](PortOptions &options) { options.exit_status = true; }},
    AtomOption{"in", [](PortOptions &options) { options.output = false; }},
    AtomOption{"out", [](PortOptions &options) { options.input = false; }},
    AtomOption{"use_stdio", [](PortOptions &options) { options.use_stdio = true; }},
    AtomOption{"nouse_stdio", [](PortOptions &options) { options.use_stdio = false; }},
    AtomOption{"stderr_to_stdout", [](PortOptions &options) { options.stderr_to_stdout = true; }},
    // Accepted without effect, as Windows-only or advisory in OTP.
    AtomOption{"hide", [](PortOptions &) {}},
    AtomOption{"overlapped_io", [](PortOptions &) {}},
};

// Apply an atom option; false when it is none.
bool atom_option(std::string_view name, PortOptions &options) {
    const auto found = std::ranges::find(ATOM_OPTIONS, name, &AtomOption::name);
    if (found == ATOM_OPTIONS.end()) {
        return false;
    }
    found->apply(options);
    return true;
}

// A list of strings, as {args, List} takes; none for anything else.
std::optional<std::vector<std::string>> strings(const Term &list) {
    std::vector<std::string> result;
    for (auto rest = list; !rest.is_nil(); rest = need(rest.tail())) {
        const auto text = rest.is_cons() ? text_of(need(rest.head())) : std::nullopt;
        if (!text) {
            return std::nullopt;
        }
        result.push_back(*text);
    }
    return result;
}

// One {Name, Value} pair of {env, Env}: an unset variable when Value is false.
std::optional<std::pair<std::string, std::optional<std::string>>> variable(const Term &pair) {
    if (!pair.is_tuple() || pair.tuple_size().value_or(0) != 2) {
        return std::nullopt;
    }
    const auto name = text_of(need(pair.tuple_element(0)));
    const auto value = need(pair.tuple_element(1));
    if (!name || name->empty()) {
        return std::nullopt;
    }
    if (value.is_atom() && value.atom_spelling().value_or("") == "false") {
        return std::pair{*name, std::optional<std::string>{}};
    }
    const auto text = text_of(value);
    return text ? std::optional{std::pair{*name, text}} : std::nullopt;
}

// Apply {env, Env}; false when Env is not a list of {Name, Value | false}.
bool environment(const Term &list, PortOptions &options) {
    for (auto rest = list; !rest.is_nil(); rest = need(rest.tail())) {
        const auto item = rest.is_cons() ? variable(need(rest.head())) : std::nullopt;
        if (!item) {
            return false;
        }
        options.env.push_back(*item);
    }
    return true;
}

// Apply {packet, N} or {line, L}; false for another value.
bool framing_option(std::string_view name, const Term &value, PortOptions &options) {
    const auto number = small(value.word());
    if (name == "packet" && number && (*number == 1 || *number == 2 || *number == 4)) {
        options.framing = Framing::packet;
        options.packet_bytes = static_cast<std::size_t>(*number);
        return true;
    }
    if (name == "line" && number && *number > 0) {
        options.framing = Framing::line;
        options.line_length = static_cast<std::size_t>(*number);
        return true;
    }
    return false;
}

// Apply {busy_limits_port, {Low, High} | disabled} (docs/ports.md#busy-ports): limits of at least 1, a low limit
// above the high one lowered to it; disabled never makes the port busy. False for another value.
bool busy_limits(const Term &value, PortOptions &options) {
    if (value.is_atom() && value.atom_spelling().value_or("") == "disabled") {
        options.busy_low = options.busy_high = SIZE_MAX;
        return true;
    }
    if (!value.is_tuple() || value.tuple_size().value_or(0) != 2) {
        return false;
    }
    const auto low = small(need(value.tuple_element(0)).word());
    const auto high = small(need(value.tuple_element(1)).word());
    if (!low || !high || *low < 1 || *high < 1) {
        return false;
    }
    options.busy_high = static_cast<std::size_t>(*high);
    options.busy_low = std::min(static_cast<std::size_t>(*low), options.busy_high);
    return true;
}

// Apply a {Name, Value} option; false when it is none or its value is invalid.
bool pair_option(std::string_view name, const Term &value, PortOptions &options) {
    if (name == "packet" || name == "line") {
        return framing_option(name, value, options);
    }
    if (name == "busy_limits_port") {
        return busy_limits(value, options);
    }
    if (name == "args") {
        const auto list = strings(value);
        options.args = list.value_or(std::vector<std::string>{});
        return list.has_value();
    }
    if (name == "arg0" || name == "cd") {
        const auto text = text_of(value);
        (name == "cd" ? options.cd : options.arg0) = text;
        return text.has_value();
    }
    if (name == "env") {
        return environment(value, options);
    }
    return name == "parallelism" && value.is_boolean();
}

// Apply one option of open_port/2; false when it is none.
bool option(const Term &item, PortOptions &options) {
    if (item.is_atom()) {
        return atom_option(item.atom_spelling().value_or(""), options);
    }
    if (!item.is_tuple() || item.tuple_size().value_or(0) != 2 || !need(item.tuple_element(0)).is_atom()) {
        return false;
    }
    return pair_option(need(item.tuple_element(0)).atom_spelling().value_or(""), need(item.tuple_element(1)), options);
}

// The options of open_port/2; badarg for any unknown or invalid one.
PortOptions port_options(const ListArgument &list) {
    PortOptions options;
    for (const auto &item : list.elements) {
        if (!option(item, options)) {
            bad_argument();
        }
    }
    return options;
}

// A descriptor of {fd, In, Out}; badarg unless it is a non-negative small integer.
int descriptor(const Term &value) {
    const auto number = small(value.word());
    if (!number || *number < 0 || *number > 0x7fffffff) {
        bad_argument();
    }
    return static_cast<int>(*number);
}

// The driver and name of {fd, In, Out}.
std::pair<std::unique_ptr<detail::PortDriver>, std::string> fd(const TupleArgument &name) {
    const auto in = descriptor(name.elements[1]);
    const auto out = descriptor(name.elements[2]);
    return {detail::fd_driver(in, out), std::to_string(in) + "/" + std::to_string(out)};
}

// The driver and name of {spawn, Command} or {spawn_executable, File}; error:Reason (enoent, eacces, ...) when the
// program cannot be started.
std::pair<std::unique_ptr<detail::PortDriver>, std::string>
spawned(ProcessContext &context, bool executable, const std::string &command, const PortOptions &options) {
    auto driver = detail::spawn_driver({.executable = executable, .command = command}, options);
    if (!driver) {
        raise_atom(context, driver.error().reason);
    }
    return {std::move(*driver), command};
}

// The project library's driver {spawn_driver, Name} (files, sockets); badarg for any other name.
std::unique_ptr<detail::PortDriver> internal_driver(ProcessContext &context, const std::string &name) {
    if (name == "clause_file") {
        return detail::file_driver();
    }
    if (name != "tcp_inet" && name != "udp_inet") {
        bad_argument();
    }
    return detail::Executor::of(context).socket_driver(name == "udp_inet");
}

// The driver of an open_port/2 port name and the name port_info reports; badarg for an unknown or invalid name.
std::pair<std::unique_ptr<detail::PortDriver>, std::string> driver(ProcessContext &context, const TupleArgument &name,
                                                                   const PortOptions &options) {
    const auto kind = name.elements.empty() ? std::string_view{} : name.elements[0].atom_spelling().value_or("");
    if (kind == "fd" && name.elements.size() == 3) {
        return fd(name);
    }
    const auto command = name.elements.size() == 2 ? text_of(name.elements[1]) : std::nullopt;
    if (!command) {
        bad_argument();
    }
    if (kind == "spawn_driver") {
        return {internal_driver(context, *command), *command};
    }
    if (kind != "spawn" && kind != "spawn_executable") {
        bad_argument();
    }
    return spawned(context, kind == "spawn_executable", *command, options);
}

// open_port(PortName, Options): a new port linked to the caller (docs/ports.md#drivers): {fd, In, Out},
// {spawn, Command} or {spawn_executable, File}.
TermResult<Term> open_port(ProcessContext &context, const TupleArgument &name, const ListArgument &list) {
    auto options = port_options(list);
    auto [port_driver, spelling] = driver(context, name, options);
    const auto word =
        detail::Executor::of(context).open_port(context, std::move(port_driver), std::move(options), spelling);
    return Term::from_word(word, context);
}

// port_close(Port): true; badarg when the port is not open.
TermResult<Term> port_close(ProcessContext &context, const Term &port) {
    if (!detail::Executor::of(context).close_port(context, port_of(context, port))) {
        bad_argument();
    }
    return atom(context, "true");
}

// The options of port_command/3: force and nosuspend.
struct CommandOptions final {
    bool force = false;
    bool nosuspend = false;
};

// Write Data to the port of `word` as port_command/2,3 do: true, or false for a busy port with nosuspend; a busy port
// otherwise suspends the caller until it is not busy. badarg for a closed port or data that is not iodata, notsup
// when forced (no driver allows force, as OTP's spawn and fd drivers do not).
TermResult<Term> command(ProcessContext &context, Word word, const Term &data, CommandOptions options = {}) {
    if (options.force) {
        raise_atom(context, "notsup");
    }
    auto bytes = iodata_bytes(data);
    if (!bytes) {
        bad_argument();
    }
    const auto outcome =
        detail::Executor::of(context).command_port(context, word, std::move(*bytes), options.nosuspend);
    if (outcome == detail::PortOutcome::busy) {
        return atom(context, "false");
    }
    if (outcome != detail::PortOutcome::done) {
        bad_argument();
    }
    return atom(context, "true");
}

// port_command(Port, Data): true after writing Data.
TermResult<Term> port_command2(ProcessContext &context, const Term &port, const Term &data) {
    return command(context, port_of(context, port), data);
}

// The options of port_command/3 (force, nosuspend); badarg for any other option.
CommandOptions command_options(const ListArgument &options) {
    CommandOptions result;
    for (const auto &option : options.elements) {
        const auto spelling = option.is_atom() ? option.atom_spelling().value_or("") : "";
        if (spelling != "force" && spelling != "nosuspend") {
            bad_argument();
        }
        (spelling == "force" ? result.force : result.nosuspend) = true;
    }
    return result;
}

// port_command(Port, Data, Options): force is notsup on every driver; nosuspend answers false for a busy port.
TermResult<Term> port_command3(ProcessContext &context, const Term &port, const Term &data,
                               const ListArgument &options) {
    const auto parsed = command_options(options);
    return command(context, port_of(context, port), data, parsed);
}

// port_connect(Port, Pid): true; the new owner is linked to the port.
TermResult<Term> port_connect(ProcessContext &context, const Term &port, const Term &pid) {
    if (!pid.is_pid() || !detail::Executor::of(context).connect_port(port_of(context, port), pid)) {
        bad_argument();
    }
    return atom(context, "true");
}

// port_control(Port, Operation, Data): the driver's answer, a binary for a binary port, else a byte list; badarg for
// a closed port, a driver without control or an operation it does not have.
TermResult<Term> control(ProcessContext &context, Word port, std::optional<std::int64_t> code,
                         const std::optional<std::vector<std::byte>> &bytes) {
    if (!code || *code < 0 || *code > 0xffffffff || !bytes) {
        bad_argument();
    }
    const auto answer =
        detail::Executor::of(context).control_port(context, port, *bytes, static_cast<std::uint32_t>(*code));
    if (!answer) {
        bad_argument();
    }
    TermFactory factory(context);
    if (answer->second) {
        return factory.binary(answer->first);
    }
    std::vector<Word> list;
    for (const auto byte : answer->first) {
        list.push_back(need(encode_integer(std::to_integer<std::int64_t>(byte))));
    }
    return factory.list_words(list, need(factory.nil()));
}

TermResult<Term> port_control(ProcessContext &context, const Term &port, const Term &operation, const Term &data) {
    return control(context, port_of(context, port), small(operation.word()), iodata_bytes(data));
}

// port_call(Port, Data) and port_call(Port, Operation, Data): no driver answers calls.
TermResult<Term> port_call2(ProcessContext &context, const Term &port, const Term &) {
    port_of(context, port);
    bad_argument();
}

TermResult<Term> port_call3(ProcessContext &context, const Term &port, const Term &, const Term &) {
    port_of(context, port);
    bad_argument();
}

// A list of pid or port words.
TermResult<Term> word_list(ProcessContext &context, const std::vector<Word> &words) {
    TermFactory factory(context);
    return factory.list_words(words, need(factory.nil()));
}

// {Name, Value} of port_info/2.
TermResult<Term> item(ProcessContext &context, std::string_view name, const TermResult<Term> &value) {
    return TermFactory(context).tuple(std::array{atom(context, name), need(value)});
}

// The value of a counting port_info item (id, input, output, memory, queue_size); none for another item.
std::optional<std::size_t> info_count(std::string_view name, const detail::PortInfo &info) {
    if (name == "id" || name == "input" || name == "output") {
        return name == "id" ? info.id : (name == "input" ? info.input : info.output);
    }
    if (name == "queue_size") {
        return info.queue_size;
    }
    // A port record has no Erlang heap.
    return name == "memory" ? std::optional<std::size_t>{0} : std::nullopt;
}

// The value of a port_info item that is a constant atom (locking, parallelism) or [] (monitors: a port monitors
// nothing); none for another item.
std::optional<TermResult<Term>> info_constant(ProcessContext &context, std::string_view name) {
    TermFactory factory(context);
    if (name == "monitors") {
        return factory.nil();
    }
    if (name == "locking" || name == "parallelism") {
        return factory.atom(name == "locking" ? "port_level" : "false");
    }
    return std::nullopt;
}

// The value of one port_info item; none for an unknown item.
std::optional<TermResult<Term>> info_value(ProcessContext &context, std::string_view name,
                                           const detail::PortInfo &info) {
    if (const auto count = info_count(name, info)) {
        return Term::from_word(need(encode_integer(static_cast<std::int64_t>(*count))));
    }
    if (name == "name") {
        return char_list(context, info.name);
    }
    if (name == "links" || name == "monitored_by") {
        return word_list(context, name == "links" ? info.links : info.monitored_by);
    }
    if (name == "connected") {
        return Term::from_word(info.connected, context);
    }
    if (name == "os_pid") {
        return info.os_pid ? Term::from_word(need(encode_integer(*info.os_pid)))
                           : TermFactory(context).atom("undefined");
    }
    return info_constant(context, name);
}

// port_info(Port, Item): {Item, Value}; registered_name gives [] for a port without a name; undefined once the
// port closed; badarg for an unknown item.
TermResult<Term> port_info2(ProcessContext &context, const Term &port, const AtomArgument &name) {
    const auto info = detail::Executor::of(context).port_info(port_of(context, port));
    if (name.spelling == "registered_name") {
        if (!info) {
            return atom(context, "undefined");
        }
        return info->registered != 0 ? item(context, name.spelling, Term::from_word(info->registered, context))
                                     : TermFactory(context).nil();
    }
    if (!info) {
        // Validate the item before answering for a closed port.
        if (!info_value(context, name.spelling, detail::PortInfo{})) {
            bad_argument();
        }
        return atom(context, "undefined");
    }
    const auto value = info_value(context, name.spelling, *info);
    if (!value) {
        bad_argument();
    }
    return item(context, name.spelling, *value);
}

// port_info(Port): the items name, links, id, connected, input, output and os_pid, or undefined.
TermResult<Term> port_info1(ProcessContext &context, const Term &port) {
    const auto info = detail::Executor::of(context).port_info(port_of(context, port));
    if (!info) {
        return atom(context, "undefined");
    }
    std::vector<Term> items;
    for (const auto name : {"name", "links", "id", "connected", "input", "output", "os_pid"}) {
        const auto value = info_value(context, name, *info);
        if (!value) {
            bad_argument();
        }
        items.push_back(need(item(context, name, *value)));
    }
    return TermFactory(context).list(items);
}

// port_to_list(Port): its text #Port<0.N>.
TermResult<Term> port_to_list(ProcessContext &context, const Term &port) {
    if (!port.is_port()) {
        bad_argument();
    }
    return char_list(context, need(format_term(port, TermStyle::write)));
}

// list_to_port(Text): the port of #Port<0.N> text; badarg for other text or a number this program never issued.
TermResult<Term> list_to_port(ProcessContext &context, const ListArgument &text) {
    std::string spelling;
    for (const auto &code : text.elements) {
        const auto value = small(code.word());
        if (!value || *value < 0 || *value > 127) {
            bad_argument();
        }
        spelling.push_back(static_cast<char>(*value));
    }
    constexpr std::string_view prefix = "#Port<0.";
    Word number = 0;
    const auto *end = spelling.data() + spelling.size();
    const auto parsed = spelling.starts_with(prefix) && spelling.ends_with(">")
                            ? std::from_chars(spelling.data() + prefix.size(), end - 1, number)
                            : std::from_chars_result{nullptr, std::errc::invalid_argument};
    if (parsed.ec != std::errc{} || parsed.ptr != end - 1) {
        bad_argument();
    }
    const auto port = Term::from_word(detail::port_word(number), context);
    if (!port) {
        bad_argument();
    }
    return *port;
}

// The element of a 2-tuple, or none for another term.
std::optional<std::pair<Term, Term>> pair_of(const Term &value) {
    if (!value.is_tuple() || value.tuple_size().value_or(0) != 2) {
        return std::nullopt;
    }
    return std::pair{need(value.tuple_element(0)), need(value.tuple_element(1))};
}

// ports(): the open ports.
TermResult<Term> ports(ProcessContext &context) { return word_list(context, detail::Executor::of(context).ports()); }

constexpr std::array PORT_BUILTINS{
    typed_entry<open_port>("erlang", "open_port"),
    typed_entry<port_close>("erlang", "port_close"),
    typed_entry<port_command2>("erlang", "port_command"),
    typed_entry<port_command3>("erlang", "port_command"),
    typed_entry<port_connect>("erlang", "port_connect"),
    typed_entry<port_control>("erlang", "port_control"),
    typed_entry<port_call2>("erlang", "port_call"),
    typed_entry<port_call3>("erlang", "port_call"),
    typed_entry<port_info1>("erlang", "port_info"),
    typed_entry<port_info2>("erlang", "port_info"),
    typed_entry<port_to_list>("erlang", "port_to_list"),
    typed_entry<list_to_port>("erlang", "list_to_port"),
    typed_entry<ports>("erlang", "ports"),
};
} // namespace

namespace {
// The request {Tag, Value} of `from`: {connect, Pid} or {command, Data}; malformed for anything else.
detail::PortRequest tagged_request(Word from, const Term &request) {
    using Kind = detail::PortRequest::Kind;
    const auto inner = pair_of(request);
    if (!inner) {
        return {};
    }
    const auto &[name, value] = *inner;
    const auto tag = name.is_atom() ? name.atom_spelling().value_or("") : "";
    auto result = detail::PortRequest::of(tag == "connect" ? Kind::connect : Kind::command, from);
    if (tag == "connect" && value.is_pid()) {
        result.owner = value.word();
        return result;
    }
    auto data = tag == "command" ? iodata_bytes(value) : std::nullopt;
    if (!data) {
        return {};
    }
    result.data = std::move(*data);
    return result;
}
} // namespace

detail::PortRequest port_request(const Term &message) {
    const auto outer = pair_of(message);
    if (!outer || !outer->first.is_pid()) {
        return {};
    }
    const auto &request = outer->second;
    if (request.is_atom() && request.atom_spelling().value_or("") == "close") {
        return detail::PortRequest::of(detail::PortRequest::Kind::close, outer->first.word());
    }
    return tagged_request(outer->first.word(), request);
}
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> port_builtins() noexcept { return builtins::PORT_BUILTINS; }
} // namespace clause::runtime
