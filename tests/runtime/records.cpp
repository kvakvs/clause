#include "terms.hpp"
#include "terms/structural_order.hpp"
#include <array>
#include <clause/abi/modules.hpp>
#include <clause/abi/records.hpp>
#include <clause/runtime/modules.hpp>
#include <clause/runtime/output.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Native record cells and CLAUSE_record_v1 over hand-written descriptors (docs/native-records.md).
namespace {
using namespace clause;
using namespace clause::runtime;
using abi::v1::RecordCheck;
using abi::v1::RecordDescriptor;
using abi::v1::RecordOperation;
using abi::v1::RecordOutcome;

// Keep assertions active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Atom slots shared by both test modules: 0 module, 1 pt, 2 x, 3 y, 4 hidden, 5 z, 6 empty.
constexpr std::array<abi::v1::AtomDescriptor, 7> GEO_ATOMS{
    {{"geo", 3}, {"pt", 2}, {"x", 1}, {"y", 1}, {"hidden", 6}, {"z", 1}, {"empty", 5}}};
constexpr std::array<abi::v1::AtomDescriptor, 4> OTHER_ATOMS{{{"other", 5}, {"pt", 2}, {"x", 1}, {"y", 1}}};
constexpr std::array<std::size_t, 2> XY{2, 3};
constexpr std::array<std::size_t, 1> Z{5};
constexpr std::array<std::size_t, 2> OTHER_XY{2, 3};

extern const abi::v1::ModuleDescriptor geo;
extern const abi::v1::ModuleDescriptor other;
// geo exports pt and keeps hidden private; other defines its own private pt.
const std::array<RecordDescriptor, 3> GEO_RECORDS{{{&geo, 0, 1, 1, XY.data(), XY.size(), nullptr, 0},
                                                   {&geo, 0, 4, 0, Z.data(), Z.size(), nullptr, 0},
                                                   {&geo, 0, 6, 1, nullptr, 0, nullptr, 0}}};
const std::array<RecordDescriptor, 1> OTHER_RECORDS{{{&other, 0, 1, 0, OTHER_XY.data(), OTHER_XY.size(), nullptr, 0}}};
const abi::v1::ModuleDescriptor geo{
    abi::v1::version,   sizeof(Word) * 8,  "geo", 3, nullptr, 0, GEO_ATOMS.data(), GEO_ATOMS.size(),
    GEO_RECORDS.data(), GEO_RECORDS.size()};
const abi::v1::ModuleDescriptor other{
    abi::v1::version,     sizeof(Word) * 8,    "other", 5, nullptr, 0, OTHER_ATOMS.data(), OTHER_ATOMS.size(),
    OTHER_RECORDS.data(), OTHER_RECORDS.size()};

// One service call: its outcome, its output words and the infrastructure status it left, if any.
struct Call {
    RecordOutcome outcome;
    std::vector<Term> output;
    std::optional<abi::v1::Status> status;
};

// Invoke the service inside a generated invocation with the given operands.
Call call(ProcessContext &context, RecordOperation operation, RecordCheck check, const RecordDescriptor *descriptor,
          std::span<const Term> operands, std::size_t outputs = 1) {
    std::vector<Word> words;
    for (const auto &value : operands) {
        words.push_back(value.word());
    }
    std::vector<Word> output(std::max<std::size_t>(outputs, 1), 0);
    GeneratedInvocation scope(context.generated_calls());
    const auto outcome = static_cast<RecordOutcome>(CLAUSE_record_v1(&context, static_cast<std::uint8_t>(operation),
                                                                     static_cast<std::uint8_t>(check), descriptor,
                                                                     words.data(), words.size(), output.data()));
    const auto &failure = context.generated_calls().failure();
    Call result{outcome, {}, failure ? failure->status : std::nullopt};
    if (outcome == RecordOutcome::success || outcome == RecordOutcome::bad_record ||
        outcome == RecordOutcome::bad_field) {
        for (std::size_t i = 0; i < outputs; ++i) {
            result.output.push_back(Term::from_word(output[i], context).value());
        }
    }
    return result;
}

// Render a value as io_lib ~w or erlang:display text.
std::string text(const Term &value, TermStyle style = TermStyle::display) { return format_term(value, style).value(); }

// Fixed operands for the checks: module and name atoms.
struct Names {
    Term geo, other, pt, hidden, x, y, z, zz;
};

Names names(ProcessContext &context) {
    TermFactory factory(context);
    const auto atom = [&](const char *spelling) { return factory.atom(spelling).value(); };
    return {atom("geo"), atom("other"), atom("pt"), atom("hidden"), atom("x"), atom("y"), atom("z"), atom("zz")};
}

// Build a record of a descriptor through the make operation.
Term make(ProcessContext &context, const RecordDescriptor &descriptor, std::span<const Term> fields) {
    const auto made = call(context, RecordOperation::make, RecordCheck::any, &descriptor, fields);
    require(made.outcome == RecordOutcome::success, "construction failed");
    return made.output[0];
}

// Construction captures the definition; values are not tuple elements and print in definition order.
void construction(ProcessContext &context, const Names &n) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto two = factory.integer(2).value();
    const auto point = make(context, GEO_RECORDS[0], std::array{one, two});
    require(point.is_native_record() && !point.is_tuple() && point.tuple_size().error() == TermError::wrong_type,
            "record is not a distinct kind");
    require(point.record_field(n.y)->integer_value() == 2 &&
                point.record_field(n.z).error() == TermError::unknown_field,
            "field lookup by name failed");
    require(text(point) == "#geo:pt{x=1,y=2}" && text(point, TermStyle::write) == "#geo:pt{x = 1,y = 2}",
            "record text differs");
    const auto empty = make(context, GEO_RECORDS[2], {});
    require(text(empty) == "#geo:empty{}" && empty.record_fields()->empty(), "empty record differs");
    const auto nested = make(context, GEO_RECORDS[0], std::array{point, factory.tuple(std::array{empty}).value()});
    require(text(nested) == "#geo:pt{x=#geo:pt{x=1,y=2},y={#geo:empty{}}}", "nested record text differs");
    const auto wrong = call(context, RecordOperation::make, RecordCheck::any, &GEO_RECORDS[0], std::array{one});
    require(wrong.outcome == RecordOutcome::failure && wrong.status == abi::v1::Status::invalid_argument,
            "field count mismatch accepted");
    const RecordDescriptor forged = GEO_RECORDS[0];
    const auto unknown = call(context, RecordOperation::make, RecordCheck::any, &forged, std::array{one, two});
    require(unknown.outcome == RecordOutcome::failure && unknown.status == abi::v1::Status::wrong_owner,
            "unregistered descriptor accepted");
}

