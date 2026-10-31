#include <array>
#include <clause/runtime/modules.hpp>
#include <cstdio>
#include <stdexcept>

using namespace clause;
using namespace clause::runtime;

// Keep lifetime/transaction checks active in release consumers.
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Confirm the ABI bridge forwards exactly the runtime-owned process context.
abi::v1::TermWord context_entry(ProcessContext *context, const abi::v1::TermWord *args) {
    require(context->code_server().find_module("test").has_value(), "wrong context forwarded");
    return args[0];
}

// Reject incompatible and malformed drafts without publishing partial modules.
void rejection(Runtime &runtime, abi::v1::ModuleDescriptor descriptor) {
    for (const auto version : {abi::v1::version - 1, abi::v1::version + 1, abi::v1::version + 2}) {
        descriptor.abi_version = version;
        require(register_module(runtime, descriptor).error() == CodeError::abi_mismatch, "other version accepted");
    }
    descriptor.abi_version = abi::v1::version;
    descriptor.term_bits = sizeof(Word) == 8 ? 32 : 64;
    require(register_module(runtime, descriptor).error() == CodeError::abi_mismatch, "width accepted");
    descriptor.term_bits = sizeof(Word) * 8;
    const std::array duplicates{descriptor.exports[0], descriptor.exports[0]};
    descriptor.exports = duplicates.data();
    descriptor.export_count = duplicates.size();
    require(register_module(runtime, descriptor).error() == CodeError::invalid_export, "duplicate accepted");
    require(!runtime.code_server()->find_module("test"), "failed draft was published");
}

// Exercise descriptor copying, immutable publication and code-image ownership through resolved handles.
void publication() {
    auto runtime = Runtime::start().value();
    const abi::v1::ExportDescriptor item{"id", 2, 1, context_entry};
    abi::v1::ModuleDescriptor descriptor{abi::v1::version, sizeof(Word) * 8, "test", 4, &item, 1};
    rejection(*runtime, descriptor);
    auto image = CodeImage::linked();
    std::weak_ptr<const CodeImage> lifetime = image;
    auto loaded = register_module(*runtime, descriptor, std::move(image));
    require(loaded.has_value() && (*loaded)->functions().frozen(), "module not frozen");
    require(register_module(*runtime, descriptor).error() == CodeError::duplicate_module, "module replaced");
    auto function = runtime->code_server()->resolve({"test", "id", 1});
    auto *context = runtime->create_context().value();
    const std::array arguments{Term::from_word(*encode_integer(-42)).value()};
    require(function->call(*context, arguments)->word() == arguments.front().word(), "ABI value changed");
    loaded = std::unexpected(CodeError::module_not_found);
    runtime.reset();
    require(!lifetime.expired(), "handle lost code image");
    function = std::unexpected(CodeError::module_not_found);
    require(lifetime.expired(), "code image leaked");
}

// Validate the service boundary and real module publication without exposing registry implementation details.
int main() {
    try {
        require(CLAUSE_register_module_v4(nullptr, nullptr) ==
                    static_cast<std::uint8_t>(abi::v1::Status::invalid_argument),
                "null service input accepted");
        publication();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
