#include "byte_scans.hpp"
#include "../semantic/capabilities.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Intrinsics.h>
#include <variant>

namespace clause::codegen {
namespace {
using Bytes = std::bitset<256>;
using Range = std::pair<unsigned, unsigned>;
// Bytes one vector test reads, the most byte ranges it tests, and the largest counter step, which keeps a counter
// advanced by 16 steps inside the word.
constexpr unsigned WIDTH = 16;
constexpr std::size_t MAX_RANGES = 4;
constexpr std::int64_t MAX_STEP = std::int64_t{1} << 20;

const ast::Expression &node(const ast::Module &syntax, const ast::ExprId &id) {
    return syntax.expression(semantic::ungroup(syntax, id));
}

const ast::Expression &head(const ast::Module &syntax, const ast::FunctionClause &clause, const std::size_t parameter) {
    return node(syntax, semantic::pattern_root(syntax, clause.arguments.at(parameter)));
}

const std::u32string *variable(const ast::Expression &expression) {
    const auto *found = std::get_if<ast::Variable>(&expression.value);
    return found ? &found->name : nullptr;
}

// The value of an integer literal that fits 64 bits.
std::optional<std::int64_t> integer(const ast::IntegerLiteral &literal) {
    const auto &digits = literal.value.decimal;
    std::int64_t value = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() ? std::optional{value}
                                                                                   : std::nullopt;
}

// The value of an integer or character literal that fits 64 bits.
std::optional<std::int64_t> literal(const ast::Expression &expression) {
    if (const auto *character = std::get_if<ast::CharacterLiteral>(&expression.value)) {
        return static_cast<std::int64_t>(character->value);
    }
    const auto *number = std::get_if<ast::IntegerLiteral>(&expression.value);
    return number ? integer(*number) : std::nullopt;
}

bool named(const ast::BinaryModifier &modifier, const std::initializer_list<std::u32string_view> names) {
    return !modifier.parameter && std::ranges::find(names, std::u32string_view{modifier.name.name}) != names.end();
}

// Whether a segment is one unsigned byte: no size or size 8, and only integer, unsigned and endianness types.
bool byte_segment(const ast::Module &syntax, const ast::BinarySegment &segment) {
    if (segment.size && literal(node(syntax, *segment.size)) != 8) {
        return false;
    }
    const auto byte_type = [](const auto &modifier) {
        return named(modifier, {U"integer", U"unsigned", U"big", U"little", U"native"});
    };
    return !segment.modifiers || std::ranges::all_of(*segment.modifiers, byte_type);
}

// Whether a segment is a binary or bitstring rest without a size.
bool rest_segment(const ast::BinarySegment &segment) {
    return !segment.size && segment.modifiers && segment.modifiers->size() == 1 &&
           named(segment.modifiers->front(), {U"binary", U"bytes", U"bits", U"bitstring"});
}

// The first bytes for which an earlier clause's head may match the parameter while bytes remain: a literal first
// byte claims itself (truncated to a byte, which only claims more), an empty binary claims none; none when the head
// may match any byte.
std::optional<Bytes> claimed(const ast::Module &syntax, const ast::Expression &pattern) {
    const auto *bits = std::get_if<ast::Bitstring>(&pattern.value);
    if (!bits) {
        return std::nullopt;
    }
    Bytes bytes;
    if (bits->segments.empty()) {
        return bytes;
    }
    const auto &first = bits->segments.front();
    const auto &value = node(syntax, first.value);
    const auto *text = std::get_if<ast::StringLiteral>(&value.value);
    const auto byte = text && !text->value.empty() && !first.size && !first.modifiers
                          ? std::optional{static_cast<std::int64_t>(text->value.front())}
                      : byte_segment(syntax, first) ? literal(value)
                                                    : std::nullopt;
    if (!byte) {
        return std::nullopt;
    }
    bytes.set(static_cast<std::size_t>(static_cast<std::uint64_t>(*byte) & 0xFFU));
    return bytes;
}

// --- Guards evaluated for each byte value: integers and booleans with Erlang's guard semantics ---

struct Unsupported {};

struct Raised {};

using Term = std::variant<Unsupported, Raised, std::int64_t, bool>;

bool comparison(const ast::BinaryOperator operation) {
    using enum ast::BinaryOperator;
    return std::ranges::contains(
        std::initializer_list{equal, not_equal, less_equal, less, greater_equal, greater, exact_equal, exact_not_equal},
        operation);
}

// The two evaluated operands of a binary operator.
using Operands = std::array<Term, 2>;

// Whether evaluation stops at a term: it is not modeled, or it raised.
bool settled(const Term &term) {
    return std::holds_alternative<Unsupported>(term) || std::holds_alternative<Raised>(term);
}

// Term order of two integers or booleans: numbers sort before atoms, and false before true.
int order(const Operands &operands) {
    const auto rank = [](const Term &term) {
        return std::holds_alternative<std::int64_t>(term) ? std::pair{0, std::get<std::int64_t>(term)}
                                                          : std::pair{1, std::int64_t{std::get<bool>(term)}};
    };
    const auto [a, b] = std::pair{rank(operands[0]), rank(operands[1])};
    return a < b ? -1 : (a == b ? 0 : 1);
}

Term compared(const ast::BinaryOperator operation, const Operands &operands) {
    using enum ast::BinaryOperator;
    const auto sign = order(operands);
    switch (operation) {
    case less:
        return sign < 0;
    case less_equal:
        return sign <= 0;
    case greater:
        return sign > 0;
    case greater_equal:
        return sign >= 0;
    case equal:
    case exact_equal:
        return sign == 0;
    default:
        return sign != 0;
    }
}

// and, or and xor of two booleans; any other operand raises.
Term logical(const ast::BinaryOperator operation, const Operands &operands) {
    using enum ast::BinaryOperator;
    const auto *x = std::get_if<bool>(&operands[0]);
    const auto *y = std::get_if<bool>(&operands[1]);
    if (!x || !y) {
        return Raised{};
    }
    switch (operation) {
    case logical_and:
        return *x && *y;
    case logical_or:
        return *x || *y;
    default:
        return *x != *y;
    }
}

// +, -, band and bor of two integers; any other operand raises.
Term integral(const ast::BinaryOperator operation, const Operands &operands) {
    using enum ast::BinaryOperator;
    const auto *a = std::get_if<std::int64_t>(&operands[0]);
    const auto *b = std::get_if<std::int64_t>(&operands[1]);
    if (!a || !b) {
        return Raised{};
    }
    switch (operation) {
    case add:
        return *a + *b;
    case subtract:
        return *a - *b;
    case bit_and:
        return *a & *b;
    default:
        return *a | *b;
    }
}

// A strict binary operator applied to its evaluated operands; unsupported for operators not modeled.
Term operate(const ast::BinaryOperator operation, const Operands &operands) {
    using enum ast::BinaryOperator;
    if (comparison(operation)) {
        return compared(operation, operands);
    }
    if (std::ranges::contains(std::initializer_list{logical_and, logical_or, logical_xor}, operation)) {
        return logical(operation, operands);
    }
    if (std::ranges::contains(std::initializer_list{add, subtract, bit_and, bit_or}, operation)) {
        return integral(operation, operands);
    }
    return Unsupported{};
}

struct Evaluation {
    // The module's syntax, the byte variable's name and the byte value it is bound to.
    const ast::Module &syntax;
    const std::u32string &name;
    std::int64_t byte;

