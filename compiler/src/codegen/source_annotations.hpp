#pragma once
#include "source_locations.hpp"
#include <clause/compiler/ast/module.hpp>
#include <cstddef>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class DebugLoc;
class Module;
class raw_ostream;
} // namespace llvm

namespace clause::codegen {
// One physical Erlang source line an instruction came from, for IR comments and assembly listings.
struct SourceLine {
    // Physical file name as the preprocessor recorded it, escaped to stay on one comment line.
    std::string file_;
    // One-based line number within that file.
    std::size_t line_ = 0;
    // The line's text, escaped to stay on one comment line.
    std::string text_;
};

// Find the source line a location names; synthetic or unmapped locations give nothing.
std::optional<SourceLine> source_line_of(const llvm::DebugLoc &location, const SourceScopes &sources);

// Prepare source comments before entering LLVM's assembly writer, which must not receive C++ exceptions.
class SourceAnnotations final : public llvm::AssemblyAnnotationWriter {
  public:
    // Match surviving instruction locations to owned sources and bound aggregate comment storage.
    SourceAnnotations(const llvm::Module &module, const ast::Module &syntax, const SourceScopes &sources,
                      std::size_t capacity);
    // List physical source filenames once at the beginning of the textual snapshot.
    void print_sources(llvm::raw_ostream &stream) const;
    // Append the original source line beside its instruction without allocating inside LLVM.
    void printInfoComment(const llvm::Value &value, llvm::formatted_raw_ostream &stream) noexcept override;

  private:
    // Own the bounded filename header separately from instruction comments.
    std::vector<std::byte> header_;
    // Own preformatted comments for this snapshot; LLVM instructions remain borrowed until printing ends.
    std::map<const llvm::Value *, std::vector<std::byte>> comments_;
};
} // namespace clause::codegen
