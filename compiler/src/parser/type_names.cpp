#include "forms.hpp"
#include <algorithm>

namespace clause {
namespace {
// Pin parser-only builtin classification to erl_internal:is_type/2 at the grammar baseline.
bool predefined(std::u32string_view name, std::size_t arity) {
    static constexpr std::pair<std::u32string_view, std::size_t> types[]{{U"any", 0},
                                                                         {U"arity", 0},
                                                                         {U"atom", 0},
                                                                         {U"binary", 0},
                                                                         {U"bitstring", 0},
                                                                         {U"bool", 0},
                                                                         {U"boolean", 0},
                                                                         {U"byte", 0},
                                                                         {U"char", 0},
                                                                         {U"dynamic", 0},
                                                                         {U"float", 0},
                                                                         {U"function", 0},
                                                                         {U"identifier", 0},
                                                                         {U"integer", 0},
                                                                         {U"iodata", 0},
                                                                         {U"iolist", 0},
                                                                         {U"list", 0},
                                                                         {U"list", 1},
                                                                         {U"map", 0},
                                                                         {U"maybe_improper_list", 0},
                                                                         {U"maybe_improper_list", 2},
                                                                         {U"mfa", 0},
                                                                         {U"module", 0},
                                                                         {U"neg_integer", 0},
                                                                         {U"nil", 0},
                                                                         {U"no_return", 0},
                                                                         {U"node", 0},
                                                                         {U"non_neg_integer", 0},
                                                                         {U"none", 0},
                                                                         {U"nonempty_binary", 0},
                                                                         {U"nonempty_bitstring", 0},
                                                                         {U"nonempty_improper_list", 2},
                                                                         {U"nonempty_list", 0},
                                                                         {U"nonempty_list", 1},
                                                                         {U"nonempty_maybe_improper_list", 0},
                                                                         {U"nonempty_maybe_improper_list", 2},
                                                                         {U"nonempty_string", 0},
                                                                         {U"number", 0},
                                                                         {U"pid", 0},
                                                                         {U"port", 0},
                                                                         {U"pos_integer", 0},
                                                                         {U"record", 0},
                                                                         {U"reference", 0},
                                                                         {U"string", 0},
                                                                         {U"term", 0},
                                                                         {U"timeout", 0},
                                                                         {U"tuple", 0}};
    // Kept sorted for the binary search (optimization step OPTM.1).
    static_assert(std::ranges::is_sorted(types));
    return std::ranges::binary_search(types, std::pair{name, arity});
}
} // namespace

ast::TypeValue FormParser::named_type() {
    const auto token = category(cursor_.anchor().kind, "type literal");
    switch (token.kind) {
    case TokenKind::variable:
        return ast::Variable{value<std::u32string>(token)};
    case TokenKind::integer:
        return ast::IntegerLiteral{value<Integer>(token)};
    case TokenKind::character:
        return std::get<ast::CharacterLiteral>(literal_value(token));
    case TokenKind::atom:
        break;
    default:
        fail(DiagnosticCode::parser_syntax, "expected type syntax");
    }
    ast::Atom name{value<std::u32string>(token)};
    std::optional<ast::Atom> module;
    if (cursor_.take_syntax(U":")) {
        module = std::move(name);
        name = ast::Atom{value<std::u32string>(category(TokenKind::atom, "remote type name"))};
        expect(U"(");
    } else if (!cursor_.take_syntax(U"(")) {
        return name;
    }
    auto arguments = type_elements(U")");
    return type_application(std::move(module), std::move(name), std::move(arguments));
}

ast::TypeValue FormParser::type_application(std::optional<ast::Atom> module, ast::Atom name,
                                            std::vector<ast::TypeId> arguments) {
    if (!module && arguments.empty()) {
        if (name.name == U"tuple") {
            return ast::TupleType{.any = true, .elements = {}};
        }
        if (name.name == U"map") {
            return ast::MapType{.any = true, .fields = {}};
        }
    }
    const bool builtin = !module && predefined(name.name, arguments.size());
    return ast::TypeApplication{
        .module = std::move(module), .name = std::move(name), .predefined = builtin, .arguments = std::move(arguments)};
}
} // namespace clause
