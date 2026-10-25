#include "../../terms/identities.hpp"
#include "../erlang/ports.hpp"
#include "../typed.hpp"
#define ZSTD_STATIC_LINKING_ONLY
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <string>
#include <terms.hpp>
#include <type_traits>
#include <utility>
#include <vector>
#include <zstd.h>

// The zstd contexts and dictionaries of Clause's private clause_zstd module (plan 21 step LIB54E,
// docs/builtins.md#zstd-module): each is a handle reference whose buffer holds its state, answering as OTP's zstd NIF
// does (the same libzstd release, output chunk sizes and error terms); library/stdlib/zstd.erl answers OTP's zstd
// functions over these builtins.
namespace clause::runtime::builtins {
namespace {
// A dictionary shared by its handle and the contexts that reference it, freed with the last of them.
struct Shared {
    std::atomic<std::uint32_t> references;
    bool compress;

    union {
        ZSTD_CDict *c;
        ZSTD_DDict *d;
    };
};

// The state of one context, kept in its handle's buffer; trivially copyable, so it starts as the buffer's bytes.
struct Context {
    // Tells a context buffer apart from other handles.
    std::uint64_t magic;

    union {
        ZSTD_CCtx *c;
        ZSTD_DCtx *d;
    };

    // The dictionary the context references, kept alive with it.
    Shared *dictionary;
    // The pid word of the process that made it: only that process uses it.
    Word owner;
    // Set once decompression of a frame began, as the NIF's in_use.
    bool in_use;
};

// The state of one dictionary handle.
struct Dictionary {
    std::uint64_t magic;
    Shared *shared;
};

static_assert(std::is_trivially_copyable_v<Context>);
static_assert(std::is_trivially_copyable_v<Dictionary>);

// The roles of the builtins' term arguments, so adjacent ones cannot be swapped by mistake.
struct HandleRole {};

struct DictionaryRole {};

struct DataRole {};

struct KeyRole {};

struct ValueRole {};

struct SizeRole {};

struct FlushRole {};

using Handle = Named<HandleRole>;
using DictionaryHandle = Named<DictionaryRole>;
using Data = Named<DataRole>;
using Key = Named<KeyRole>;
using Value = Named<ValueRole>;
using Size = Named<SizeRole>;
using Flush = Named<FlushRole>;

constexpr std::uint64_t COMPRESS_MAGIC = 0x7A7374645F637478;   // "zstd_ctx"
constexpr std::uint64_t DECOMPRESS_MAGIC = 0x7A7374645F647478; // "zstd_dtx"
constexpr std::uint64_t CDICT_MAGIC = 0x7A7374645F636463;      // "zstd_cdc"
constexpr std::uint64_t DDICT_MAGIC = 0x7A7374645F646463;      // "zstd_ddc"

void release(Shared *shared) noexcept {
    if (shared == nullptr || shared->references.fetch_sub(1) != 1) {
        return;
    }
    if (shared->compress) {
        ZSTD_freeCDict(shared->c);
    } else {
        ZSTD_freeDDict(shared->d);
    }
    delete shared;
}

// Free the libzstd context and drop the dictionary: close/1, or the last reference dying.
void close_context(Context &context, bool compress) noexcept {
    if (context.c == nullptr) {
        return;
    }
    if (compress) {
        ZSTD_freeCCtx(context.c);
    } else {
        ZSTD_freeDCtx(context.d);
    }
    context.c = nullptr;
    release(std::exchange(context.dictionary, nullptr));
}

void finalize_compress(std::span<std::byte> bytes) noexcept {
    close_context(*reinterpret_cast<Context *>(bytes.data()), true);
}

void finalize_decompress(std::span<std::byte> bytes) noexcept {
    close_context(*reinterpret_cast<Context *>(bytes.data()), false);
}

void finalize_dictionary(std::span<std::byte> bytes) noexcept {
    release(std::exchange(reinterpret_cast<Dictionary *>(bytes.data())->shared, nullptr));
}

// Raise error:Reason with the atom `reason`.
[[noreturn]] void fail(ProcessContext &context, AtomName reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = need(TermFactory(context).atom(reason)).word()};
}

// Raise error:{badarg, {What, Term}}, as the NIF does for an unknown key or value.
[[noreturn]] void fail_invalid(ProcessContext &context, AtomName what, const Term &term) {
    const std::array detail{atom(context, what), term};
    const std::array reason{atom(context, "badarg"_atom), need(TermFactory(context).tuple(detail))};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = need(TermFactory(context).tuple(reason)).word()};
}

