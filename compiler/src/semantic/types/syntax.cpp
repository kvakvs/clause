#include "syntax.hpp"

namespace clause::semantic::types {
namespace {
// Every AST category has an explicit description; adding syntax requires updating this visitor.
struct Describe {
    Description operator()(const ast::Atom &v) const { return {{Kind::atom, utf8(v.name)}}; }

    Description operator()(const ast::Variable &v) const { return {{Kind::variable, utf8(v.name)}}; }

    Description operator()(const ast::IntegerLiteral &v) const { return {{Kind::integer, v.value.decimal}}; }

    Description operator()(const ast::CharacterLiteral &v) const {
        return {{Kind::integer, std::to_string(static_cast<std::uint32_t>(v.value))}};
    }

    Description operator()(const ast::TypeGroup &v) const { return {{Kind::annotation, ""}, {v.type}}; }

    Description operator()(const ast::AnnotatedType &v) const {
        return {{Kind::annotation, utf8(v.variable.name)}, {v.type}};
    }

    Description operator()(const ast::UnionType &v) const { return {{Kind::union_type}, {v.left, v.right}}; }

    Description operator()(const ast::RangeType &v) const { return {{Kind::range}, {v.first, v.last}}; }

    Description operator()(const ast::UnaryType &v) const {
        return {{Kind::unary, std::to_string(static_cast<unsigned>(v.operation))}, {v.operand}};
    }

    Description operator()(const ast::BinaryTypeOperator &v) const {
        return {{Kind::binary, std::to_string(static_cast<unsigned>(v.operation))}, {v.left, v.right}};
    }

    Description operator()(const ast::TypeApplication &v) const {
        return {{Kind::application, utf8(v.name.name), v.module ? utf8(v.module->name) : ""}, v.arguments};
    }

    Description operator()(const ast::TupleType &v) const {
        return {{Kind::tuple, v.any ? "any" : "exact"}, v.elements};
    }

    Description operator()(const ast::ListType &v) const {
        Description d{{Kind::list, v.nonempty ? "nonempty" : "possibly_empty"}};
        if (v.element) {
            d.children.push_back(*v.element);
        }
        return d;
    }

    Description operator()(const ast::MapType &v) const {
        Description d{{Kind::map, v.any ? "any" : "exact"}};
        d.children.reserve(2 * v.fields.size());
        d.node.labels.reserve(v.fields.size());
        for (const auto &field : v.fields) {
            d.children.push_back(field.key);
            d.children.push_back(field.value);
            d.node.labels.push_back(std::to_string(static_cast<unsigned>(field.kind)));
        }
        return d;
    }

    Description operator()(const ast::RecordType &v) const {
        Description d{{Kind::record, utf8(v.name.name), v.module ? utf8(v.module->name) : ""}};
        d.children.reserve(v.fields.size());
        d.node.labels.reserve(v.fields.size());
        for (const auto &field : v.fields) {
            d.children.push_back(field.type);
            d.node.labels.push_back(utf8(field.name.name));
        }
        return d;
    }

    Description operator()(const ast::BitstringType &v) const {
        Description d{{Kind::bitstring}};
        if (v.base) {
            d.children.push_back(*v.base);
            d.node.labels.emplace_back("base");
        }
        if (v.unit) {
            d.children.push_back(*v.unit);
            d.node.labels.emplace_back("unit");
        }
        return d;
    }

    Description operator()(const ast::FunType &v) const {
        Description d{{Kind::function, v.arguments ? "product" : "any_arguments"}};
        if (v.arguments) {
            d.children = *v.arguments;
        }
        if (v.result) {
            d.children.push_back(*v.result);
            d.node.labels.emplace_back("result");
        }
        return d;
    }
};
} // namespace

Description describe(const ast::TypeValue &value) {
    auto description = std::visit(Describe{}, value);
    description.node.children.reserve(description.children.size());
    return description;
}

Id translate(Graph &graph, const ast::Module &syntax, const ast::TypeId &root) {
    struct Frame {
        // Retain unfinished children and the next source-order edge on an explicit stack.
        Description description;
        std::size_t next = 0;
    };

    std::vector<Frame> pending{{describe(syntax.type(root).value)}};
    std::size_t work = 0;
    while (!pending.empty()) {
        if (++work > graph.limits().type_work) {
            return graph.exhausted();
        }
        auto &frame = pending.back();
        if (frame.next < frame.description.children.size()) {
            const auto child = frame.description.children[frame.next++];
            pending.push_back({describe(syntax.type(child).value)});
            continue;
        }
        auto node = std::move(frame.description.node);
        pending.pop_back();
        const auto id = node.kind == Kind::union_type ? graph.join(node.children) : graph.intern(std::move(node));
        if (pending.empty()) {
            return id;
        }
        pending.back().description.node.children.push_back(id);
    }
    return graph.bottom();
}
} // namespace clause::semantic::types
