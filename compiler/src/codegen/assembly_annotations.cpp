#include "assembly_annotations.hpp"
#include "bounded_stream.hpp"
#include <array>
#include <charconv>
#include <limits>
#include <set>

namespace clause::codegen {
namespace {
constexpr std::string_view BLANKS = " \t";
// Local labels the assembly printer adds for line tables, or that only line tables reference; Mach-O drops the dot.
constexpr std::array<std::string_view, 3> LINE_TABLE_LABELS{"Ltmp", "Lfunc_begin", "Lfunc_end"};
using Labels = std::set<std::string_view, std::less<>>;

// The line without its leading indentation.
std::string_view trimmed(const std::string_view line) {
    const auto begin = line.find_first_not_of(BLANKS);
    return begin == std::string_view::npos ? std::string_view{} : line.substr(begin);
}

// The whitespace-separated field `index` of a trimmed line; field 0 is the directive or mnemonic.
std::string_view field(std::string_view line, std::size_t index) {
    for (; index != 0; --index) {
        line = trimmed(line.substr(std::min(line.find_first_of(BLANKS), line.size())));
    }
    return line.substr(0, line.find_first_of(BLANKS));
}

// The line a DWARF `.loc FILE LINE` or CodeView `.cv_loc FUNCTION FILE LINE` directive names.
std::optional<std::size_t> location_line(const std::string_view line) {
    const auto directive = field(line, 0);
    const auto text = directive == ".loc" ? field(line, 2) : directive == ".cv_loc" ? field(line, 3) : "";
    std::size_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return {};
    }
    return number;
}

// Whether a section switch enters debug information (ELF/COFF `.debug*`, Mach-O `__DWARF`); nothing for others.
std::optional<bool> debug_section(const std::string_view line) {
    const auto directive = field(line, 0);
    if (directive == ".text" || directive == ".data" || directive == ".bss") {
        return false;
    }
    if (directive != ".section") {
        return {};
    }
    const auto name = field(line, 1);
    return name.starts_with(".debug") || name.starts_with("__DWARF");
}

// Debug file and CodeView bookkeeping directives that only feed the listing's own line table.
bool debug_directive(const std::string_view line) {
    const auto directive = field(line, 0);
    return directive == ".file" || directive.starts_with(".cv_");
}

// The line-table label starting at `text`, with its optional dot; empty when `text` starts none.
std::string_view line_table_label(const std::string_view text) {
    const auto dot = text.starts_with('.') ? std::size_t{1} : std::size_t{0};
    const auto name = text.substr(dot);
    for (const auto prefix : LINE_TABLE_LABELS) {
        if (name.starts_with(prefix)) {
            const auto end = std::min(name.find_first_not_of("0123456789", prefix.size()), name.size());
            return end > prefix.size() ? text.substr(0, dot + end) : std::string_view{};
        }
    }
    return {};
}

// The label a line defines when it consists of one line-table label definition only.
std::string_view defined_label(const std::string_view line) {
    const auto label = line_table_label(line);
    return !label.empty() && line.substr(label.size()) == ":" ? label : std::string_view{};
}

// Collect the line-table labels kept code still uses, outside the dropped debug sections and their own definitions.
Labels used_labels(const std::span<const std::string_view> lines) {
    Labels used;
    bool in_debug_section = false;
    for (const auto line : lines) {
        const auto text = trimmed(line);
        in_debug_section = debug_section(text).value_or(in_debug_section);
        if (in_debug_section || !defined_label(text).empty()) {
            continue;
        }
        for (auto at = text.find('L'); at != std::string_view::npos; at = text.find('L', at + 1)) {
            const auto begin = at != 0 && text[at - 1] == '.' ? at - 1 : at;
            if (const auto label = line_table_label(text.substr(begin)); !label.empty()) {
                used.insert(label);
            }
        }
    }
    return used;
}

// Copies assembly lines, announcing each new IR origin where a line-table directive stood.
class Annotator {
  public:
    // Borrow the numbered instructions; `comment` is the target's assembly comment marker.
    Annotator(const std::span<const IrLine> lines, const Labels &used, const std::string_view comment,
              const std::size_t capacity)
        : lines_(lines), used_(used), comment_(comment), stream_(capacity) {}

    // Copy one assembly line unless it is debug bookkeeping.
    void add(const std::string_view line) {
        const auto text = trimmed(line);
        if (const auto debug = debug_section(text)) {
            in_debug_section_ = *debug;
        }
        if (in_debug_section_ || unused_label(text)) {
            return;
        }
        if (const auto number = location_line(text)) {
            locate(*number);
        } else if (!debug_directive(text)) {
            stream_ << line << '\n';
        }
    }

    // Transfer the listing; throws when it outgrew the artifact byte limit.
    std::vector<std::byte> take() { return stream_.take_bytes(); }

  private:
    // Whether the line only defines a line-table label nothing kept refers to.
    bool unused_label(const std::string_view text) const {
        const auto label = defined_label(text);
        return !label.empty() && !used_.contains(label);
    }

    // Switch to instruction `number`; line 0 marks machine code without an IR origin.
    void locate(const std::size_t number) {
        const auto *origin = number == 0 || number > lines_.size() ? nullptr : &lines_[number - 1];
        if (origin != origin_ && origin) {
            announce(*origin);
        }
        origin_ = origin;
    }

    // Name the Erlang line when it changed within the function, then the IR instruction.
    void announce(const IrLine &origin) {
        if (origin.function_ != function_) {
            function_ = origin.function_;
            source_ = nullptr;
        }
        const auto &source = origin.source_;
        if (source && (!source_ || source_->file_ != source->file_ || source_->line_ != source->line_)) {
            source_ = &*source;
            stream_ << comment_ << " erlang " << source->file_ << ':' << source->line_ << ": " << source->text_
                    << '\n';
        }
        stream_ << comment_ << "     ir " << origin.text_ << '\n';
    }

    // The numbered instructions, by line-table line minus one.
    std::span<const IrLine> lines_;
    // The line-table labels kept code refers to.
    const Labels &used_;
    // The target's comment marker, opening every annotation.
    std::string_view comment_;
    // The bounded listing being written.
    BoundedStream stream_;
    // Whether the current section holds debug information, whose lines are dropped.
    bool in_debug_section_ = false;
    // The instruction the following machine code came from; null before the first or for line 0.
    const IrLine *origin_ = nullptr;
    // The last Erlang line announced in the current function.
    const SourceLine *source_ = nullptr;
    // The function of the last announced instruction.
    std::size_t function_ = std::numeric_limits<std::size_t>::max();
};
} // namespace

std::vector<std::byte> annotate_assembly(std::string_view assembly, const std::span<const IrLine> lines,
                                         const std::string_view comment, const std::size_t capacity) {
    std::vector<std::string_view> split;
    while (!assembly.empty()) {
        const auto end = std::min(assembly.find('\n'), assembly.size());
        split.push_back(assembly.substr(0, end));
        assembly.remove_prefix(std::min(end + 1, assembly.size()));
    }
    const auto used = used_labels(split);
    Annotator annotator(lines, used, comment, capacity);
    for (const auto line : split) {
        annotator.add(line);
    }
    return annotator.take();
}
} // namespace clause::codegen
