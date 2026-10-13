// Added for the inference cache (plan step 58V).
#include "inference_store.hpp"
#include "dependent.hpp"
#include <algorithm>
#include <stdexcept>

namespace clause::semantic::types {
namespace {
// The layout of the bytes: a decoder of another layout finds them damaged.
constexpr std::uint64_t LAYOUT = 1;
// Seven value bits per byte; the high bit says another byte follows.
constexpr unsigned VALUE_BITS = 7;
constexpr std::uint64_t MORE = 0x80;

// Bytes that cannot be decoded, or name what the module lacks: decoding stops and installs nothing.
class Damaged final : public std::runtime_error {
  public:
    Damaged() : std::runtime_error("damaged inference results") {}
};

// Appends unsigned numbers (LEB128) and length-prefixed texts.
class Writer {
  public:
    void number(std::uint64_t value) {
        while (value >= MORE) {
            bytes_.push_back(static_cast<char>((value & (MORE - 1)) | MORE));
            value >>= VALUE_BITS;
        }
        bytes_.push_back(static_cast<char>(value));
    }

    void text(const std::string_view text) {
        number(text.size());
        bytes_.append(text);
    }

    std::string finish() && { return std::move(bytes_); }

  private:
    std::string bytes_;
};

// Reads what Writer wrote; running out of bytes or an oversized number is damage.
class Reader {
  public:
    explicit Reader(const std::string_view bytes) : rest_(bytes) {}

    std::uint64_t number() {
        std::uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += VALUE_BITS) {
            const auto byte = static_cast<std::uint64_t>(static_cast<unsigned char>(next()));
            result |= (byte & (MORE - 1)) << shift;
            if ((byte & MORE) == 0) {
                return result;
            }
        }
        throw Damaged();
    }

    // A number below `bound`: a count or an index into what was read before.
    std::size_t below(const std::size_t bound) {
        const auto value = number();
        if (value >= bound) {
            throw Damaged();
        }
        return static_cast<std::size_t>(value);
    }

    std::string text() {
        const auto size = below(rest_.size() + 1);
        std::string result(rest_.substr(0, size));
        rest_.remove_prefix(size);
        return result;
    }

    bool done() const { return rest_.empty(); }

    // The bytes left, which bound any count still to be read.
    std::size_t left() const { return rest_.size(); }

  private:
    char next() {
        if (rest_.empty()) {
            throw Damaged();
        }
        const auto byte = rest_.front();
        rest_.remove_prefix(1);
        return byte;
    }

    std::string_view rest_;
};

// Gathers what a module's results reach, numbers graph nodes in graph order (children were interned before their
// parents, so they come first) and dependent facts in index order, then writes everything.
class Encoder {
  public:
    explicit Encoder(const Inference &inference) : inference_(inference) {}

    std::string encode(const Module &module) {
        collect(module);
        number();
        out_.number(LAYOUT);
        write_nodes();
        write_dependents();
        write_functions();
        write_expressions();
        return std::move(out_).finish();
    }

  private:
    // Note a node and every node below it.
    void reach(const Id id) {
        std::vector<Id> pending{id};
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            if (nodes_.insert(next.index).second) {
                const auto &children = inference_.graph.get(next).children;
                pending.insert(pending.end(), children.begin(), children.end());
            }
        }
    }

    void reach(const std::vector<Id> &ids) {
        for (const auto id : ids) {
            reach(id);
        }
    }

    void reach(const Fact &fact) {
        reach(fact.type);
        if (fact.dependent && dependents_.insert(*fact.dependent).second) {
            reach(inference_.dependents.at(*fact.dependent).types);
        }
    }

    void reach(const std::vector<FunctionType> &types) {
        for (const auto &type : types) {
            reach(type.inputs);
            reach(type.result);
        }
    }

    // The module's functions with a summary, and its evaluated expressions.
    void collect(const Module &module) {
        for (const auto &function : module.functions) {
            const auto found = inference_.functions.find(&function);
            if (found == inference_.functions.end()) {
                continue;
            }
            functions_.push_back(&function);
            reach(found->second.inputs);
            reach(found->second.result);
            reach(found->second.entry);
            reach(found->second.types);
            if (const auto inputs = inference_.inputs.find(&function); inputs != inference_.inputs.end()) {
                reach(inputs->second);
            }
        }
        const auto &syntax = *module.syntax;
        for (std::size_t index = 0; index < syntax.expression_count(); ++index) {
            const auto found = inference_.expressions.find(&syntax.expression_at(index));
            if (found != inference_.expressions.end()) {
                expressions_.emplace_back(index, found->second);
                reach(found->second);
            }
        }
    }

