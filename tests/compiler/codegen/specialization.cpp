#include "codegen/specialization.hpp"
#include "codegen/optimization.hpp"
#include "codegen/verification.hpp"
#include "lowering_support.hpp"

using R = cg::Representation;

// Model repeated checks that real sources rarely make profitable, so the planner's limits are reached.
cg::SpecializationInput useful(std::string module, std::string symbol) {
    return {std::move(module),
            std::move(symbol),
            1000,
            {850, 30, 30, 30},
            {{R::small_integer, R::generic, R::generic, R::generic},
             {R::small_integer, R::small_integer, R::generic, R::generic},
             {R::small_integer, R::generic, R::small_integer, R::generic},
             {R::small_integer, R::generic, R::generic, R::small_integer}}};
}

// Pin deterministic hard caps and deduplication, including reversed input ordering.
void budgets() {
    cg::CompilationRequest request;
    request.optimization = cg::OptimizationLevel::speed;
    std::vector<cg::SpecializationInput> inputs;
    for (unsigned module = 0; module < 5; ++module) {
        for (unsigned function = 0; function < 12; ++function) {
            inputs.push_back(useful(std::to_string(module), std::to_string(function)));
        }
    }
    const auto plan = cg::plan_specializations(request, inputs);
    require(plan.candidates.size() == 128, "target cap not enforced");
    std::map<std::string, unsigned> modules;
    std::map<std::pair<std::string, std::string>, unsigned> functions;
    for (const auto &candidate : plan.candidates) {
        require(++modules[candidate.module] <= 32, "module cap not enforced");
        require(++functions[{candidate.module, candidate.symbol}] <= 3, "function cap not enforced");
    }
    std::ranges::reverse(inputs);
    const auto reversed = cg::plan_specializations(request, inputs);
    for (std::size_t i = 0; i < plan.candidates.size(); ++i) {
        require(plan.candidates[i].symbol == reversed.candidates[i].symbol &&
                    plan.candidates[i].profile == reversed.candidates[i].profile &&
                    plan.candidates[i].module == reversed.candidates[i].module,
                "nondeterministic ranking");
    }
}

// Reject unprofitable growth, deduplicate representation-equivalent sites and bound adversarial profiles.
void policies() {
    cg::CompilationRequest request;
    std::vector inputs{useful("module", "function")};
    require(cg::plan_specializations(request, inputs).candidates.empty(), "O0 specialized");
    request.optimization = cg::OptimizationLevel::speed;
    request.disable_type_specialization = true;
    require(cg::plan_specializations(request, inputs).candidates.empty(), "override ignored");
    request.disable_type_specialization = false;
    const auto repeated_profile = inputs.front().profiles.front();
    inputs.front().profiles.assign(5000, repeated_profile);
    const auto repeated = cg::plan_specializations(request, inputs);
    require(repeated.candidates.size() == 1 && repeated.decisions.contains(cg::SpecializationReason::duplicate) &&
                repeated.decisions.contains(cg::SpecializationReason::work_limit),
            "profile work not bounded");
    inputs.front().checks.assign(4, 1);
    require(cg::plan_specializations(request, inputs).candidates.empty(), "growth estimate ignored dispatch");
    inputs.front().checks.assign(255, 0);
    inputs.front().profiles.assign(5000, cg::TypeProfile(255, R::generic));
    require(cg::plan_specializations(request, inputs).candidates.empty(), "unknown union product enumerated");
}

// An exhausted inference budget yields no proofs, and the generic code still verifies and emits.
void exhausted_inference() {
    auto exhausted = fixtures({"fact_budget.erl", "client.erl", "answer.erl"}, "", cg::OptimizationLevel::speed);
    require(analyze_and_lower(exhausted, {.syntax_work = 3}), "exhausted inference rejected generic compilation");
    require(cg::detail::state(exhausted).specializations.candidates.empty(), "missing facts became proofs");
    require(cg::verify_ir(exhausted) && cg::optimize(exhausted) && cg::emit_objects(exhausted),
            "inference budget fallback failed to verify or emit supported representations");
}

// Observe acceptance and each bounded rejection reason in the progress trace.
void trace_profiles() {
    cg::CompilationRequest request;
    std::string events;
    request.progress = [&](const cg::CompilationProgress &event) { events += event.detail + "\n"; };
    std::vector inputs{useful("module", "function")};
    (void)cg::plan_specializations(request, inputs);
    request.optimization = cg::OptimizationLevel::speed;
    (void)cg::plan_specializations(request, inputs);
    inputs.front().checks.assign(4, 1);
    (void)cg::plan_specializations(request, inputs);
    inputs.front() = useful("module", "function");
    const auto repeated = inputs.front().profiles.front();
    inputs.front().profiles.assign(5000, repeated);
    (void)cg::plan_specializations(request, inputs);
    inputs.front().checks.assign(4, 0);
    (void)cg::plan_specializations(request, inputs);
    for (const auto *reason : {"accepted-benefit", "disabled-by-policy", "variant-limit", "growth-limit", "work-limit",
                               "duplicate-profile", "no-benefit"}) {
        require(events.find(reason) != std::string::npos, "specialization decision lost its reason");
    }
}

// Validate the planner's bounds and the inference budget fallback.
int main() {
    try {
        budgets();
        policies();
        exhausted_inference();
        trace_profiles();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
