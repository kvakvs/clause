#pragma once
#include <clause/abi/feature_diagnostic.hpp>
#include <clause/abi/status.hpp>
#include <expected>

namespace clause::runtime {
struct DiagnosticSink {
    // Borrow callback state for the report; the caller must retain it through invocation.
    void *context = nullptr;
    // Consume a fully formatted message without newline; null selects stderr. False means delivery failed.
    bool (*write)(void *, std::string_view) = nullptr;
};

// One owner-side failure per operation; propagation reads status without reporting a second time.
// This host-side object is not a generated-function parameter and must not be shared across concurrent operations.
class FeatureFailure final {
  public:
    // Reserve a report without emitting anything; default delivery goes to stderr.
    explicit FeatureFailure(DiagnosticSink sink = {}) noexcept;
    FeatureFailure(const FeatureFailure &) = delete;
    FeatureFailure &operator=(const FeatureFailure &) = delete;
    FeatureFailure(FeatureFailure &&) = delete;
    FeatureFailure &operator=(FeatureFailure &&) = delete;
    ~FeatureFailure() = default;
    // Latch the first failure, translate sink/formatting exceptions and return an explicit project status.
    abi::v1::Status report(abi::v1::FeatureId feature, const abi::v1::FeatureContext &context = {}) noexcept;
    // Inspect/propagate the existing outcome; OK means no failure has been reported by this owner.
    abi::v1::Status status() const noexcept;

  private:
    // Retain only a borrowed sink; no runtime-wide diagnostic state or LLVM dependency is required.
    DiagnosticSink sink_;
    // Prevent retries, reformatting and duplicate output while failure propagates through callers.
    abi::v1::Status status_ = abi::v1::Status::ok;
};

// Translate the catalog status once at a host boundary; callers propagate the typed error unchanged.
template <typename Error>
std::unexpected<Error> deferred_service(const abi::v1::FeatureId feature, const std::string_view operation,
                                        const DiagnosticSink sink = {}) noexcept {
    FeatureFailure failure(sink);
    const auto status = failure.report(feature, {.operation = operation});
    return std::unexpected(status == abi::v1::Status::not_implemented ? Error::not_implemented
                                                                      : Error::diagnostic_failure);
}
} // namespace clause::runtime