    void number() {
        for (const auto index : nodes_) {
            node_locals_.emplace(index, node_locals_.size());
        }
        for (const auto index : dependents_) {
            dependent_locals_.emplace(index, dependent_locals_.size());
        }
    }

    void write_nodes() {
        out_.number(nodes_.size());
        const auto owner = inference_.graph.top().owner;
        for (const auto index : nodes_) {
            const auto &node = inference_.graph.get({owner, index});
            out_.number(static_cast<std::uint64_t>(node.kind));
            out_.text(node.name);
            out_.text(node.module);
            write(node.children);
            out_.number(node.labels.size());
            for (const auto &label : node.labels) {
                out_.text(label);
            }
        }
    }

    void write_dependents() {
        out_.number(dependents_.size());
        for (const auto index : dependents_) {
            const auto &dependent = inference_.dependents.at(index);
            out_.number(static_cast<std::uint64_t>(dependent.construct));
            out_.number(dependent.parameters.size());
            for (const auto &parameter : dependent.parameters) {
                out_.number(parameter.clause);
                out_.number(parameter.local);
            }
            out_.number(dependent.names.size());
            for (const auto &name : dependent.names) {
                out_.text(name);
            }
            write(dependent.types);
        }
    }

    void write_functions() {
        out_.number(functions_.size());
        for (const auto *function : functions_) {
            out_.number(function->key.name.size());
            for (const auto character : function->key.name) {
                out_.number(character);
            }
            out_.number(function->key.arity);
            const auto &summary = inference_.functions.at(function);
            write(summary.inputs);
            write(summary.result);
            write(summary.entry);
            write(summary.types);
            const auto inputs = inference_.inputs.find(function);
            out_.number(inputs == inference_.inputs.end() ? 0 : 1);
            if (inputs != inference_.inputs.end()) {
                write(inputs->second);
            }
        }
    }

    void write_expressions() {
        out_.number(expressions_.size());
        for (const auto &[index, fact] : expressions_) {
            out_.number(index);
            write(fact);
        }
    }

    void write(const std::vector<Id> &ids) {
        out_.number(ids.size());
        for (const auto id : ids) {
            out_.number(node_locals_.at(id.index));
        }
    }

    // A fact's type, then its argument and dependent fact, each as one more than its index (0 for none).
    void write(const Fact &fact) {
        out_.number(node_locals_.at(fact.type.index));
        out_.number(fact.argument ? *fact.argument + 1 : 0);
        out_.number(fact.dependent ? dependent_locals_.at(*fact.dependent) + 1 : 0);
    }

    void write(const std::vector<FunctionType> &types) {
        out_.number(types.size());
        for (const auto &type : types) {
            write(type.inputs);
            write(type.result);
            out_.number(type.exact ? 1 : 0);
        }
    }

    const Inference &inference_;
    Writer out_;
    // Graph indices and dependent fact indices the results reach, with their numbers in the bytes.
    std::set<std::size_t> nodes_;
    std::map<std::size_t, std::size_t> node_locals_;
    std::set<std::size_t> dependents_;
    std::map<std::size_t, std::size_t> dependent_locals_;
    // The functions with a summary in source order, and the expression facts by arena position.
    std::vector<const Function *> functions_;
    std::vector<std::pair<std::size_t, Fact>> expressions_;
};

// Reads encoded results into fresh graph nodes and dependent facts of the inference, keeping summaries, inputs and
// expression facts aside until everything decoded and every live function of the module has its summary.
class Decoder {
  public:
    Decoder(Inference &inference, const Module &module, const std::string_view bytes)
        : inference_(inference), module_(module), in_(bytes) {}

    void decode() {
        if (in_.number() != LAYOUT) {
            throw Damaged();
        }
        read_nodes();
        read_dependents();
        read_functions();
        read_expressions();
        if (!in_.done() || !covers_live()) {
            throw Damaged();
        }
    }

    void install() {
        for (auto &[function, summary] : summaries_) {
            inference_.functions.insert_or_assign(function, std::move(summary));
            inference_.reused.insert(function);
        }
        for (auto &[function, inputs] : inputs_) {
            inference_.inputs.insert_or_assign(function, std::move(inputs));
        }
        inference_.reused_expressions.merge(expressions_);
    }