// Access checks the captured identity the operation names and reports missing fields with the record's identity.
void access(ProcessContext &context, const Names &n) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto mine = make(context, GEO_RECORDS[0], std::array{one, factory.integer(2).value()});
    const auto theirs = make(context, OTHER_RECORDS[0], std::array{one, one});
    const auto secret = make(context, GEO_RECORDS[1], std::array{one});
    const auto get = [&](const Term &record, RecordCheck check, const Term &module, const Term &name,
                         const Term &field) {
        return call(context, RecordOperation::get, check, nullptr, std::array{record, module, name, field});
    };
    require(get(theirs, RecordCheck::name, n.geo, n.pt, n.x).outcome == RecordOutcome::success,
            "local access checked the module");
    const auto foreign = get(theirs, RecordCheck::module_name, n.geo, n.pt, n.x);
    require(foreign.outcome == RecordOutcome::bad_record && foreign.output[0].word() == theirs.word(),
            "foreign record accepted");
    require(get(secret, RecordCheck::exported_module_name, n.geo, n.hidden, n.z).outcome == RecordOutcome::bad_record,
            "unexported record accepted externally");
    require(get(secret, RecordCheck::any, n.geo, n.hidden, n.z).outcome == RecordOutcome::success,
            "anonymous access checked export");
    const auto tuple = factory.tuple(std::array{n.pt, one, one}).value();
    const auto not_record = get(tuple, RecordCheck::any, n.geo, n.pt, n.x);
    require(not_record.outcome == RecordOutcome::bad_record && not_record.output[0].word() == tuple.word(),
            "tuple record accepted");
    require(get(n.x, RecordCheck::any, n.geo, n.pt, n.x).outcome == RecordOutcome::bad_record, "atom accepted");
    const auto missing = get(mine, RecordCheck::name, n.geo, n.pt, n.zz);
    require(missing.outcome == RecordOutcome::bad_field && text(missing.output[0]) == "{{geo,pt},zz}",
            "missing field payload differs");
}