// Raise error:{zstd_error, Name} for a libzstd result that is an error; the result otherwise.
std::size_t checked(ProcessContext &context, std::size_t result) {
    if (!ZSTD_isError(result)) {
        return result;
    }
    const std::string_view name = ZSTD_getErrorName(result);
    const std::array reason{atom(context, "zstd_error"_atom),
                            need(TermFactory(context).binary(std::as_bytes(std::span(name.data(), name.size()))))};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = need(TermFactory(context).tuple(reason)).word()};
}

Word self_word(ProcessContext &context) { return need(TermFactory(context).pid(context.identity())).word(); }

// The open context of `magic` behind a handle, used by its owner; badarg for anything else (a closed context too),
// not_on_controlling_process for a context of another process.
Context &context_of(ProcessContext &context, const Term &handle, std::uint64_t magic) {
    const auto buffer = detail::HandleAccess::buffer(handle);
    if (!buffer || (*buffer)->size() != sizeof(Context)) {
        bad_argument();
    }
    // Only the owner changes the context, so no two calls touch it at once.
    auto *state = reinterpret_cast<Context *>(const_cast<std::byte *>((*buffer)->data()));
    if (state->magic != magic || state->c == nullptr) {
        bad_argument();
    }
    if (state->owner != self_word(context)) {
        fail(context, "not_on_controlling_process"_atom);
    }
    return *state;
}

Context &compressor(ProcessContext &context, const Handle &handle) {
    return context_of(context, handle.term, COMPRESS_MAGIC);
}

Context &decompressor(ProcessContext &context, const Handle &handle) {
    return context_of(context, handle.term, DECOMPRESS_MAGIC);
}

// The shared dictionary of a dictionary handle of `magic`; badarg for anything else.
Shared &dictionary_of(const DictionaryHandle &handle, std::uint64_t magic) {
    const auto buffer = detail::HandleAccess::buffer(handle.term);
    if (!buffer || (*buffer)->size() != sizeof(Dictionary)) {
        bad_argument();
    }
    const auto *state = reinterpret_cast<const Dictionary *>((*buffer)->data());
    if (state->magic != magic || state->shared == nullptr) {
        bad_argument();
    }
    return *state->shared;
}

// The bytes of iodata; badarg for anything else.
std::vector<std::byte> iodata(const Data &data) {
    auto bytes = iodata_bytes(data.term);
    if (!bytes) {
        bad_argument();
    }
    return std::move(*bytes);
}

template <typename State>
TermResult<Term> make_handle(ProcessContext &context, const State &state, detail::HandleAccess::Finalizer finalizer) {
    std::array<std::byte, sizeof(State)> bytes{};
    std::memcpy(bytes.data(), &state, sizeof(state));
    return detail::HandleAccess::make(context.heap(), bytes, finalizer);
}

TermResult<Term> init_compress(ProcessContext &context) {
    Context state{};
    state.magic = COMPRESS_MAGIC;
    state.c = ZSTD_createCCtx();
    state.owner = self_word(context);
    return make_handle(context, state, finalize_compress);
}

TermResult<Term> init_decompress(ProcessContext &context) {
    Context state{};
    state.magic = DECOMPRESS_MAGIC;
    state.d = ZSTD_createDCtx();
    state.owner = self_word(context);
    return make_handle(context, state, finalize_decompress);
}

Term load_compress_dictionary(ProcessContext &context, const Handle &handle, const Data &data) {
    auto &state = compressor(context, handle);
    const auto bytes = iodata(data);
    release(std::exchange(state.dictionary, nullptr));
    checked(context, ZSTD_CCtx_loadDictionary(state.c, bytes.data(), bytes.size()));
    return atom(context, "ok"_atom);
}

