#include "startup.hpp"
#include "../semantic/symbols.hpp"
#include "llvm_state.hpp"
#include "runtime_symbols.hpp"
#include <clause/abi/startup.hpp>
#include <clause/compiler/source.hpp>
#include <llvm/IR/IRBuilder.h>
#include <span>
#include <string_view>
#include <vector>

namespace clause::codegen {
namespace {
// Private constant with exact UTF-8 bytes; the descriptor stores its size separately.
llvm::Constant *spelling(llvm::Module &output, const std::string_view text, const char *name) {
    auto *bytes = llvm::ConstantDataArray::getString(output.getContext(), text, false);
    return new llvm::GlobalVariable(output, bytes->getType(), true, llvm::GlobalValue::PrivateLinkage, bytes, name);
}

// Array of the batch's external module descriptors, in source (registration) order.
llvm::Constant *descriptors(llvm::Module &output, const std::span<const std::unique_ptr<semantic::Module>> modules) {
    auto &context = output.getContext();
    std::vector<llvm::Constant *> entries;
    entries.reserve(modules.size());
    for (const auto &module : modules) {
        const auto symbol = semantic::encode_symbol({utf8(module->name), "", 0}) + ".descriptor";
        entries.push_back(output.getOrInsertGlobal(symbol, llvm::Type::getInt8Ty(context)));
    }
    auto *array =
        llvm::ConstantArray::get(llvm::ArrayType::get(llvm::PointerType::get(context, 0), entries.size()), entries);
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage, array,
                                    "startup.modules");
}

// The symbols of the extension entries the program links: the crypto archive's, then the ssl archive's (added for
// the ssl runtime archive, plan 21 step LIB81B), then the epmd archive's (plan 30 step DIST3d).
std::vector<std::string_view> extension_symbols(const llvm::Triple &triple, const StartupRequest &request) {
    std::vector<std::string_view> symbols;
    if (request.crypto) {
        symbols.push_back(request.crypto_fips ? services::symbol<services::CryptoFipsExtension>(triple)
                                              : services::symbol<services::CryptoExtension>(triple));
    }
    if (request.ssl) {
        symbols.push_back(services::symbol<services::SslExtension>(triple));
    }
    if (request.bundled_epmd) {
        symbols.push_back(services::symbol<services::EpmdExtension>(triple));
    }
    return symbols;
}

// Array of the extension entries the program links, or null without any. Added for the crypto runtime archive.
llvm::Constant *extensions(llvm::Module &output, const std::span<const std::string_view> symbols) {
    auto *ptr = llvm::PointerType::get(output.getContext(), 0);
    if (symbols.empty()) {
        return llvm::ConstantPointerNull::get(ptr);
    }
    std::vector<llvm::Constant *> entries;
    for (const auto &symbol : symbols) {
        auto entry = output.getOrInsertFunction(symbol, llvm::FunctionType::get(ptr, false));
        entries.push_back(llvm::cast<llvm::Constant>(entry.getCallee()));
    }
    auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(ptr, entries.size()), entries);
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage, array,
                                    "startup.extensions");
}

// Array of the built-in runtime option words, NUL-terminated, or null without any. Added for manifest runtime options
// (plan 30 step DIST4a3b).
llvm::Constant *runtime_options(llvm::Module &output, const std::span<const std::string> words) {
    auto *ptr = llvm::PointerType::get(output.getContext(), 0);
    if (words.empty()) {
        return llvm::ConstantPointerNull::get(ptr);
    }
    std::vector<llvm::Constant *> entries;
    for (const auto &word : words) {
        auto *bytes = llvm::ConstantDataArray::getString(output.getContext(), word, true);
        entries.push_back(new llvm::GlobalVariable(output, bytes->getType(), true, llvm::GlobalValue::PrivateLinkage,
                                                   bytes, "startup.option"));
    }
    auto *array = llvm::ConstantArray::get(llvm::ArrayType::get(ptr, entries.size()), entries);
    return new llvm::GlobalVariable(output, array->getType(), true, llvm::GlobalValue::PrivateLinkage, array,
                                    "startup.options");
}

// Constant abi::v1::StartupDescriptor naming the modules, the entry (arity 1, or 0 without the arguments), the
// linked runtime extensions and the built-in runtime options.
llvm::GlobalVariable *descriptor(llvm::Module &output, const std::span<const std::unique_ptr<semantic::Module>> modules,
                                 const StartupRequest &request, llvm::IntegerType *word) {
    llvm::IRBuilder<> builder(output.getContext());
    auto *ptr = builder.getPtrTy();
    auto *i32 = builder.getInt32Ty();
    auto *type = llvm::StructType::get(i32, i32, ptr, word, ptr, word, ptr, word, i32, ptr, word, ptr, word);
    const auto module = utf8(modules[request.module]->name);
    const auto symbols = extension_symbols(output.getTargetTriple(), request);
    const auto size = [word](const std::size_t value) { return llvm::ConstantInt::get(word, value); };
    auto *data = llvm::ConstantStruct::get(
        type, {builder.getInt32(abi::v1::version), builder.getInt32(word->getBitWidth()), descriptors(output, modules),
               size(modules.size()), spelling(output, module, "startup.module"), size(module.size()),
               spelling(output, request.function, "startup.function"), size(request.function.size()),
               builder.getInt32((request.escript ? abi::v1::startup_escript : 0U) |
                                (request.arity == 0 ? abi::v1::startup_no_arguments : 0U)),
               extensions(output, symbols), size(symbols.size()), runtime_options(output, request.runtime_options),
               size(request.runtime_options.size())});
    return new llvm::GlobalVariable(output, type, true, llvm::GlobalValue::PrivateLinkage, data, "startup.descriptor");
}

// Define `int main(int, char **)` forwarding argc/argv and the descriptor to the runtime's program service.
void define_main(llvm::Module &output, llvm::GlobalVariable *startup) {
    llvm::IRBuilder<> builder(output.getContext());
    auto *ptr = builder.getPtrTy();
    auto *i32 = builder.getInt32Ty();
    auto *main = llvm::Function::Create(llvm::FunctionType::get(i32, {i32, ptr}, false),
                                        llvm::GlobalValue::ExternalLinkage, "main", output);
    builder.SetInsertPoint(llvm::BasicBlock::Create(output.getContext(), "entry", main));
    auto service = output.getOrInsertFunction(services::symbol<services::Main>(output.getTargetTriple()),
                                              llvm::FunctionType::get(i32, {i32, ptr, ptr}, false));
    builder.CreateRet(builder.CreateCall(service, {main->getArg(0), main->getArg(1), startup}));
}
} // namespace

void emit_startup(Compilation &compilation, const std::span<const std::unique_ptr<semantic::Module>> modules,
                  llvm::IntegerType *word) {
    auto &state = detail::state(compilation);
    if (!state.request.startup || state.request.startup->module >= modules.size() || state.modules.empty()) {
        throw std::invalid_argument("startup: entry module outside the batch");
    }
    const auto &request = *state.request.startup;
    auto output = std::make_unique<llvm::Module>("startup", *state.context);
    output->setTargetTriple(state.modules.front()->getTargetTriple());
    output->setDataLayout(state.modules.front()->getDataLayout());
    define_main(*output, descriptor(*output, modules, request, word));
    state.modules.push_back(std::move(output));
}
} // namespace clause::codegen