    Term operator()(const ast::ExprId &id) const {
        return std::visit([this](const auto &value) { return term(value); }, syntax.expression(id).value);
    }

    Term term(const ast::IntegerLiteral &value) const {
        const auto number = integer(value);
        return number ? Term{*number} : Term{Unsupported{}};
    }

    Term term(const ast::CharacterLiteral &value) const { return static_cast<std::int64_t>(value.value); }

    Term term(const ast::Atom &value) const {
        return value.name == U"true" ? Term{true} : (value.name == U"false" ? Term{false} : Term{Unsupported{}});
    }

    Term term(const ast::Variable &value) const { return value.name == name ? Term{byte} : Term{Unsupported{}}; }

    Term term(const ast::Group &value) const { return (*this)(value.expression); }

    Term term(const ast::UnaryExpression &value) const;
    Term term(const ast::BinaryExpression &value) const;
    Term term(const ast::CallExpression &value) const;

    template <class Other> Term term(const Other &) const { return Unsupported{}; }

    // andalso and orelse: the right operand runs only when the left is the boolean that does not decide the result.
    Term lazy(const ast::BinaryExpression &value, const Term &left) const {
        const auto *decided = std::get_if<bool>(&left);
        if (!decided) {
            return std::holds_alternative<Unsupported>(left) ? left : Term{Raised{}};
        }
        return *decided == (value.operation == ast::BinaryOperator::or_else) ? Term{*decided} : (*this)(value.right);
    }
};

Term Evaluation::term(const ast::UnaryExpression &value) const {
    const auto operand = (*this)(value.operand);
    if (settled(operand)) {
        return operand;
    }
    if (value.operation == ast::UnaryOperator::logical_not) {
        return std::holds_alternative<bool>(operand) ? Term{!std::get<bool>(operand)} : Term{Raised{}};
    }
    const auto *number = std::get_if<std::int64_t>(&operand);
    if (!number || value.operation == ast::UnaryOperator::bit_not) {
        return number ? Term{Unsupported{}} : Term{Raised{}};
    }
    return value.operation == ast::UnaryOperator::negative ? -*number : *number;
}

Term Evaluation::term(const ast::BinaryExpression &value) const {
    const auto left = (*this)(value.left);
    if (value.operation == ast::BinaryOperator::and_also || value.operation == ast::BinaryOperator::or_else) {
        return lazy(value, left);
    }
    const Operands operands{left, (*this)(value.right)};
    const auto stop = std::ranges::find_if(operands, settled);
    return stop != operands.end() ? *stop : operate(value.operation, operands);
}

// is_integer/1 of the byte; any other call is not evaluated.
Term Evaluation::term(const ast::CallExpression &value) const {
    const auto *target = std::get_if<ast::Atom>(&node(syntax, value.target).value);
    if (!target || target->name != U"is_integer" || value.arguments.size() != 1) {
        return Unsupported{};
    }
    const auto argument = (*this)(value.arguments.front());
    return std::holds_alternative<Unsupported>(argument) ? argument
                                                         : Term{std::holds_alternative<std::int64_t>(argument)};
}

// Whether a guard accepts one byte value; none when it tests anything but the byte.
std::optional<bool> accepts(const Evaluation &evaluation, const std::optional<ast::GuardSyntax> &guard) {
    if (!guard) {
        return true;
    }
    bool passed = false;
    for (const auto &alternative : guard->alternatives) {
        bool all = true;
        for (const auto &test : alternative.tests) {
            const auto result = evaluation(test);
            if (std::holds_alternative<Unsupported>(result)) {
                return std::nullopt;
            }
            const auto *truth = std::get_if<bool>(&result);
            all = all && truth && *truth;
        }
        passed = passed || all;
    }
    return passed;
}

// The bytes a clause's guard accepts with `name` bound to each.
std::optional<Bytes> accepted(const ast::Module &syntax, const std::optional<ast::GuardSyntax> &guard,
                              const std::u32string &name) {
    Bytes bytes;
    for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
        const auto passes = accepts({syntax, name, static_cast<std::int64_t>(byte)}, guard);
        if (!passes) {
            return std::nullopt;
        }
        bytes.set(byte, *passes);
    }
    return bytes;
}

// --- The scan clause ---

struct ScanNames {
    // The scanned parameter, its byte and rest variables, and the variable of every other parameter (empty at the
    // scanned one).
    std::size_t parameter;
    std::u32string byte;
    std::u32string rest;
    std::vector<std::u32string> parameters;
};

// The byte and rest variables of a `<<Byte, Rest/binary>>` pattern (an empty byte name for `_`); none for any
// other pattern.
std::optional<std::pair<std::u32string, std::u32string>> scan_pattern(const ast::Module &syntax,
                                                                      const ast::Expression &pattern) {
    const auto *bits = std::get_if<ast::Bitstring>(&pattern.value);
    if (!bits || bits->segments.size() != 2 || !byte_segment(syntax, bits->segments[0]) ||
        !rest_segment(bits->segments[1])) {
        return std::nullopt;
    }
    const auto *byte = variable(node(syntax, bits->segments[0].value));
    const auto *rest = variable(node(syntax, bits->segments[1].value));
    if (!byte || !rest) {
        return std::nullopt;
    }
    return std::pair{*byte == U"_" ? std::u32string{} : *byte, *rest};
}

// The variables of a clause whose head is `<<Byte, Rest/binary>>` at the parameter and a distinct variable at every
// other one; none otherwise.
std::optional<ScanNames> scan_names(const ast::Module &syntax, const ast::FunctionClause &clause,
                                    const std::size_t parameter) {
    const auto pattern = scan_pattern(syntax, head(syntax, clause, parameter));
    if (!pattern) {
        return std::nullopt;
    }
    ScanNames names{parameter, pattern->first, pattern->second, {}};
    std::set<std::u32string> seen{names.rest};
    for (std::size_t index = 0; index < clause.arguments.size(); ++index) {
        const auto *name = index == parameter ? &names.byte : variable(head(syntax, clause, index));
        const bool distinct = name && *name != U"_" && (name->empty() || seen.insert(*name).second);
        if (!distinct) {
            return std::nullopt;
        }
        names.parameters.push_back(index == parameter ? std::u32string{} : *name);
    }
    return names;
}

// The constant `name + K` or `K + name` adds to `name`, and -K for `name - K`; none for any other expression.
std::optional<std::int64_t> offset(const ast::Module &syntax, const ast::BinaryExpression &sum,
                                   const std::u32string &name) {
    const auto is_name = [&](const ast::ExprId &id) {
        const auto *found = variable(node(syntax, id));
        return found && *found == name;
    };
    if (sum.operation == ast::BinaryOperator::subtract) {
        const auto by = is_name(sum.left) ? literal(node(syntax, sum.right)) : std::nullopt;
        return by ? std::optional{-*by} : std::nullopt;
    }
    if (sum.operation != ast::BinaryOperator::add) {
        return std::nullopt;
    }
    return is_name(sum.left) ? literal(node(syntax, sum.right))
                             : (is_name(sum.right) ? literal(node(syntax, sum.left)) : std::nullopt);
}

// What one argument of the scan clause's self call adds to its parameter's variable `name` each byte: 0 for the
// variable itself, else a bounded constant offset; none for anything else.
std::optional<std::int64_t> step(const ast::Module &syntax, const ast::Expression &argument,
                                 const std::u32string &name) {
    if (const auto *same = variable(argument)) {
        return *same == name ? std::optional<std::int64_t>{0} : std::nullopt;
    }
    const auto *sum = std::get_if<ast::BinaryExpression>(&argument.value);
    const auto by = sum ? offset(syntax, *sum, name) : std::nullopt;
    return by && *by <= MAX_STEP && *by >= -MAX_STEP ? by : std::nullopt;
}

// The clause's body when it is only a local call of the function itself passing the rest at the parameter.
const ast::CallExpression *self_call(const ExpressionLowering &state, const ast::FunctionClause &clause,
                                     const ScanNames &names) {
    const auto &syntax = *state.module.syntax;
    const auto *call =
        clause.body.size() == 1 ? std::get_if<ast::CallExpression>(&node(syntax, clause.body.front()).value) : nullptr;
    const auto *target = call ? std::get_if<ast::Atom>(&node(syntax, call->target).value) : nullptr;
    if (!target || target->name != state.function.key.name || call->arguments.size() != names.parameters.size()) {
        return nullptr;
    }
    const auto *rest = variable(node(syntax, call->arguments.at(names.parameter)));
    return rest && *rest == names.rest ? call : nullptr;
}

// The (parameter, step) counters of a scan clause whose self call passes every other parameter on unchanged.
std::optional<std::vector<std::pair<std::size_t, std::int64_t>>>
counters(const ExpressionLowering &state, const ast::FunctionClause &clause, const ScanNames &names) {
    const auto *call = self_call(state, clause, names);
    if (!call) {
        return std::nullopt;
    }
    const auto &syntax = *state.module.syntax;
    std::vector<std::pair<std::size_t, std::int64_t>> found;
    for (std::size_t index = 0; index < names.parameters.size(); ++index) {
        const auto by = index == names.parameter
                            ? std::optional<std::int64_t>{0}
                            : step(syntax, node(syntax, call->arguments[index]), names.parameters[index]);
        if (!by) {
            return std::nullopt;
        }
        if (*by != 0) {
            found.emplace_back(index, *by);
        }
    }
    return found;
}

// The maximal runs of consecutive bytes in a set.
std::vector<Range> ranges(const Bytes &bytes) {
    std::vector<Range> runs;
    for (unsigned byte = 0; byte < bytes.size(); ++byte) {
        if (!bytes.test(byte)) {
            continue;
        }
        if (!runs.empty() && runs.back().second + 1 == byte) {
            runs.back().second = byte;
        } else {
            runs.emplace_back(byte, byte);
        }
    }
    return runs;
}

// The scan of clause `index`, accepting the bytes its guard accepts that no earlier clause claims.
std::optional<ByteScan> scan_clause(const ExpressionLowering &state, const ast::FunctionClause &clause,
                                    const std::size_t index, const ScanNames &names, const Bytes &taken) {
    const auto bytes = accepted(*state.module.syntax, clause.guard, names.byte);
    auto steps = counters(state, clause, names);
    if (!bytes || !steps) {
        return std::nullopt;
    }
    const auto free = *bytes & ~taken;
    if (free.none() || std::min(ranges(free).size(), ranges(~free).size()) > MAX_RANGES) {
        return std::nullopt;
    }
    return ByteScan{index, free, std::move(*steps)};
}

// --- The vector test ---

llvm::Value *compare(ExpressionLowering &state, const llvm::CmpInst::Predicate predicate, llvm::Value *left,
                     llvm::Value *right) {
    return state.builder.Insert(llvm::CmpInst::Create(llvm::Instruction::ICmp, predicate, left, right));
}

// Each byte of `chunk` is in one of `runs`: one subtract and one unsigned compare per run.
llvm::Value *in_ranges(ExpressionLowering &state, llvm::Value *chunk, const std::vector<Range> &runs) {
    auto &builder = state.builder;
    auto *type = chunk->getType();
    llvm::Value *inside = nullptr;
    for (const auto &[low, high] : runs) {
        auto *distance = builder.CreateSub(chunk, llvm::ConstantInt::get(type, low));
        auto *in = compare(state, llvm::CmpInst::ICMP_ULE, distance, llvm::ConstantInt::get(type, high - low));
        inside = inside ? builder.CreateOr(inside, in) : in;
    }
    return inside;
}

// Whether each byte of an i8 or a vector of i8 is in the set: the set's runs or its complement's, whichever are
// fewer; true for the full set.
llvm::Value *accepts_byte(ExpressionLowering &state, const Bytes &bytes, llvm::Value *byte) {
    const auto inside = ranges(bytes);
    const auto outside = ranges(~bytes);
    if (outside.empty()) {
        return llvm::ConstantInt::getTrue(llvm::CmpInst::makeCmpResultType(byte->getType()));
    }
    return inside.size() <= outside.size() ? in_ranges(state, byte, inside)
                                           : state.builder.CreateNot(in_ranges(state, byte, outside));
}

// How many of the 16 bytes at `address` lead in the set before the first byte outside it (0 to 16), tested on all
// 16 bytes at once.
llvm::Value *run_length(ExpressionLowering &state, const Bytes &bytes, llvm::Value *address) {
    auto &builder = state.builder;
    if (bytes.all()) {
        return builder.getInt32(WIDTH);
    }
    auto *chunk = builder.CreateAlignedLoad(llvm::FixedVectorType::get(builder.getInt8Ty(), WIDTH), address,
                                            llvm::Align(1), "scan.chunk");
    auto *in = accepts_byte(state, bytes, chunk);
    auto *mask = builder.CreateZExt(builder.CreateBitCast(in, builder.getIntNTy(WIDTH)), builder.getInt32Ty());
    auto *stops = builder.CreateOr(builder.CreateNot(mask), builder.getInt32(1U << WIDTH));
    return builder.CreateBinaryIntrinsic(llvm::Intrinsic::cttz, stops, builder.getTrue(), nullptr, "scan.run");
}

// Whether the rest starts on a byte boundary with 16 bytes left, so the vector test reads only its bytes.
llvm::Value *positioned(ExpressionLowering &state, const LoopRest &rest, llvm::Value *absolute) {
    auto &builder = state.builder;
    const auto constant = [&](std::uint64_t value) { return llvm::ConstantInt::get(state.word, value); };
    return builder.CreateAnd(
        compare(state, llvm::CmpInst::ICMP_EQ, builder.CreateAnd(absolute, constant(7)), constant(0)),
        compare(state, llvm::CmpInst::ICMP_UGE, rest.bits, constant(std::uint64_t{WIDTH} * 8)));
}

// Whether every counter is a small integer that 16 of its steps keep inside the word.
llvm::Value *counters_fit(ExpressionLowering &state, const ByteScan &scan, const std::vector<llvm::Value *> &values) {
    auto &builder = state.builder;
    const auto constant = [&](std::uint64_t value) { return llvm::ConstantInt::get(state.word, value); };
    const auto width = state.word->getBitWidth();
    const auto minimum = llvm::APInt::getSignedMinValue(width).getZExtValue();
    llvm::Value *fit = builder.getTrue();
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto reach = static_cast<std::uint64_t>(std::abs(scan.counters[index].second)) * WIDTH
                           << abi::v1::small_integer_bits;
        auto *value = values[index];
        auto *tag = builder.CreateAnd(value, constant(abi::v1::small_integer_tag));
        auto *distance = builder.CreateSub(value, constant(minimum + reach));
        const auto span = llvm::APInt::getMaxValue(width).getZExtValue() - 2 * reach;
        fit = builder.CreateAnd(
            fit, builder.CreateAnd(compare(state, llvm::CmpInst::ICMP_EQ, tag, constant(abi::v1::small_integer_tag)),
                                   compare(state, llvm::CmpInst::ICMP_ULE, distance, constant(span))));
    }
    return fit;
}

} // namespace

