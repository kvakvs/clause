#include "placeholders.hpp"
#include "../project/paths.hpp"
#include <clause/compiler/printing.hpp>
#include <clause/compiler/source.hpp>

namespace clause::cli {
namespace {
// Anonymous parameters for one clause head: "_, _, _".
std::string parameters(const std::size_t arity) {
    std::string result;
    for (std::size_t i = 0; i < arity; ++i) {
        result += i == 0 ? "_" : ", _";
    }
    return result;
}

// The frontend adds module_info/0,1 to every compiled module; a placeholder must not define them again.
bool predefined(const semantic::FunctionKey &key) { return key.name == U"module_info" && key.arity <= 1; }
} // namespace

std::string placeholder_source(const std::u32string &module, const std::set<semantic::FunctionKey> &functions) {
    const auto name = atom_source(utf8(module));
    std::string exports;
    std::string clauses;
    for (const auto &key : functions) {
        if (predefined(key)) {
            continue;
        }
        const auto function = atom_source(utf8(key.name));
        exports.append(exports.empty() ? "" : ", ").append(function).append("/").append(std::to_string(key.arity));
        clauses.append(function).append("(").append(parameters(key.arity)).append(") -> {module_ignored, ");
        clauses.append(name).append("}.\n");
    }
    std::string result = "-module(" + name + ").\n";
    result.append("-export([").append(exports).append("]).\n").append(clauses);
    return result;
}

std::filesystem::path placeholder_path(const std::u32string &module) {
    return std::filesystem::path(semantic::IGNORED_MODULE_DIRECTORY) / project::native_path(utf8(module) + ".erl");
}
} // namespace clause::cli
