#include "call_trace.hpp"
#include <algorithm>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/output.hpp>
#include <clause/runtime/process_context.hpp>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace clause::runtime::detail {
namespace {
constexpr std::size_t NONE = static_cast<std::size_t>(-1);
// Bounds of printed terms; the OTP-side tracer uses the same ones.
constexpr int PRINT_DEPTH = 5;
constexpr std::size_t PRINT_WIDTH = 16;
constexpr std::size_t PRINT_MAP_SIZE = 10;
constexpr std::size_t PRINT_BINARY = 40;

// Read an environment variable; unset reads as empty.
std::string environment(const char *name) {
#ifdef _WIN32
    char *buffer = nullptr;
    std::size_t size = 0;
    const auto error = _dupenv_s(&buffer, &size, name);
    const std::unique_ptr<char, decltype(&std::free)> value(buffer, &std::free);
    return error == 0 && value ? std::string(value.get()) : std::string();
#else
    const auto *value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

// The trace file shared by every process, with the module prefixes whose calls are the program's own.
struct Sink final {
    std::FILE *file = nullptr;
    std::mutex mutex;
    std::vector<std::string> quiet;
    // CLAUSE_CALL_TRACE_ALL=1: also log every entry by name.
    bool all = false;
    // CLAUSE_CALL_TRACE_FIRST=1: also log the first entry of every function, and the functions seen so far.
    bool first = false;
    std::unordered_set<const abi::v1::FrameDescriptor *> seen;
    std::unordered_map<const abi::v1::FrameDescriptor *, std::string> names;
};

// The module prefixes of CLAUSE_CALL_TRACE_QUIET (comma separated), by default luerl,ttdict,ttsets.
std::vector<std::string> quiet_prefixes() {
    auto quiet = environment("CLAUSE_CALL_TRACE_QUIET");
    if (quiet.empty()) {
        quiet = "luerl,ttdict,ttsets";
    }
    std::vector<std::string> prefixes;
    for (std::size_t at = 0; at <= quiet.size();) {
        const auto end = std::min(quiet.find(',', at), quiet.size());
        if (end > at) {
            prefixes.push_back(quiet.substr(at, end - at));
        }
        at = end + 1;
    }
    return prefixes;
}

// Open the trace file for writing; null when it cannot be opened.
std::FILE *open_file(const std::string &path) {
    std::FILE *file = nullptr;
#ifdef _WIN32
    if (fopen_s(&file, path.c_str(), "wb") != 0) {
        file = nullptr;
    }
#else
    file = std::fopen(path.c_str(), "wb");
#endif
    return file;
}

// The sink of CLAUSE_CALL_TRACE, CLAUSE_CALL_TRACE_QUIET, CLAUSE_CALL_TRACE_ALL and CLAUSE_CALL_TRACE_FIRST; null
// when tracing is off, so a program that does not trace allocates nothing for it.
Sink *open_sink() {
    const auto path = environment("CLAUSE_CALL_TRACE");
    auto *file = path.empty() ? nullptr : open_file(path);
    if (!file) {
        return nullptr;
    }
    auto *result = new Sink();
    result->file = file;
    result->quiet = quiet_prefixes();
    result->all = environment("CLAUSE_CALL_TRACE_ALL") == "1";
    result->first = environment("CLAUSE_CALL_TRACE_FIRST") == "1";
    return result;
}

// The sink, opened on first use and kept for the program's lifetime.
Sink *sink_instance() {
    static Sink *const instance = open_sink();
    return instance;
}

// The open sink; only a CallTrace, which exists only while tracing, uses it.
Sink &sink() { return *sink_instance(); }

std::string_view atom(const abi::v1::ModuleDescriptor &module, const std::size_t slot) {
    return slot < module.atom_count && module.atoms[slot].spelling
               ? std::string_view(module.atoms[slot].spelling, module.atoms[slot].size)
               : std::string_view{};
}

// module:function/arity of a frame: generated functions from their module's atoms, builtins from the registry.
std::string name(ProcessContext &owner, const abi::v1::FrameDescriptor *frame) {
    if (!frame) {
        return "(none)";
    }
    if (frame->module) {
        return std::string(atom(*frame->module, frame->module_atom)) + ":" +
               std::string(atom(*frame->module, frame->function_atom)) + "/" + std::to_string(frame->arity);
    }
    if (frame->body) {
        return "(runtime)";
    }
    auto &names = sink().names;
    const auto found = names.find(frame);
    if (found != names.end()) {
        return found->second;
    }
    auto text = owner.code_server().builtins().name_of(*frame);
    if (text.empty()) {
        text = "(continuation)";
    }
    names.emplace(frame, text);
    return text;
}

// Whether a module name belongs to the program's own code.
bool quiet(const std::string &qualified) {
    const auto module = std::string_view(qualified).substr(0, qualified.find(':'));
    if (module.starts_with("(")) {
        return true;
    }
    for (const auto &prefix : sink().quiet) {
        if (module.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

// Bounded printer mirroring the OTP-side tracer (call_trace.erl in the session scratchpad).
class Printer final {
  public:
    explicit Printer(std::string &out) : out_(out) {}

    void term(const Term &value, const int depth) {
        if (depth == 0) {
            out_ += "_";
        } else if (const auto text = scalar(value)) {
            out_ += *text;
        } else if (value.is_bitstring()) {
            bits(value);
        } else if (value.is_nil() || value.is_cons()) {
            list(value, depth);
        } else if (value.is_tuple()) {
            tuple(value, depth);
        } else if (value.is_map()) {
            map(value, depth);
        } else {
            out_ += "#Other";
        }
    }

  private:
    // The text of a number or atom, or the placeholder of a value printed without contents; none for containers.
    static std::optional<std::string> scalar(const Term &value) {
        if (value.is_integer() || value.is_float() || value.is_atom()) {
            return format_term(value, TermStyle::write, 400).value_or("?");
        }
        if (value.is_function()) {
            return "#Fun";
        }
        if (value.is_pid()) {
            return "#Pid";
        }
        if (value.is_port()) {
            return "#Port";
        }
        if (value.is_reference()) {
            return "#Ref";
        }
        return std::nullopt;
    }

    void bits(const Term &value) {
        if (!value.is_binary()) {
            out_ += "#Bits";
            return;
        }
        const auto bytes = value.binary_bytes();
        if (!bytes) {
            out_ += "#BadBinary";
        } else if (bytes->size() <= PRINT_BINARY) {
            prefix(*bytes, bytes->size());
        } else {
            out_ += "<<" + std::to_string(bytes->size()) + "B:";
            prefix(*bytes, PRINT_BINARY);
            out_ += ">>";
        }
    }

    // The first `size` bytes as a quoted string when all are printable ASCII, else as decimals.
    void prefix(const std::vector<std::byte> &bytes, const std::size_t size) {
        const auto shown = std::span(bytes).first(size);
        if (std::ranges::all_of(shown,
                                [](std::byte item) { return item >= std::byte{32} && item <= std::byte{126}; })) {
            quoted(shown);
        } else {
            decimals(shown);
        }
    }

    void quoted(std::span<const std::byte> bytes) {
        out_ += "<<\"";
        for (const auto item : bytes) {
            const auto code = static_cast<char>(item);
            if (code == '"' || code == '\\') {
                out_ += '\\';
            }
            out_ += code;
        }
        out_ += "\">>";
    }

    void decimals(std::span<const std::byte> bytes) {
        out_ += "<<";
        for (std::size_t at = 0; at < bytes.size(); ++at) {
            out_ += (at == 0 ? "" : ",") + std::to_string(static_cast<unsigned char>(bytes[at]));
        }
        out_ += ">>";
    }

    void list(Term value, const int depth) {
        out_ += "[";
        std::size_t count = 0;
        while (value.is_cons()) {
            if (count == PRINT_WIDTH) {
                out_ += "|...]";
                return;
            }
            out_ += count++ == 0 ? "" : ",";
            term(value.head().value(), depth - 1);
            value = value.tail().value();
        }
        if (!value.is_nil()) {
            out_ += "|";
            term(value, depth - 1);
        }
        out_ += "]";
    }

    void tuple(const Term &value, const int depth) {
        const auto size = value.tuple_size().value_or(0);
        out_ += "{";
        for (std::size_t at = 0; at < size; ++at) {
            if (at == PRINT_WIDTH) {
                out_ += ",...";
                break;
            }
            out_ += at == 0 ? "" : ",";
            term(value.tuple_element(at).value(), depth - 1);
        }
        out_ += "}";
    }

    void map(const Term &value, const int depth) {
        const auto size = value.map_size().value_or(0);
        if (size > PRINT_MAP_SIZE) {
            out_ += "#{" + std::to_string(size) + "}";
            return;
        }
        out_ += "#{";
        std::size_t count = 0;
        for (const auto &[key, item] : value.map_entries().value()) {
            out_ += count++ == 0 ? "" : ",";
            term(key, depth - 1);
            out_ += "=>";
            term(item, depth - 1);
        }
        out_ += "}";
    }

    std::string &out_;
};
} // namespace

bool call_trace_enabled() noexcept {
    try {
        return sink_instance() != nullptr;
    } catch (...) {
        return false;
    }
}

std::unique_ptr<CallTrace> CallTrace::create(ProcessContext &owner) {
    return call_trace_enabled() ? std::make_unique<CallTrace>(owner) : nullptr;
}

void CallTrace::line(const std::string_view text) const noexcept {
    auto &out = sink();
    const std::scoped_lock lock(out.mutex);
    static_cast<void>(std::fprintf(out.file, "%*s%.*s\n", static_cast<int>(open_.size() * 2), "",
                                   static_cast<int>(text.size()), text.data()));
}

std::string CallTrace::text(const Word word) const noexcept {
    try {
        auto value = Term::from_word(word, owner_);
        if (!value) {
            return "#Unadmitted";
        }
        std::string result;
        Printer(result).term(*value, PRINT_DEPTH);
        return result;
    } catch (...) {
        return "#PrintFailed";
    }
}

void CallTrace::note_first(const abi::v1::FrameDescriptor &callee) const {
    auto &out = sink();
    if (!out.first) {
        return;
    }
    bool fresh = false;
    {
        const std::scoped_lock lock(out.mutex);
        fresh = out.seen.insert(&callee).second;
    }
    if (fresh) {
        line("FIRST " + name(owner_, &callee));
    }
}

bool CallTrace::release(const std::size_t frame, const std::size_t replaced) {
    // Frames at or above the new one were released by unwinding.
    while (!open_.empty() && open_.back().frame != NONE && frame != NONE && open_.back().frame >= frame &&
           !(replaced != NONE && open_.back().frame == replaced)) {
        open_.pop_back();
    }
    return replaced != NONE && !open_.empty() && open_.back().frame == replaced;
}

void CallTrace::enter(const abi::v1::FrameDescriptor *caller, const abi::v1::FrameDescriptor &callee,
                      const std::size_t frame, const std::size_t replaced, const Word *arguments) noexcept {
    try {
        note_first(callee);
        if (release(frame, replaced)) {
            return; // A boundary call continues in its own tail call.
        }
        const auto callee_name = name(owner_, &callee);
        if (sink().all) {
            line(". " + callee_name);
        }
        if (!quiet(name(owner_, caller)) || quiet(callee_name)) {
            return;
        }
        auto text = "CALL " + callee_name + "(";
        for (std::size_t at = 0; at < callee.arity; ++at) {
            text += (at == 0 ? "" : ", ") + this->text(arguments[at]);
        }
        line(text + ")");
        open_.push_back({frame, callee_name});
    } catch (...) {
        ++dropped_;
    }
}

void CallTrace::builtin_done(const Word result, const bool failed, const bool trapped) noexcept {
    if (open_.empty() || open_.back().frame != NONE) {
        return;
    }
    const auto open = std::move(open_.back());
    open_.pop_back();
    try {
        line(failed ? "EXC " + open.name : "RET " + open.name + " -> " + (trapped ? "#Trapped" : text(result)));
    } catch (...) {
        ++dropped_;
    }
}

void CallTrace::leave(const std::size_t frame, const Outcome outcome) noexcept {
    while (!open_.empty() && open_.back().frame != NONE && open_.back().frame > frame) {
        open_.pop_back();
    }
    if (open_.empty() || open_.back().frame != frame) {
        return;
    }
    const auto open = std::move(open_.back());
    open_.pop_back();
    try {
        line(outcome.failed ? "EXC " + open.name : "RET " + open.name + " -> " + text(outcome.result));
    } catch (...) {
        ++dropped_;
    }
}

void CallTrace::failure(const abi::v1::FrameDescriptor *running, const CallFailure &failure) noexcept {
    try {
        auto text = "RAISE in " + name(owner_, running) + " code=" + std::to_string(static_cast<int>(failure.code));
        if (failure.reason) {
            text += " reason=" + std::to_string(static_cast<int>(*failure.reason));
        }
        if (failure.status) {
            text += " status=" + std::to_string(static_cast<int>(*failure.status));
        }
        if (failure.value) {
            text += " value=" + this->text(failure.value->word());
        }
        line(text);
    } catch (...) {
        ++dropped_;
    }
}
} // namespace clause::runtime::detail
