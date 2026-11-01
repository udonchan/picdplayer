#include "c2_assessment_session.hpp"
#include <limits>
#include <nlohmann/json.hpp>

C2AssessmentSession::C2AssessmentSession(std::string session_id, MediaWorker& worker)
    : session_id_(std::move(session_id)), worker_(worker) {}

const char* C2AssessmentSession::start_rejection(const C2AssessmentContext& context) const {
    const auto& c = context.input;
    if (active_) return "assessment_busy";
    if (!context.direct) return "direct_backend_required";
    if (!context.c2_supported) return "c2_support_not_reported";
    if (!c.device_current || !c.gate.audio_ready || !c.toc || !c.disc_generation || !c.observation_generation)
        return "device_context_unavailable";
    if (!c.gate.ready()) return "playback_not_quiescent";
    if (!context.capture_idle) return "capture_busy_or_releasing";
    if (!c.media_idle || !worker_.idle()) return "media_busy";
    if (c.policy_pending) return "policy_pending";
    if (!c.offset_samples) return "offset_unknown";
    if (*c.offset_samples != 0) return "explicit_offset_zero_required";
    return "";
}

bool C2AssessmentSession::context_current(const C2AssessmentContext& context) const {
    const auto& c = context.input;
    return reference_ && c.device_current && c.gate.audio_ready && context.direct && context.c2_supported &&
        c.toc && c.disc_generation == disc_ && c.observation_generation == observation_ &&
        c.policy_revision == policy_ && !c.policy_pending && c.offset_samples && *c.offset_samples == 0;
}

ApiCommandResult C2AssessmentSession::start(const ApiCommand& command, const C2AssessmentContext& context) {
    if (active_) return ApiCommandFailure::c2_busy;
    if (command.session_id != session_id_ || command.disc_generation != context.input.disc_generation ||
        command.observation_generation != context.input.observation_generation) return ApiCommandFailure::c2_context_changed;
    if (*start_rejection(context) || !command.c2_reference || next_job_ == std::numeric_limits<std::uint64_t>::max())
        return ApiCommandFailure::c2_not_ready;
    try { C2ReferenceAssessment validate(*command.c2_reference); }
    catch (const std::exception&) { return ApiCommandFailure::c2_invalid_reference; }
    for (const auto& sector : command.c2_reference->sectors) {
        bool in_audio = false;
        for (const auto& track : context.input.toc->tracks)
            if (sector.lba >= track.start_lba && static_cast<std::int64_t>(sector.lba) <
                static_cast<std::int64_t>(track.start_lba) + track.length_frames) in_audio = true;
        if (!in_audio) return ApiCommandFailure::c2_not_ready;
    }
    const auto next = next_job_ + 1;
    auto scheduled = *command.c2_reference;
    scheduled.expected_toc = *context.input.toc;
    if (!worker_.request_c2(scheduled, next)) return ApiCommandFailure::c2_busy;
    next_job_ = job_ = next;
    disc_ = context.input.disc_generation;
    observation_ = context.input.observation_generation;
    policy_ = context.input.policy_revision;
    reference_ = std::move(scheduled);
    staged_.reset(); flag_ = {}; counts_ = {}; error_.clear();
    state_ = "RUNNING"; active_ = true; released_ = false;
    return true;
}

bool C2AssessmentSession::interrupt() {
    if (!active_) return false; // Completed scoped evidence remains until context changes/cancel.
    worker_.cancel_c2();
    staged_.reset(); flag_ = {}; state_ = "CANCELLED";
    released_ = worker_.idle();
    active_ = !released_;
    return active_;
}

ApiCommandResult C2AssessmentSession::cancel(const ApiCommand& command) {
    if (command.session_id != session_id_) return ApiCommandFailure::c2_context_changed;
    if (!job_ || command.job_id != job_) return ApiCommandFailure::c2_no_job;
    (void)interrupt();
    flag_ = {}; reference_.reset(); staged_.reset(); state_ = "CANCELLED";
    return true;
}

void C2AssessmentSession::offer(const MediaWorkerResult& result) {
    if (result.work != MediaWork::assess_c2 || result.c2_job_id != job_) return;
    released_ = true; // Callback returned after raw reader retirement.
    if (active_ && state_ == "RUNNING" && result.observation_generation == observation_) staged_ = result;
}

void C2AssessmentSession::poll(const C2AssessmentContext& context) {
    if (reference_ && (!context_current(context) ||
        (active_ && (!context.input.gate.ready() || !context.capture_idle)))) {
        (void)interrupt(); reference_.reset(); flag_ = {}; staged_.reset();
        if (!active_) state_ = "INVALIDATED";
    }
    if (!active_) return;
    if (state_ == "CANCELLED") {
        if (released_ || worker_.idle()) { active_ = false; released_ = true; }
        return;
    }
    if (!staged_) return;
    const auto result = std::move(*staged_);
    staged_.reset(); active_ = false; released_ = true;
    if (!context_current(context)) { state_ = "INVALIDATED"; return; }
    if (!result.error.empty() || !result.c2_assessment) {
        state_ = "FAILED"; error_ = result.error.empty() ? "C2 assessment result missing" : result.error;
        return;
    }
    const auto& run = *result.c2_assessment;
    counts_ = run.counts;
    error_ = run.error;
    if (run.state == C2AssessmentRunState::complete) { state_ = "COMPLETE"; flag_ = run.assessment; }
    else if (run.state == C2AssessmentRunState::cancelled) state_ = "CANCELLED";
    else if (run.state == C2AssessmentRunState::timed_out) state_ = "TIMED_OUT";
    else state_ = "FAILED";
}

CapabilityFlag C2AssessmentSession::flag(const C2AssessmentContext& context) const {
    if (state_ == "COMPLETE" && context_current(context)) return flag_;
    return {Knowledge::unknown, CapabilityEvidenceSource::none,
        "No current independent-reference sector C2 assessment: " + state_ +
        (error_.empty() ? "; explicit aligned reference and challenge required" : "; " + error_)};
}

ApiResponse C2AssessmentSession::response(const C2AssessmentContext& context) const {
    const auto evidence = flag(context);
    auto lbas = nlohmann::json::array();
    if (reference_ && context_current(context)) for (const auto& sector : reference_->sectors) lbas.push_back(sector.lba);
    nlohmann::json data = {{"profile", c2_assessment_profile}, {"session_id", session_id_}, {"job_id", job_},
        {"state", state_}, {"busy", active_}, {"reader_released", released_}, {"disc_generation", disc_},
        {"current_context", context_current(context)},
        {"reference_id", reference_ ? reference_->id : ""}, {"reference_lbas", lbas},
        {"reference_basis", "operator-asserted independent/aligned RAW-LBA S16LE offset0; not authenticated ground truth"},
        {"observation_generation", observation_}, {"start_rejection", start_rejection(context)},
        {"value", knowledge_name(evidence.value)}, {"source", capability_evidence_source_name(evidence.source)},
        {"detail", evidence.detail}, {"error", error_}, {"samples", counts_.samples}, {"unavailable", counts_.unavailable},
        {"clean_equal", counts_.clean_equal}, {"clean_different", counts_.clean_different},
        {"flagged_equal", counts_.flagged_equal}, {"flagged_different", counts_.flagged_different}};
    return {200, "application/json", data.dump()};
}
