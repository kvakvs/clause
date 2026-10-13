#include "source_annotations.hpp"
#include "bounded_stream.hpp"
#include "source_locations.hpp"
#include <algorithm>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FormattedStream.h>
#include <optional>
#include <string_view>

namespace clause::codegen {
namespace {
using Sources = std::map<std::string, SourcePtr, std::less<>>;

// Keep physical buffers from the parsing session, including includes and macro invocation sites.
Sources source_files(const ast::Module &syntax) {
    Sources sources;
    for (const auto &id : syntax.forms()) {
        for (const auto &origin : syntax.extent(syntax.form(id).source)) {
            const auto &site = source_site(origin);
            if (site.source) {
                sources.try_emplace(site.source->name, site.source);
            }
        }
    }
    return sources;
}

// Locate a physical line using the existing position table without rescanning the file per instruction.
std::optional<std::u32string_view> source_line(const Source &source, const std::size_t line) {
    std::size_t begin = 0;
    auto end = source.text.size();
    while (begin < end) {
        const auto middle = begin + (end - begin) / 2;
        if (source.position(middle).line < line) {
            begin = middle + 1;
        } else {
            end = middle;
        }
    }
    if (source.position(begin).line != line) {
        return {};
    }
    const std::u32string_view text(source.text);
    const auto newline = text.find(U'\n', begin);
    auto contents = text.substr(begin, (newline == text.npos ? text.size() : newline) - begin);
    if (contents.ends_with(U'\r')) {
        contents.remove_suffix(1);
    }
    return contents;
}

// Prevent source controls or path delimiters from escaping a single LLVM comment line.
void escaped_text(llvm::raw_ostream &stream, const std::string_view text, const bool quoted) {
    constexpr std::string_view digits = "0123456789abcdef";
    for (const unsigned char byte : text) {
        const bool delimiter = quoted && (byte == '\\' || byte == '"');
        const bool control = (byte < 32 && byte != '\t') || byte == 127;
        if (control || delimiter) {
            stream << "\\x" << digits[byte >> 4U] << digits[byte & 15U];
        } else {
            stream << static_cast<char>(byte);
        }
    }
}

// Convert retained decoded text in small chunks so Latin-1 and UTF-8 inputs share UTF-8 output.
void source_text(llvm::raw_ostream &stream, std::u32string_view text) {
    while (!text.empty()) {
        const auto count = std::min<std::size_t>(text.size(), 1024);
        escaped_text(stream, utf8(text.substr(0, count)), false);
        text.remove_prefix(count);
    }
}

// List each physical source once, keeping unusual filename characters inside one comment.
std::vector<std::byte> source_header(const Sources &sources, const std::size_t capacity) {
    BoundedStream stream(capacity);
    stream << "; Erlang source files:\n";
    for (const auto &[name, source] : sources) {
        stream << "; \"";
        escaped_text(stream, name, true);
        stream << "\"\n";
    }
    return stream.take_bytes();
}

// Prepare one complete comment before entering LLVM; unmapped synthetic instructions stay unannotated.
std::vector<std::byte> source_comment(const llvm::DebugLoc &location, const SourceScopes &sources,
                                      const std::size_t capacity) {
    const auto line = source_line_of(location, sources);
    if (!line) {
        return {};
    }
    BoundedStream stream(capacity);
    stream << " ; " << line->text_;
    return stream.take_bytes();
}
} // namespace

std::optional<SourceLine> source_line_of(const llvm::DebugLoc &location, const SourceScopes &sources) {
    const auto found = sources.find(location.getScope());
    if (found == sources.end()) {
        return {};
    }
    const auto &source = *found->second;
    const auto text = source_line(source, location.getLine());
    if (!text) {
        return {};
    }
    SourceLine result{.file_ = {}, .line_ = location.getLine(), .text_ = {}};
    llvm::raw_string_ostream file(result.file_);
    escaped_text(file, source.name, false);
    llvm::raw_string_ostream contents(result.text_);
    source_text(contents, *text);
    return result;
}

SourceAnnotations::SourceAnnotations(const llvm::Module &module, const ast::Module &syntax, const SourceScopes &sources,
                                     std::size_t capacity) {
    header_ = source_header(source_files(syntax), capacity);
    capacity -= header_.size();
    for (const auto &function : module) {
        std::pair<const llvm::DILocation *, const llvm::BasicBlock *> previous{};
        for (const auto &instruction : llvm::instructions(function)) {
            const auto *location = instruction.getDebugLoc().get();
            const std::pair current{location, instruction.getParent()};
            if (location && current != previous) {
                auto comment = source_comment(instruction.getDebugLoc(), sources, capacity);
                capacity -= comment.size();
                comments_.emplace(&instruction, std::move(comment));
            }
            previous = current;
        }
    }
}

void SourceAnnotations::print_sources(llvm::raw_ostream &stream) const {
    stream.write(reinterpret_cast<const char *>(header_.data()), header_.size());
}

void SourceAnnotations::printInfoComment(const llvm::Value &value, llvm::formatted_raw_ostream &stream) noexcept {
    const auto found = comments_.find(&value);
    if (found != comments_.end() && !found->second.empty()) {
        const auto &bytes = found->second;
        stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
}
} // namespace clause::codegen
