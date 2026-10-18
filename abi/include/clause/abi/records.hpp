#pragma once
#include "v1.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace clause::abi::v1 {
struct ModuleDescriptor;

// One native record definition (docs/native-records.md), immutable beside its module descriptor. Names are atom
// slots of the defining module; the runtime binds them when the module registers.
struct RecordDescriptor {
    // The defining module; its atom slots spell every name below.
    const ModuleDescriptor *module;
    std::size_t module_atom;
    std::size_t name_atom;
    // Nonzero when the defining module lists the record in -export_record.
    std::size_t exported;
    // Field-name atom slots in definition order.
    const std::size_t *fields;
    std::size_t field_count;
    // records:get_definition/2's field list as External Term Format bytes: {Field, Default} for a field with a
    // default, else the bare Field, in definition order (plan 21 step LIB8B).
    const char *definition;
    std::size_t definition_size;
};

// make: values are every field in definition order. get: record, module, name, field. update: record, module, name,
// then field/value pairs. match: record, module, name, then fields, extracted in that order. test: record, module,
// name. The module and name words are ignored when the check does not use them.
enum class RecordOperation : std::uint8_t { make, get, update, match, test };

// Which captured identity an operation accepts (docs/native-records.md#operations).
enum class RecordCheck : std::uint8_t {
    // Any native record.
    any,
    // A native record with this name, from any module.
    name,
    // A native record with this module and name.
    module_name,
    // An exported native record with this module and name.
    exported_module_name,
    // A native record that is exported or defined in this module (the module word).
    exported_or_module,
};

// Semantic failures publish their payload: the offending value for bad_record, {{Module, Name}, Field} for
// bad_field. no_match is a failed pattern or test; infrastructure failures use the checked channel.
enum class RecordOutcome : std::uint8_t { success, bad_record, bad_field, no_match, failure };

static_assert(std::is_standard_layout_v<RecordDescriptor>);
static_assert(sizeof(RecordDescriptor) == 8 * sizeof(TermWord));
} // namespace clause::abi::v1

// Borrow rooted words; make reads descriptor (a registered RecordDescriptor) and ignores check. Results go to
// output: one word, or one per matched field.
std::uint8_t CLAUSE_record_v1(void *context, std::uint8_t operation, std::uint8_t check, const void *descriptor,
                              const clause::abi::v1::TermWord *values, std::size_t count,
                              clause::abi::v1::TermWord *output) noexcept;
