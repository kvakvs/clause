#include "module_info.hpp"
#include "calls.hpp"
#include <algorithm>
#include <clause/compiler/printing.hpp>
#include <set>

namespace clause::semantic {
namespace {
// Generic attributes OTP's compiler consumes or drops instead of keeping them for module_info(attributes).
constexpr std::array<std::u32string_view, 6> DROPPED_ATTRIBUTES{U"compile",       U"export_type", U"optional_callbacks",
                                                                U"export_record", U"docformat",   U"feature"};
// module_info/0 gives the first five module_info/1 keys.
constexpr std::size_t SUMMARY_KEYS = 5;

// The functions the module defines, in definition order.
std::vector<FunctionKey> defined_functions(const ast::Module &syntax) {
    std::vector<FunctionKey> result;
    for (const auto &id : syntax.forms()) {
        const auto *function = std::get_if<ast::Function>(&syntax.form(id).value);
        if (!function) {
            continue;
        }
        // The search cost here is quadratic but the volume of search is small so the impact is also small
        FunctionKey key{function->name.name, function->clauses.front().arguments.size()};
        if (!std::ranges::contains(result, key)) {
            result.push_back(std::move(key));
        }
    }
    return result;
}

// The functions the -export attributes list, and main/1 of an escript.
std::set<FunctionKey> exported_functions(const ast::Module &syntax, const bool escript) {
    std::set<FunctionKey> result;
    for (const auto &id : syntax.forms()) {
        const auto *exports = std::get_if<ast::ExportAttribute>(&syntax.form(id).value);
        for (const auto &entry : exports ? exports->functions : std::vector<ast::NameArity>{}) {
            if (const auto count = arity(entry.arity)) {
                result.insert({entry.name.name, *count});
            }
        }
    }
    if (escript) {
        result.insert({U"main", 1});
    }
    return result;
}

// A list of {Name, Arity} tuples as Erlang source text.
std::string keys_source(const std::vector<FunctionKey> &keys) {
    std::string result;
    for (const auto &key : keys) {
        result += (result.empty() ? "{" : ", {") + atom_source(utf8(key.name)) + ", " + std::to_string(key.arity) + "}";
    }
    return "[" + result + "]";
}

// One kept attribute as {Name, Values}; OTP wraps a value that is not a list in one.
std::string attribute_source(const ast::Module &syntax, const ast::GenericAttribute &attribute) {
    const auto value = term_source(syntax, attribute.value);
    const bool list = std::holds_alternative<ast::TermList>(syntax.term(attribute.value).value);
    return "{" + atom_source(utf8(attribute.name.name)) + ", " + (list ? value : "[" + value + "]") + "}";
}

// The digest as an unsigned big-endian integer in decimal, as OTP derives a missing -vsn from the MD5.
std::string digest_integer(std::array<std::uint8_t, 16> digest) {
    std::string digits;
    while (std::ranges::any_of(digest, [](const std::uint8_t byte) { return byte != 0; })) {
        unsigned remainder = 0;
        for (auto &byte : digest) {
            const unsigned value = (remainder << 8U) | byte;
            byte = static_cast<std::uint8_t>(value / 10);
            remainder = value % 10;
        }
        digits.insert(digits.begin(), static_cast<char>('0' + remainder));
    }
    return digits.empty() ? "0" : digits;
}

// The attributes module_info(attributes) keeps, in source order, after an MD5-derived vsn when there is no -vsn.
std::string attributes_source(const ast::Module &syntax, const ModuleFacts &facts) {
    std::vector<std::string> kept;
    bool vsn = false;
    for (const auto &id : syntax.forms()) {
        const auto *attribute = std::get_if<ast::GenericAttribute>(&syntax.form(id).value);
        if (attribute && !std::ranges::contains(DROPPED_ATTRIBUTES, attribute->name.name)) {
            vsn = vsn || attribute->name.name == U"vsn";
            kept.push_back(attribute_source(syntax, *attribute));
        }
    }
    if (!vsn) {
        kept.insert(kept.begin(), "{vsn, [" + digest_integer(facts.md5_) + "]}");
    }
    std::string result;
    for (const auto &item : kept) {
        result += (result.empty() ? "" : ", ") + item;
    }
    return "[" + result + "]";
}

// The digest as a binary literal.
std::string digest_source(const std::array<std::uint8_t, 16> &digest) {
    std::string result;
    for (const auto byte : digest) {
        result += (result.empty() ? "" : ",") + std::to_string(byte);
    }
    return "<<" + result + ">>";
}

// The functions OTP adds: behaviour_info/1 when generated, then module_info/0,1.
std::vector<FunctionKey> predefined(const bool behaviour_info) {
    std::vector<FunctionKey> result;
    if (behaviour_info) {
        result.push_back({U"behaviour_info", 1});
    }
    result.push_back({U"module_info", 0});
    result.push_back({U"module_info", 1});
    return result;
}

using InfoValues = std::vector<std::pair<std::string_view, std::string>>;

// The value of each module_info/1 key as source text; the first five are module_info/0's, in OTP's order.
InfoValues info_values(const ast::Module &syntax, const ModuleFacts &facts, const bool behaviour_info) {
    auto functions = defined_functions(syntax);
    const auto exported = exported_functions(syntax, facts.escript_);
    std::vector<FunctionKey> exports;
    std::ranges::copy_if(functions, std::back_inserter(exports),
                         [&](const auto &key) { return exported.contains(key); });
    const auto added = predefined(behaviour_info);
    exports.insert(exports.end(), added.begin(), added.end());
    functions.insert(functions.end(), added.begin(), added.end());
    return {{"module", atom_source(utf8(*declared_module(syntax)))},
            {"exports", keys_source(exports)},
            {"attributes", attributes_source(syntax, facts)},
            {"compile", "[{version, " + string_source(facts.version_) + "}, {options, []}, {source, " +
                            string_source(facts.source_) + "}]"},
            {"md5", digest_source(facts.md5_)},
            {"functions", keys_source(functions)},
            {"nifs", "[]"},
            {"native", "false"}};
}

// module_info/0: the summary as one literal list, so it makes no calls.
std::string summary_source(const InfoValues &values) {
    std::string result;
    for (std::size_t index = 0; index < SUMMARY_KEYS; ++index) {
        result += std::string(index == 0 ? "" : ",\n     ") + "{" + std::string(values[index].first) + ", " +
                  values[index].second + "}";
    }
    return "module_info() ->\n    [" + result + "].\n";
}

// module_info/1: one clause per key, then badarg for any other.
std::string clauses_source(const InfoValues &values) {
    std::string result;
    for (const auto &[key, value] : values) {
        result += "module_info(" + std::string(key) + ") -> " + value + ";\n";
    }
    return result + "module_info(_) -> erlang:error(badarg).\n";
}
} // namespace

std::string module_info_source(const ast::Module &syntax, const ModuleFacts &facts, const bool behaviour_info) {
    if (!declared_module(syntax)) {
        return {};
    }
    const auto values = info_values(syntax, facts, behaviour_info);
    std::string result;
    if (!defines_function(syntax, {U"module_info", 0})) {
        result += summary_source(values);
    }
    if (!defines_function(syntax, {U"module_info", 1})) {
        result += clauses_source(values);
    }
    return result;
}
} // namespace clause::semantic