std::optional<ByteScan> byte_scan(const ExpressionLowering &state, const std::vector<ast::FunctionClause> &clauses,
                                  const std::size_t parameter) {
    const auto &syntax = *state.module.syntax;
    Bytes taken;
    for (std::size_t index = 0; index < clauses.size(); ++index) {
        if (const auto names = scan_names(syntax, clauses[index], parameter)) {
            return scan_clause(state, clauses[index], index, *names, taken);
        }
        const auto claims = claimed(syntax, head(syntax, clauses[index], parameter));
        if (!claims) {
            return std::nullopt;
        }
        taken |= *claims;
    }
    return std::nullopt;
}

void scan_tail(ExpressionLowering &state, const ByteScan &scan, const LoopRest &rest, const ast::CallExpression &call,
               const std::function<void(const LoopRest &)> &enter) {
    auto &builder = state.builder;
    const auto &syntax = *state.module.syntax;
    std::vector<llvm::Value *> values;
    for (const auto &[parameter, by] : scan.counters) {
        values.push_back(state.values.at(&syntax.expression(call.arguments.at(parameter))));
        if (state.raw_floats.contains(values.back())) {
            enter(rest);
            return;
        }
    }
    const auto &view = *state.binary_loop->view;
    auto *absolute = builder.CreateAdd(view.base, rest.offset);
    auto &context = state.entry.getContext();
    auto *probe = llvm::BasicBlock::Create(context, "scan.probe", &state.entry);
    auto *run = llvm::BasicBlock::Create(context, "scan.run", &state.entry);
    auto *plain = llvm::BasicBlock::Create(context, "scan.none", &state.entry);
    auto *fits = llvm::BasicBlock::Create(context, "scan.fits", &state.entry);
    builder.CreateCondBr(positioned(state, rest, absolute), probe, plain);
    // The first byte is tested and branched on alone, so a run that stops at once leaves the rest untouched.
    builder.SetInsertPoint(probe);
    auto *address = builder.CreateIntToPtr(builder.CreateAdd(view.data, builder.CreateLShr(absolute, 3)),
                                           builder.getPtrTy(), "scan.bytes");
    auto *first = builder.CreateAlignedLoad(builder.getInt8Ty(), address, llvm::Align(1), "scan.first");
    builder.CreateCondBr(accepts_byte(state, scan.bytes, first), fits, plain);
    builder.SetInsertPoint(fits);
    builder.CreateCondBr(counters_fit(state, scan, values), run, plain);
    builder.SetInsertPoint(plain);
    enter(rest);
    builder.SetInsertPoint(run);
    auto *skipped = builder.CreateZExt(run_length(state, scan.bytes, address), state.word, "scan.skipped");
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto [parameter, by] = scan.counters[index];
        auto *delta = builder.CreateMul(
            skipped, llvm::ConstantInt::getSigned(state.word, by * (std::int64_t{1} << abi::v1::small_integer_bits)));
        state.values.at(&syntax.expression(call.arguments.at(parameter))) =
            builder.CreateAdd(values[index], delta, "scan.counter");
    }
    auto *bits = builder.CreateShl(skipped, llvm::ConstantInt::get(state.word, 3));
    auto moved = rest;
    moved.offset = builder.CreateAdd(rest.offset, bits, "scan.offset");
    moved.bits = builder.CreateSub(rest.bits, bits, "scan.bits");
    enter(moved);
}
} // namespace clause::codegen