// Update copies with replaced fields under the update checks; matching extracts fields or does not match.
void update_and_match(ProcessContext &context, const Names &n) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto nine = factory.integer(9).value();
    const auto point = make(context, GEO_RECORDS[0], std::array{one, factory.integer(2).value()});
    const auto secret = make(context, GEO_RECORDS[1], std::array{one});
    const auto updated = call(context, RecordOperation::update, RecordCheck::module_name, nullptr,
                              std::array{point, n.geo, n.pt, n.y, nine, n.x, nine});
    require(updated.outcome == RecordOutcome::success && text(updated.output[0]) == "#geo:pt{x=9,y=9}" &&
                text(point) == "#geo:pt{x=1,y=2}",
            "update result differs or changed the original");
    const auto bad =
        call(context, RecordOperation::update, RecordCheck::any, nullptr, std::array{point, n.geo, n.pt, n.zz, nine});
    require(bad.outcome == RecordOutcome::bad_field && text(bad.output[0]) == "{{geo,pt},zz}", "bad update field");
    const auto foreign = call(context, RecordOperation::update, RecordCheck::exported_or_module, nullptr,
                              std::array{secret, n.other, n.pt});
    require(foreign.outcome == RecordOutcome::bad_record, "private record updated from another module");
    const auto own = call(context, RecordOperation::update, RecordCheck::exported_or_module, nullptr,
                          std::array{secret, n.geo, n.pt});
    require(own.outcome == RecordOutcome::success && own.output[0].exactly_equal(secret).value(),
            "empty update differs");
    const auto matched = call(context, RecordOperation::match, RecordCheck::module_name, nullptr,
                              std::array{point, n.geo, n.pt, n.y, n.x}, 2);
    require(matched.outcome == RecordOutcome::success && matched.output[0].integer_value() == 2 &&
                matched.output[1].integer_value() == 1,
            "matched fields differ");
    const auto absent =
        call(context, RecordOperation::match, RecordCheck::any, nullptr, std::array{point, n.geo, n.pt, n.zz}, 1);
    const auto other =
        call(context, RecordOperation::match, RecordCheck::module_name, nullptr, std::array{point, n.other, n.pt}, 0);
    require(absent.outcome == RecordOutcome::no_match && other.outcome == RecordOutcome::no_match,
            "failed match did not report no_match");
    const auto test = [&](const Term &value, RecordCheck check) {
        return call(context, RecordOperation::test, check, nullptr, std::array{value, n.geo, n.pt}, 0).outcome;
    };
    require(test(point, RecordCheck::module_name) == RecordOutcome::success &&
                test(secret, RecordCheck::module_name) == RecordOutcome::no_match &&
                test(one, RecordCheck::any) == RecordOutcome::no_match,
            "record tests differ");
}

// Order: tuple < record < map; definitions by module then name; values in definition order; == is numeric.
void order(ProcessContext &context) {
    TermFactory factory(context);
    const auto one = factory.integer(1).value();
    const auto real = factory.floating(1.0).value();
    const auto two = factory.integer(2).value();
    const auto compare = [](const Term &left, const Term &right, bool exact) {
        return detail::structural_order(left, right, exact).value();
    };
    const auto a = make(context, GEO_RECORDS[0], std::array{one, two});
    const auto b = make(context, GEO_RECORDS[0], std::array{one, two});
    const auto c = make(context, GEO_RECORDS[0], std::array{two, one});
    const auto d = make(context, GEO_RECORDS[0], std::array{real, two});
    const auto foreign = make(context, OTHER_RECORDS[0], std::array{one, one});
    const auto tuple = factory.tuple(std::array{one}).value();
    const auto map = factory.map({}).value();
    require(compare(a, b, true) == 0 && compare(a, c, true) < 0 && compare(a, d, true) != 0 &&
                compare(a, d, false) == 0,
            "record value order differs");
    require(compare(a, foreign, true) < 0 && compare(tuple, a, true) < 0 && compare(a, map, true) < 0,
            "record category order differs");
    const auto empty = make(context, GEO_RECORDS[2], {});
    const auto secret = make(context, GEO_RECORDS[1], std::array{one});
    require(compare(empty, a, true) < 0 && compare(secret, a, true) < 0, "definition order differs");
}

// Records copy between heaps, survive collections and keep their heap verifiable.
void memory(Runtime &runtime) {
    auto &source = *runtime.create_context().value();
    auto &destination = *runtime.create_context().value();
    TermFactory factory(source);
    const auto inner = make(source, GEO_RECORDS[2], {});
    const auto list = factory.list(std::array{inner, inner}).value();
    const auto point = make(source, GEO_RECORDS[0], std::array{list, inner});
    const auto expected = text(point);
    require(source.heap().verify().has_value(), "record heap does not verify");
    const auto copy = point.copy_to(destination.heap()).value();
    require(text(copy) == expected && copy.exactly_equal(point).value() && destination.heap().verify().has_value(),
            "copied record differs");
    std::array roots{point.word()};
    require(source.heap().collect(roots).has_value() && source.heap().verify().has_value(), "collection failed");
    require(text(Term::from_word(roots[0], source).value()) == expected, "record lost by collection");
    require(runtime.destroy_context(&source) == abi::v1::Status::ok && text(copy) == expected,
            "copy depended on its source");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        require(register_module(*runtime, geo).has_value() && register_module(*runtime, other).has_value(),
                "record modules rejected");
        auto bad = geo;
        const RecordDescriptor stray{&other, 0, 1, 0, nullptr, 0, nullptr, 0};
        bad.name = "bad";
        bad.name_size = 3;
        bad.records = &stray;
        bad.record_count = 1;
        require(register_module(*runtime, bad).error() == CodeError::invalid_module, "foreign descriptor accepted");
        auto &context = *runtime->create_context().value();
        const auto n = names(context);
        construction(context, n);
        access(context, n);
        update_and_match(context, n);
        order(context);
        memory(*runtime);
        std::cout << "native records passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