Term load_decompress_dictionary(ProcessContext &context, const Handle &handle, const Data &data) {
    auto &state = decompressor(context, handle);
    const auto bytes = iodata(data);
    release(std::exchange(state.dictionary, nullptr));
    checked(context, ZSTD_DCtx_loadDictionary(state.d, bytes.data(), bytes.size()));
    return atom(context, "ok"_atom);
}

// Keep `shared` alive with the context, dropping the dictionary it referenced before.
void reference(Context &state, Shared &shared) {
    shared.references.fetch_add(1);
    release(std::exchange(state.dictionary, &shared));
}

Term ref_compress_dictionary(ProcessContext &context, const Handle &handle, const DictionaryHandle &dictionary) {
    auto &state = compressor(context, handle);
    auto &shared = dictionary_of(dictionary, CDICT_MAGIC);
    checked(context, ZSTD_CCtx_refCDict(state.c, shared.c));
    reference(state, shared);
    return atom(context, "ok"_atom);
}

Term ref_decompress_dictionary(ProcessContext &context, const Handle &handle, const DictionaryHandle &dictionary) {
    auto &state = decompressor(context, handle);
    auto &shared = dictionary_of(dictionary, DDICT_MAGIC);
    checked(context, ZSTD_DCtx_refDDict(state.d, shared.d));
    reference(state, shared);
    return atom(context, "ok"_atom);
}

// How a compression parameter's value is written: an integer, a boolean or a strategy atom.
enum class Kind : std::uint8_t { integer, boolean, strategy };

struct Parameter {
    std::string_view name;
    ZSTD_cParameter parameter;
    Kind kind;
};

constexpr std::array COMPRESS_PARAMETERS{
    Parameter{"compressionLevel", ZSTD_c_compressionLevel, Kind::integer},
    Parameter{"windowLog", ZSTD_c_windowLog, Kind::integer},
    Parameter{"hashLog", ZSTD_c_hashLog, Kind::integer},
    Parameter{"chainLog", ZSTD_c_chainLog, Kind::integer},
    Parameter{"searchLog", ZSTD_c_searchLog, Kind::integer},
    Parameter{"minMatch", ZSTD_c_minMatch, Kind::integer},
    Parameter{"targetLength", ZSTD_c_targetLength, Kind::integer},
    Parameter{"strategy", ZSTD_c_strategy, Kind::strategy},
    Parameter{"targetCBlockSize", ZSTD_c_targetCBlockSize, Kind::integer},
    Parameter{"enableLongDistanceMatching", ZSTD_c_enableLongDistanceMatching, Kind::boolean},
    Parameter{"ldmHashLog", ZSTD_c_ldmHashLog, Kind::integer},
    Parameter{"ldmMinMatch", ZSTD_c_ldmMinMatch, Kind::integer},
    Parameter{"ldmBucketSizeLog", ZSTD_c_ldmBucketSizeLog, Kind::integer},
    Parameter{"ldmHashRateLog", ZSTD_c_ldmHashRateLog, Kind::integer},
    Parameter{"contentSizeFlag", ZSTD_c_contentSizeFlag, Kind::boolean},
    Parameter{"checksumFlag", ZSTD_c_checksumFlag, Kind::boolean},
    Parameter{"dictIDFlag", ZSTD_c_dictIDFlag, Kind::boolean},
};

// The strategy atoms; default is 0.
constexpr std::array<std::string_view, 10> STRATEGIES{"default", "fast",    "dfast", "greedy",  "lazy",
                                                      "lazy2",   "btlazy2", "btopt", "btultra", "btultra2"};

// The spelling of an atom term; empty for anything else.
std::string_view spelling(const Term &term) {
    const auto converted = need(Argument<AtomArgument>::convert(term));
    return converted ? converted->spelling : std::string_view{};
}

// The compression parameter a key names; {badarg, {invalid_key, Key}} for any other key.
const Parameter &compress_parameter(ProcessContext &context, const Key &key) {
    const auto name = spelling(key.term);
    for (const auto &parameter : COMPRESS_PARAMETERS) {
        if (!name.empty() && parameter.name == name) {
            return parameter;
        }
    }
    fail_invalid(context, "invalid_key"_atom, key.term);
}

