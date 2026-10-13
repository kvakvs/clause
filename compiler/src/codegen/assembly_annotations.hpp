#pragma once
#include "source_annotations.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clause::codegen {
// One IR instruction of a listed module; line N of the listing's line table names instruction N.
// Created for --print-asm.
struct IrLine {
    // The instruction as LLVM prints it, folded to one line and without its debug location.
    std::string text_;
    // The Erlang line lowering recorded for the instruction, if any.
    std::optional<SourceLine> source_;
    // Index of the instruction's function, so Erlang comments restart in every function.
    std::size_t function_ = 0;
};

// Comment each run of machine code with the IR instruction and Erlang line it came from, read from the line-table
// directives; drops those directives and the debug sections that exist only to carry them.
std::vector<std::byte> annotate_assembly(std::string_view assembly, std::span<const IrLine> lines,
                                         std::string_view comment, std::size_t capacity);
} // namespace clause::codegen
