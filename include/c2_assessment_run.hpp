#pragma once

#include "c2_reference_assessment.hpp"
#include <chrono>
#include <functional>
#include <memory>

enum class C2AssessmentRunState { complete, cancelled, timed_out, failed };
struct C2AssessmentRunResult {
    C2AssessmentRunState state = C2AssessmentRunState::failed;
    CapabilityFlag assessment;
    C2AssessmentCounts counts;
    std::string error;
};
using C2AssessmentReaderFactory = std::function<std::unique_ptr<CddaReader>()>;

// A raw MediaWorker operation: the caller holds DriveAccessCoordinator access.
// Factory must supply a direct, one-sector, strict-C2 reader with no fallback.
// No offset/repeat wrappers, PCM storage, or automatic acquisition are used.
C2AssessmentRunResult run_c2_assessment_raw(
    const C2Reference& reference, const C2AssessmentReaderFactory& factory,
    const std::function<bool()>& current,
    SteadyNow now = [] { return std::chrono::steady_clock::now(); },
    std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);

// Same raw operation with TOC checks sharing its four-second budget.
// TOC/context failures throw; the caller still owns drive access and reader options.
C2AssessmentRunResult run_c2_assessment_with_toc(
    const C2Reference& reference, const C2AssessmentReaderFactory& factory,
    const std::function<DiscToc()>& read_toc, const std::function<bool()>& current,
    SteadyNow now = [] { return std::chrono::steady_clock::now(); });
