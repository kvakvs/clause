#include "call_trace.hpp"
#include <algorithm>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/output.hpp>
#include <clause/runtime/process_context.hpp>
#include <cstdio>
#include <cstdlib>
#include <mutex>
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

// Open the sink once from CLAUSE_CALL_TRACE, CLAUSE_CALL_TRACE_QUIET (comma separated) and CLAUSE_CALL_TRACE_ALL.
Sink &sink() {
    static Sink *instance = [] {
        auto *result = new Sink();
        const auto path = environment("CLAUSE_CALL_TRACE");
        if (!path.empty()) {
#ifdef _WIN32
            if (fopen_s(&result->file, path.c_str(), "wb") != 0) {
                result->file = nullptr;
            }
#else
            result->file = std::fopen(path.c_str(), "wb");
#endif
        }
        auto quiet = environment("CLAUSE_CALL_TRACE_QUIET");
        if (quiet.empty()) {
            quiet = "luerl,ttdict,ttsets";
        }
        for (std::size_t at = 0; at <= quiet.size();) {
            const auto end = std::min(quiet.find(',', at), quiet.size());
            if (end > at) {
                result->quiet.push_back(quiet.substr(at, end - at));
            }
            at = end + 1;
        }
        result->all = environment("CLAUSE_CALL_TRACE_ALL") == "1";
        result->first = environment("CLAUSE_CALL_TRACE_FIRST") == "1";
        return result;
    }();
    return *instance;
}

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
        } else if (value.is_integer() || value.is_float() || value.is_atom()) {
            out_ += format_term(value, TermStyle::write, 400).value_or("?");
        } else if (value.is_function()) {
            out_ += "#Fun";
        } else if (value.is_pid()) {
            out_ += "#Pid";
        } else if (value.is_port()) {
            out_ += "#Port";
        } else if (value.is_reference()) {
            out_ += "#Ref";
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
        const auto printable =
            std::all_of(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size),
                        [](std::byte item) { return item >= std::byte{32} && item <= std::byte{126}; });
        out_ += printable ? "<<\"" : "<<";
        for (std::size_t at = 0; at < size; ++at) {
            const auto code = static_cast<unsigned char>(bytes[at]);
            if (printable) {
                if (code == '"' || code == '\\') {
                    out_ += '\\';
                }
                out_ += static_cast<char>(code);
            } else {
                out_ += (at == 0 ? "" : ",") + std::to_string(code);
            }
        }
        out_ += printable ? "\">>" : ">>";
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
        return sink().file != nullptr;
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

void CallTrace::enter(const abi::v1::FrameDescriptor *caller, const abi::v1::FrameDescriptor &callee,
                      const std::size_t frame, const std::size_t replaced, const Word *arguments) noexcept {
    try {
        // Frames at or above the new one were released by unwinding.
        if (auto &out = sink(); out.first) {
            bool fresh = false;
            {
                const std::scoped_lock lock(out.mutex);
                fresh = out.seen.insert(&callee).second;
            }
            if (fresh) {
                line("FIRST " + name(owner_, &callee));
            }
        }
        while (!open_.empty() && open_.back().frame != NONE && frame != NONE && open_.back().frame >= frame &&
               !(replaced != NONE && open_.back().frame == replaced)) {
            open_.pop_back();
        }
        if (replaced != NONE && !open_.empty() && open_.back().frame == replaced) {
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
        // Tracing never fails the program.
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
    }
}

void CallTrace::leave(const std::size_t frame, const Word result, const bool failed) noexcept {
    while (!open_.empty() && open_.back().frame != NONE && open_.back().frame > frame) {
        open_.pop_back();
    }
    if (open_.empty() || open_.back().frame != frame) {
        return;
    }
    const auto open = std::move(open_.back());
    open_.pop_back();
    try {
        line(failed ? "EXC " + open.name : "RET " + open.name + " -> " + text(result));
    } catch (...) {
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
    }
}
} // namespace clause::runtime::detail