  private:
    // A count of items still to read, each at least one byte long.
    std::size_t count() { return in_.below(in_.left() + 1); }

    void read_nodes() {
        const auto total = count();
        nodes_.reserve(total);
        for (std::size_t index = 0; index < total; ++index) {
            Node node{static_cast<Kind>(in_.below(static_cast<std::size_t>(Kind::positional) + 1))};
            node.name = in_.text();
            node.module = in_.text();
            node.children = ids();
            const auto labels = count();
            for (std::size_t label = 0; label < labels; ++label) {
                node.labels.push_back(in_.text());
            }
            nodes_.push_back(inference_.graph.intern(std::move(node)));
        }
    }

    void read_dependents() {
        const auto total = count();
        for (std::size_t index = 0; index < total; ++index) {
            const auto construct = in_.below(static_cast<std::size_t>(Operator::try_of_operator) + 1);
            Dependent dependent{static_cast<Operator>(construct), {}, {}, {}};
            const auto parameters = count();
            for (std::size_t parameter = 0; parameter < parameters; ++parameter) {
                const auto clause = static_cast<std::size_t>(in_.number());
                dependent.parameters.push_back({clause, static_cast<std::size_t>(in_.number())});
            }
            const auto names = count();
            for (std::size_t name = 0; name < names; ++name) {
                dependent.names.push_back(in_.text());
            }
            dependent.types = types();
            dependents_.push_back(intern(inference_, std::move(dependent)));
        }
    }

    void read_functions() {
        const auto total = count();
        for (std::size_t index = 0; index < total; ++index) {
            const auto *function = lookup();
            Summary summary{ids(), fact()};
            summary.entry = ids();
            summary.types = types();
            if (in_.below(2) == 1) {
                inputs_.emplace(function, ids());
            }
            summaries_.emplace(function, std::move(summary));
        }
    }

    void read_expressions() {
        const auto &syntax = *module_.syntax;
        const auto total = count();
        for (std::size_t index = 0; index < total; ++index) {
            const auto &expression = syntax.expression_at(in_.below(syntax.expression_count()));
            expressions_.insert_or_assign(&expression, fact());
        }
    }

    // The module's function a key names; a function the module lacks is damage.
    const Function *lookup() {
        FunctionKey key;
        const auto size = count();
        for (std::size_t index = 0; index < size; ++index) {
            key.name.push_back(static_cast<char32_t>(in_.below(0x110000)));
        }
        key.arity = static_cast<std::size_t>(in_.number());
        const auto found = module_.lookup.find(key);
        if (found == module_.lookup.end()) {
            throw Damaged();
        }
        return &module_.functions.at(found->second);
    }

    // Whether every live function of the module has a summary.
    bool covers_live() const {
        return std::ranges::all_of(module_.functions, [&](const Function &function) {
            return !function.live_ || summaries_.contains(&function);
        });
    }

    std::vector<Id> ids() {
        std::vector<Id> result(count());
        for (auto &id : result) {
            id = nodes_.at(in_.below(nodes_.size()));
        }
        return result;
    }

    Fact fact() {
        Fact result{nodes_.at(in_.below(nodes_.size()))};
        if (const auto argument = in_.number(); argument != 0) {
            result.argument = static_cast<std::size_t>(argument - 1);
        }
        if (const auto dependent = in_.below(dependents_.size() + 1); dependent != 0) {
            result.dependent = dependents_.at(dependent - 1);
        }
        return result;
    }

    std::vector<FunctionType> types() {
        std::vector<FunctionType> result(count());
        for (auto &type : result) {
            type.inputs = ids();
            type.result = fact();
            type.exact = in_.below(2) == 1;
        }
        return result;
    }

    Inference &inference_;
    const Module &module_;
    Reader in_;
    // The interned node and the dependent fact index of each number in the bytes.
    std::vector<Id> nodes_;
    std::vector<std::size_t> dependents_;
    // What install puts into the inference.
    std::map<const Function *, Summary> summaries_;
    std::map<const Function *, std::vector<Id>> inputs_;
    std::map<const ast::Expression *, Fact> expressions_;
};
} // namespace

std::string encode_module(const Inference &inference, const Module &module) {
    return Encoder(inference).encode(module);
}

bool decode_module(Inference &inference, const Module &module, const std::string_view bytes) {
    Decoder decoder(inference, module, bytes);
    try {
        decoder.decode();
    } catch (const Damaged &) {
        return false;
    }
    decoder.install();
    return true;
}
} // namespace clause::semantic::types
