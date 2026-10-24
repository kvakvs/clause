#include "source_printer.hpp"

namespace clause::printing {
namespace {
// Clause lines start four columns right of their block keyword; bodies four more.
constexpr std::size_t STEP = 4;

// `Head ->` and the body below it.
std::string clause_text(const SourcePrinter &printer, const std::string &head, const std::vector<ast::ExprId> &body,
                        std::size_t indent) {
    return spaces(indent) + head + " ->\n" + printer.body(body, indent + STEP);
}

// A guard after a head, with its `when`.
std::string when(const SourcePrinter &printer, const std::optional<ast::GuardSyntax> &guard, std::size_t indent) {
    return guard ? " when " + printer.guard(*guard, indent) : std::string();
}

// The clauses of a fun: `(Args) when Guard`, prefixed by the name of a named fun.
std::string fun_head(const SourcePrinter &printer, const ast::FunExpression &fun, const ast::FunctionClause &clause,
                     std::size_t indent) {
    std::string arguments;
    for (const auto &argument : clause.arguments) {
        arguments += (arguments.empty() ? "" : ", ") + printer.pattern(argument, indent);
    }
    return (fun.name ? utf8(fun.name->name) : std::string()) + '(' + arguments + ')' +
           when(printer, clause.guard, indent);
}

// `fun` and, for a named fun, the space before its name.
std::string fun_keyword(const ast::FunExpression &fun) { return fun.name ? "fun " : "fun"; }

// A fun with one clause of one expression that fits on one line, on that line; none otherwise.
std::optional<std::string> fun_line(const SourcePrinter &printer, const ast::FunExpression &fun, std::size_t indent) {
    if (fun.clauses.size() != 1 || fun.clauses.front().body.size() != 1) {
        return std::nullopt;
    }
    const auto &clause = fun.clauses.front();
    const auto value = printer.expression(clause.body.front(), {indent, true, true});
    if (value.find('\n') != std::string::npos) {
        return std::nullopt;
    }
    return fun_keyword(fun) + fun_head(printer, fun, clause, indent) + " -> " + value + " end";
}

// A fun on one line when it fits; a one-clause fun with its body below; others with a line group per clause.
std::string fun_text(const SourcePrinter &printer, const ast::FunExpression &fun, std::size_t indent) {
    if (auto line = fun_line(printer, fun, indent)) {
        return std::move(*line);
    }
    if (fun.clauses.size() == 1) {
        const auto &clause = fun.clauses.front();
        return fun_keyword(fun) + fun_head(printer, fun, clause, indent) + " ->\n" +
               printer.body(clause.body, indent + STEP) + '\n' + spaces(indent) + "end";
    }
    std::string clauses;
    clauses.reserve(256);
    for (const auto &clause : fun.clauses) {
        clauses += (clauses.empty() ? "" : ";\n") +
                   clause_text(printer, fun_head(printer, fun, clause, indent + STEP), clause.body, indent + STEP);
    }
    return "fun\n" + clauses + '\n' + spaces(indent) + "end";
}

// One catch clause: `Class:Reason:Stack when Guard ->` and its body.
std::string catch_clause(const SourcePrinter &printer, const ast::CatchClause &clause, std::size_t indent) {
    std::string head;
    if (clause.exception_class) {
        head = printer.expression(*clause.exception_class, {.annotated = false}) + ':';
    }
    head += printer.pattern(clause.reason, indent);
    if (clause.stacktrace) {
        head += ':' + printer.expression(*clause.stacktrace, {.annotated = false});
    }
    return clause_text(printer, head + when(printer, clause.guard, indent), clause.body, indent);
}

// A try: body, then its of, catch and after parts, each under its keyword.
std::string try_text(const SourcePrinter &printer, const ast::TryExpression &attempt, std::size_t indent) {
    auto text = "try\n" + printer.body(attempt.body, indent + STEP) + '\n';
    if (attempt.of) {
        text += spaces(indent) + "of\n" + branch_clauses(printer, *attempt.of, indent + STEP) + '\n';
    }
    if (attempt.handlers) {
        std::string handlers;
        for (const auto &clause : *attempt.handlers) {
            handlers += (handlers.empty() ? "" : ";\n") + catch_clause(printer, clause, indent + STEP);
        }
        text += spaces(indent) + "catch\n" + handlers + '\n';
    }
    if (attempt.after) {
        text += spaces(indent) + "after\n" + printer.body(*attempt.after, indent + STEP) + '\n';
    }
    return text + spaces(indent) + "end";
}

// A maybe: its expressions and ?= matches, then the else clauses.
std::string maybe_text(const SourcePrinter &printer, const ast::MaybeExpression &block, std::size_t indent) {
    const auto inner = indent + STEP;
    std::string body;
    body.reserve(256);
    for (const auto &item : block.body) {
        const auto *match = std::get_if<ast::MaybeMatch>(&item);
        const auto text = match ? printer.pattern(match->pattern, inner) +
                                      " ?= " + printer.expression(match->value, {.indent = inner})
                                : printer.expression(std::get<ast::ExprId>(item), {inner, true, true});
        body += (body.empty() ? "" : ",\n") + spaces(inner) + text;
    }
    auto text = "maybe\n" + body + '\n';
    if (block.otherwise) {
        text += spaces(indent) + "else\n" + branch_clauses(printer, *block.otherwise, inner) + '\n';
    }
    return text + spaces(indent) + "end";
}

// A receive: its clauses, then its after part.
std::string receive_text(const SourcePrinter &printer, const ast::ReceiveExpression &receive, std::size_t indent) {
    auto text = std::string("receive\n");
    if (!receive.clauses.empty()) {
        text += branch_clauses(printer, receive.clauses, indent + STEP) + '\n';
    }
    if (receive.after) {
        const auto timeout = printer.expression(receive.after->timeout, {.indent = indent});
        text += spaces(indent) + "after " + timeout + " ->\n" + printer.body(receive.after->body, indent + STEP) + '\n';
    }
    return text + spaces(indent) + "end";
}

// The block families, each laid out on several lines from `indent`.
struct Control {
    const SourcePrinter &printer;
    std::size_t indent;