// The value of an int argument (enif_get_int), if it is one.
std::optional<int> int_value(const Term &term) {
    const auto value = small(term.word());
    if (!value || *value < INT32_MIN || *value > INT32_MAX) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

// The value of a boolean atom, if it is one.
std::optional<int> boolean_value(std::string_view name) {
    if (name == "true" || name == "false") {
        return name == "true" ? 1 : 0;
    }
    return std::nullopt;
}

// The libzstd number of a strategy atom, if it is one.
std::optional<int> strategy_value(std::string_view name) {
    for (std::size_t index = 0; index < STRATEGIES.size() && !name.empty(); ++index) {
        if (STRATEGIES.at(index) == name) {
            return static_cast<int>(index);
        }
    }
    return std::nullopt;
}

// The libzstd value of a parameter's term; {badarg, {invalid_value, Value}} for one of the wrong kind.
int parameter_value(ProcessContext &context, Kind kind, const Value &value) {
    const auto number = kind == Kind::boolean    ? boolean_value(spelling(value.term))
                        : kind == Kind::strategy ? strategy_value(spelling(value.term))
                                                 : int_value(value.term);
    if (!number) {
        fail_invalid(context, "invalid_value"_atom, value.term);
    }
    return *number;
}

Term set_compress_parameter(ProcessContext &context, const Handle &handle, const Key &key, const Value &value) {
    auto &state = compressor(context, handle);
    const auto &parameter = compress_parameter(context, key);
    checked(context,
            ZSTD_CCtx_setParameter(state.c, parameter.parameter, parameter_value(context, parameter.kind, value)));
    return atom(context, "ok"_atom);
}

// The term of a parameter's value: booleans from the result as the NIF reads them, strategies by name.
// What ZSTD_CCtx_getParameter returned and the value it wrote.
struct Reading {
    std::size_t result;
    int value;
};

TermResult<Term> parameter_term(ProcessContext &context, Kind kind, const Reading &reading) {
    const auto value = reading.value;
    if (kind == Kind::boolean) {
        return atom(context, reading.result != 0 ? "true"_atom : "false"_atom);
    }
    if (kind == Kind::strategy) {
        if (value < 0 || static_cast<std::size_t>(value) >= STRATEGIES.size()) {
            bad_argument();
        }
        return TermFactory(context).atom(AtomName{STRATEGIES.at(static_cast<std::size_t>(value))});
    }
    return TermFactory(context).integer(value);
}

TermResult<Term> get_compress_parameter(ProcessContext &context, const Handle &handle, const Key &key) {
    auto &state = compressor(context, handle);
    const auto &parameter = compress_parameter(context, key);
    int value = 0;
    const auto result = checked(context, ZSTD_CCtx_getParameter(state.c, parameter.parameter, &value));
    return parameter_term(context, parameter.kind, Reading{.result = result, .value = value});
}

Term set_pledged_src_size(ProcessContext &context, const Handle &handle, const Size &size) {
    auto &state = compressor(context, handle);
    const auto value = need(Argument<detail::Integer>::convert(size.term));
    if (!value || *value < 0 || *value > std::numeric_limits<std::uint64_t>::max()) {
        bad_argument();
    }
    checked(context, ZSTD_CCtx_setPledgedSrcSize(state.c, value->convert_to<std::uint64_t>()));
    return atom(context, "ok"_atom);
}

// windowLogMax, the only decompression parameter; {badarg, {invalid_key, Key}} for any other key.
void check_decompress_key(ProcessContext &context, const Key &key) {
    if (spelling(key.term) != "windowLogMax") {
        fail_invalid(context, "invalid_key"_atom, key.term);
    }
}

Term set_decompress_parameter(ProcessContext &context, const Handle &handle, const Key &key, const Value &value) {
    auto &state = decompressor(context, handle);
    const auto number = int_value(value.term);
    if (!number) {
        bad_argument();
    }
    check_decompress_key(context, key);
    checked(context, ZSTD_DCtx_setParameter(state.d, ZSTD_d_windowLogMax, *number));
    return atom(context, "ok"_atom);
}

TermResult<Term> get_decompress_parameter(ProcessContext &context, const Handle &handle, const Key &key) {
    auto &state = decompressor(context, handle);
    check_decompress_key(context, key);
    int value = 0;
    checked(context, ZSTD_DCtx_getParameter(state.d, ZSTD_d_windowLogMax, &value));
    return TermFactory(context).integer(value);
}

// What one stream call did: the output, the input it consumed, and whether the output chunk filled while libzstd
// still had more to write.
struct Pass {
    std::vector<std::byte> output;
    std::size_t consumed;
    bool full;
};

// Run the codec into one output chunk until the input or the chunk runs out, as the NIF's codec loop does.
template <typename Codec>
Pass run(ProcessContext &context, std::span<const std::byte> input, std::size_t chunk, Codec codec) {
    Pass pass{.output = std::vector<std::byte>(chunk), .consumed = 0, .full = false};
    ZSTD_outBuffer out{pass.output.data(), pass.output.size(), 0};
    ZSTD_inBuffer in{input.data(), input.size(), 0};
    std::size_t remaining = 0;
    do {
        remaining = checked(context, codec(out, in));
    } while (in.pos < in.size && out.pos < out.size);
    pass.full = out.pos == out.size && remaining > 0;
    pass.output.resize(out.pos);
    pass.consumed = in.pos;
    return pass;
}

// {done | flush, Output} when flushing consumed all input, {continue, Rest, Output} with input left, else
// {continue, Output}.
TermResult<Term> stream_result(ProcessContext &context, const Pass &pass, std::span<const std::byte> input, int flush) {
    TermFactory factory(context);
    const auto output = need(factory.binary(pass.output));
    if (flush != 0 && pass.consumed == input.size()) {
        const std::array parts{atom(context, pass.full ? "flush"_atom : "done"_atom), output};
        return factory.tuple(parts);
    }
    if (pass.consumed < input.size()) {
        const std::array parts{atom(context, "continue"_atom), need(factory.binary(input.subspan(pass.consumed))),
                               output};
        return factory.tuple(parts);
    }
    const std::array parts{atom(context, "continue"_atom), output};
    return factory.tuple(parts);
}

int flush_argument(const Flush &flush) {
    const auto value = int_value(flush.term);
    if (!value) {
        bad_argument();
    }
    return *value;
}

TermResult<Term> compress_stream(ProcessContext &context, const Handle &handle, const BinaryArgument &input,
                                 const Flush &flush) {
    auto &state = compressor(context, handle);
    try {
        const auto directive = flush_argument(flush);
        const auto pass = run(context, input.bytes, ZSTD_CStreamOutSize(), [&](ZSTD_outBuffer &out, ZSTD_inBuffer &in) {
            return ZSTD_compressStream2(state.c, &out, &in, static_cast<ZSTD_EndDirective>(directive));
        });
        return stream_result(context, pass, input.bytes, directive);
    } catch (const BuiltinFailure &) {
        ZSTD_CCtx_reset(state.c, ZSTD_reset_session_only);
        throw;
    }
}

// The output chunk of a decompression: a frame's whole size when it starts one that says it and is smaller.
std::size_t decompress_chunk(Context &state, std::span<const std::byte> input) {
    auto chunk = ZSTD_DStreamOutSize();
    if (!state.in_use) {
        const auto size = ZSTD_findDecompressedSize(input.data(), input.size());
        if (size != ZSTD_CONTENTSIZE_UNKNOWN && size != ZSTD_CONTENTSIZE_ERROR && size < chunk) {
            chunk = static_cast<std::size_t>(size);
        }
        state.in_use = true;
    }
    return chunk;
}

TermResult<Term> decompress_stream(ProcessContext &context, const Handle &handle, const BinaryArgument &input,
                                   const Flush &flush) {
    auto &state = decompressor(context, handle);
    const auto chunk = decompress_chunk(state, input.bytes);
    try {
        const auto directive = flush_argument(flush);
        const auto pass = run(context, input.bytes, chunk, [&](ZSTD_outBuffer &out, ZSTD_inBuffer &in) {
            return ZSTD_decompressStream(state.d, &out, &in);
        });
        return stream_result(context, pass, input.bytes, directive);
    } catch (const BuiltinFailure &) {
        ZSTD_DCtx_reset(state.d, ZSTD_reset_session_only);
        state.in_use = false;
        throw;
    }
}

Term compress_reset(ProcessContext &context, const Handle &handle) {
    auto &state = compressor(context, handle);
    ZSTD_CCtx_reset(state.c, ZSTD_reset_session_only);
    state.in_use = false;
    return atom(context, "ok"_atom);
}

Term decompress_reset(ProcessContext &context, const Handle &handle) {
    auto &state = decompressor(context, handle);
    ZSTD_DCtx_reset(state.d, ZSTD_reset_session_only);
    state.in_use = false;
    return atom(context, "ok"_atom);
}

Term compress_close(ProcessContext &context, const Handle &handle) {
    close_context(compressor(context, handle), true);
    return atom(context, "ok"_atom);
}

Term decompress_close(ProcessContext &context, const Handle &handle) {
    close_context(decompressor(context, handle), false);
    return atom(context, "ok"_atom);
}

// {error, Reason} when libzstd made no dictionary.
TermResult<Term> no_dictionary(ProcessContext &context, AtomName reason) {
    const std::array parts{atom(context, "error"_atom), atom(context, reason)};
    return TermFactory(context).tuple(parts);
}

TermResult<Term> dictionary_handle(ProcessContext &context, Shared *shared, std::uint64_t magic) {
    return make_handle(context, Dictionary{.magic = magic, .shared = shared}, finalize_dictionary);
}

TermResult<Term> create_cdict(ProcessContext &context, const BinaryArgument &data, std::int64_t level) {
    const auto &bytes = data.bytes;
    auto *dictionary =
        ZSTD_createCDict_advanced(bytes.data(), bytes.size(), ZSTD_dlm_byCopy, ZSTD_dct_auto,
                                  ZSTD_getCParams(static_cast<int>(level), 0, bytes.size()), ZSTD_defaultCMem);
    if (dictionary == nullptr) {
        return no_dictionary(context, "invalid_compress_dict"_atom);
    }
    auto *shared = new Shared{.references = 1, .compress = true, .c = dictionary};
    return dictionary_handle(context, shared, CDICT_MAGIC);
}

TermResult<Term> create_ddict(ProcessContext &context, const BinaryArgument &data) {
    const auto &bytes = data.bytes;
    auto *dictionary =
        ZSTD_createDDict_advanced(bytes.data(), bytes.size(), ZSTD_dlm_byCopy, ZSTD_dct_auto, ZSTD_defaultCMem);
    if (dictionary == nullptr) {
        return no_dictionary(context, "invalid_decompress_dict"_atom);
    }
    auto *shared = new Shared{.references = 1, .compress = false, .d = dictionary};
    return dictionary_handle(context, shared, DDICT_MAGIC);
}

TermResult<Term> dict_id_of_cdict(ProcessContext &context, const DictionaryHandle &dictionary) {
    return TermFactory(context).integer(ZSTD_getDictID_fromCDict(dictionary_of(dictionary, CDICT_MAGIC).c));
}

TermResult<Term> dict_id_of_ddict(ProcessContext &context, const DictionaryHandle &dictionary) {
    return TermFactory(context).integer(ZSTD_getDictID_fromDDict(dictionary_of(dictionary, DDICT_MAGIC).d));
}

TermResult<Term> dict_id_of_dict(ProcessContext &context, const Data &data) {
    const auto bytes = iodata(data);
    return TermFactory(context).integer(ZSTD_getDictID_fromDict(bytes.data(), bytes.size()));
}

TermResult<Term> dict_id_of_frame(ProcessContext &context, const Data &data) {
    const auto bytes = iodata(data);
    return TermFactory(context).integer(ZSTD_getDictID_fromFrame(bytes.data(), bytes.size()));
}

// An unsigned 64-bit number as an integer term.
TermResult<Term> unsigned_integer(ProcessContext &context, unsigned long long value) {
    if (value <= static_cast<unsigned long long>(INT64_MAX)) {
        return TermFactory(context).integer(static_cast<std::int64_t>(value));
    }
    return TermFactory(context).integer_decimal(std::to_string(value));
}

// {ok, #{...}} with the frame header's fields; the frame content size is undefined when unknown.
TermResult<Term> get_frame_header(ProcessContext &context, const Data &data) {
    const auto bytes = iodata(data);
    ZSTD_FrameHeader header{};
    checked(context, ZSTD_getFrameHeader(&header, bytes.data(), bytes.size()));
    TermFactory factory(context);
    const auto content = header.frameContentSize == ZSTD_CONTENTSIZE_UNKNOWN
                             ? atom(context, "undefined"_atom)
                             : need(unsigned_integer(context, header.frameContentSize));
    const std::array entries{
        std::pair{atom(context, "frameContentSize"_atom), content},
        std::pair{atom(context, "windowSize"_atom), need(unsigned_integer(context, header.windowSize))},
        std::pair{atom(context, "blockSizeMax"_atom), need(factory.integer(header.blockSizeMax))},
        std::pair{atom(context, "frameType"_atom),
                  atom(context, header.frameType == ZSTD_frame ? "ZSTD_frame"_atom : "ZSTD_skippableFrame"_atom)},
        std::pair{atom(context, "headerSize"_atom), need(factory.integer(header.headerSize))},
        std::pair{atom(context, "dictID"_atom), need(factory.integer(header.dictID))},
        std::pair{atom(context, "checksumFlag"_atom),
                  atom(context, header.checksumFlag != 0 ? "true"_atom : "false"_atom)},
    };
    const std::array parts{atom(context, "ok"_atom), need(factory.map(entries))};
    return factory.tuple(parts);
}

constexpr std::array ZSTD_BUILTINS{
    typed_entry<init_compress>("clause_zstd", "init_compress"),
    typed_entry<init_decompress>("clause_zstd", "init_decompress"),
    typed_entry<load_compress_dictionary>("clause_zstd", "load_compress_dictionary"),
    typed_entry<load_decompress_dictionary>("clause_zstd", "load_decompress_dictionary"),
    typed_entry<ref_compress_dictionary>("clause_zstd", "ref_compress_dictionary"),
    typed_entry<ref_decompress_dictionary>("clause_zstd", "ref_decompress_dictionary"),
    typed_entry<set_compress_parameter>("clause_zstd", "set_compress_parameter"),
    typed_entry<get_compress_parameter>("clause_zstd", "get_compress_parameter"),
    typed_entry<set_pledged_src_size>("clause_zstd", "set_pledged_src_size"),
    typed_entry<set_decompress_parameter>("clause_zstd", "set_decompress_parameter"),
    typed_entry<get_decompress_parameter>("clause_zstd", "get_decompress_parameter"),
    typed_entry<compress_stream>("clause_zstd", "compress_stream"),
    typed_entry<decompress_stream>("clause_zstd", "decompress_stream"),
    typed_entry<compress_reset>("clause_zstd", "compress_reset"),
    typed_entry<decompress_reset>("clause_zstd", "decompress_reset"),
    typed_entry<compress_close>("clause_zstd", "compress_close"),
    typed_entry<decompress_close>("clause_zstd", "decompress_close"),
    typed_entry<create_cdict>("clause_zstd", "create_cdict"),
    typed_entry<create_ddict>("clause_zstd", "create_ddict"),
    typed_entry<dict_id_of_cdict>("clause_zstd", "dict_id_of_cdict"),
    typed_entry<dict_id_of_ddict>("clause_zstd", "dict_id_of_ddict"),
    typed_entry<dict_id_of_dict>("clause_zstd", "dict_id_of_dict"),
    typed_entry<dict_id_of_frame>("clause_zstd", "dict_id_of_frame"),
    typed_entry<get_frame_header>("clause_zstd", "get_frame_header"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> zstd_builtins() noexcept { return builtins::ZSTD_BUILTINS; }
} // namespace clause::runtime