    std::string operator()(const ast::BlockExpression &value) const {
        return "begin\n" + printer.body(value.body, indent + STEP) + '\n' + spaces(indent) + "end";
    }

    std::string operator()(const ast::CaseExpression &value) const {
        return "case " + printer.expression(value.value, {indent, true, true}) + " of\n" +
               branch_clauses(printer, value.clauses, indent + STEP) + '\n' + spaces(indent) + "end";
    }

    std::string operator()(const ast::IfExpression &value) const {
        std::string clauses;
        clauses.reserve(256);
        for (const auto &clause : value.clauses) {
            clauses += (clauses.empty() ? "" : ";\n") +
                       clause_text(printer, printer.guard(clause.guard, indent + STEP), clause.body, indent + STEP);
        }
        return "if\n" + clauses + '\n' + spaces(indent) + "end";
    }

    std::string operator()(const ast::ReceiveExpression &value) const { return receive_text(printer, value, indent); }

    std::string operator()(const ast::FunExpression &value) const { return fun_text(printer, value, indent); }

    std::string operator()(const ast::TryExpression &value) const { return try_text(printer, value, indent); }

    std::string operator()(const ast::MaybeExpression &value) const { return maybe_text(printer, value, indent); }

    template <typename Value> std::string operator()(const Value &) const { return {}; }
};

// One simple qualifier: a filter or a generator with its arrow.
std::string qualifier_text(const SourcePrinter &printer, const ast::Qualifier &qualifier, std::size_t indent) {
    if (const auto *filter = std::get_if<ast::FilterQualifier>(&qualifier.value)) {
        return printer.expression(filter->expression, {.indent = indent});
    }
    if (const auto *list = std::get_if<ast::ListGenerator>(&qualifier.value)) {
        return printer.pattern(list->pattern, indent) + (list->strict ? " <:- " : " <- ") +
               printer.expression(list->input, {.indent = indent});
    }
    if (const auto *binary = std::get_if<ast::BinaryGenerator>(&qualifier.value)) {
        return printer.pattern(binary->pattern, indent) + (binary->strict ? " <:= " : " <= ") +
               printer.expression(binary->input, {.indent = indent});
    }
    const auto &map = std::get<ast::MapGenerator>(qualifier.value);
    return printer.pattern(map.key, indent) + " := " + printer.pattern(map.value, indent) +
           (map.strict ? " <:- " : " <- ") + printer.expression(map.input, {.indent = indent});
}

// A zip group: its members joined with `&&`.
std::string zip_text(const SourcePrinter &printer, const ast::ZippedQualifier &zip, std::size_t indent) {
    std::string result;
    for (const auto &member : zip.qualifiers) {
        result += (result.empty() ? "" : " && ") + qualifier_text(printer, member, indent);
    }
    return result;
}

// The qualifiers after `||`: zip groups join their members with `&&`.
std::string qualifiers_text(const SourcePrinter &printer, const std::vector<ast::ComprehensionQualifier> &qualifiers,
                            std::size_t indent) {
    std::string result;
    for (const auto &qualifier : qualifiers) {
        const auto *simple = std::get_if<ast::Qualifier>(&qualifier);
        const auto text = simple ? qualifier_text(printer, *simple, indent)
                                 : zip_text(printer, std::get<ast::ZippedQualifier>(qualifier), indent);
        result += (result.empty() ? "" : ", ") + text;
    }
    return result;
}

// Map fields of a map comprehension's templates.
std::string map_templates(const SourcePrinter &printer, const std::vector<ast::MapField> &fields, std::size_t indent) {
    std::string result;
    for (const auto &field : fields) {
        result += (result.empty() ? "" : ", ") + printer.expression(field.key, {.indent = indent}) +
                  (field.kind == ast::MapFieldKind::exact ? " := " : " => ") +
                  printer.expression(field.value, {.indent = indent});
    }
    return result;
}
} // namespace

std::string branch_clauses(const SourcePrinter &printer, const std::vector<ast::BranchClause> &clauses,
                           std::size_t indent) {
    std::string result;
    for (const auto &clause : clauses) {
        const auto head = printer.pattern(clause.pattern, indent) + when(printer, clause.guard, indent);
        result += (result.empty() ? "" : ";\n") + clause_text(printer, head, clause.body, indent);
    }
    return result;
}

bool control(const ast::ExprValue &value) {
    return std::holds_alternative<ast::BlockExpression>(value) || std::holds_alternative<ast::CaseExpression>(value) ||
           std::holds_alternative<ast::IfExpression>(value) || std::holds_alternative<ast::ReceiveExpression>(value) ||
           std::holds_alternative<ast::FunExpression>(value) || std::holds_alternative<ast::TryExpression>(value) ||
           std::holds_alternative<ast::MaybeExpression>(value);
}

std::string control_text(const SourcePrinter &printer, const ast::ExprValue &value, std::size_t indent) {
    return std::visit(Control{printer, indent}, value);
}

std::string comprehension_text(const SourcePrinter &printer, const ast::ExprValue &value, std::size_t indent) {
    if (const auto *list = std::get_if<ast::ListComprehension>(&value)) {
        return '[' + printer.list(list->templates, {.indent = indent}) + " || " +
               qualifiers_text(printer, list->qualifiers, indent) + ']';
    }
    if (const auto *map = std::get_if<ast::MapComprehension>(&value)) {
        return "#{" + map_templates(printer, map->templates, indent) + " || " +
               qualifiers_text(printer, map->qualifiers, indent) + '}';
    }
    const auto &binary = std::get<ast::BinaryComprehension>(value);
    return "<< " + printer.expression(binary.expression, {.indent = indent}) + " || " +
           qualifiers_text(printer, binary.qualifiers, indent) + " >>";
}
} // namespace clause::printing
