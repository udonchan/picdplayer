#include "complete_pcm_session.hpp"
#include "complete_pcm_sha256.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
const char* state_name(const std::optional<CompletePcmState>& state) {
    if (!state) return "IDLE";
    switch (*state) {
    case CompletePcmState::collecting: return "COLLECTING";
    case CompletePcmState::prepared: return "PREPARED";
    case CompletePcmState::complete: return "COMPLETE";
    case CompletePcmState::incomplete: return "INCOMPLETE";
    case CompletePcmState::invalid: return "INVALID";
    }
    return "INVALID";
}
const char* failure_name(CompletePcmWorkerFailure failure) {
    switch (failure) {
    case CompletePcmWorkerFailure::none: return "NONE";
    case CompletePcmWorkerFailure::cancelled: return "CANCELLED";
    case CompletePcmWorkerFailure::context_changed: return "CONTEXT_CHANGED";
    case CompletePcmWorkerFailure::not_ready: return "NOT_READY";
    case CompletePcmWorkerFailure::job_deadline: return "JOB_DEADLINE";
    case CompletePcmWorkerFailure::call_deadline: return "CALL_DEADLINE";
    case CompletePcmWorkerFailure::clock_regressed: return "CLOCK_REGRESSED";
    case CompletePcmWorkerFailure::clock_failed: return "CLOCK_FAILED";
    case CompletePcmWorkerFailure::operation_failed: return "OPERATION_FAILED";
    case CompletePcmWorkerFailure::core_failed: return "CORE_FAILED";
    case CompletePcmWorkerFailure::toc_changed: return "TOC_CHANGED";
    case CompletePcmWorkerFailure::memory_limit: return "MEMORY_LIMIT";
    }
    return "UNKNOWN";
}
const char* core_failure_name(CompletePcmFailure failure) {
    constexpr std::array names{"NONE", "CANCELLED", "CONTEXT_CHANGED", "CLOCK_REGRESSED", "DEADLINE",
        "READ_ERROR", "INVALID_BLOCK", "VERIFICATION_FAILED", "CONSUMER_FAILED", "RESULT_LIMIT",
        "UNFINISHED", "TOC_CHANGED", "REENTRANT_CALL"};
    const auto index = static_cast<std::size_t>(failure);
    return index < names.size() ? names[index] : "UNKNOWN";
}
Json identity_json(const CompletePcmIdentity& id) {
    return {{"session_id", id.session_id}, {"job_id", id.job_id},
        {"disc_generation", id.disc_generation}, {"reader_incarnation", id.reader_incarnation},
        {"device_observation_generation", id.device_observation_generation},
        {"policy_revision", id.policy_revision}, {"offset_samples", *id.offset_samples}};
}
}

CompletePcmSession::CompletePcmSession(std::string session_id, std::string backend,
    CompletePcmWorker::Factory factory, CompletePcmWorker::TocProbe probe,
    std::shared_ptr<DriveAccessCoordinator> access)
    : session_id_(std::move(session_id)), backend_(std::move(backend)),
      worker_(std::move(factory), std::move(probe), std::move(access)) {}

const char* CompletePcmSession::start_rejection(const CompletePcmContext& c) const {
    if (status_.busy) return status_.input_state == CompletePcmState::invalid ||
        status_.input_state == CompletePcmState::incomplete ? "capture_releasing" : "capture_busy";
    if (!c.device_current || c.observation_generation == 0) return "device_not_current";
    if (!c.gate.audio_ready || !c.toc || c.disc_generation == 0) return "audio_not_ready";
    if (!c.gate.playback_stopped) return "playback_not_stopped";
    if (!c.gate.output_stopped || !c.gate.queue_cancelled) return "output_not_quiescent";
    if (!c.gate.playback_reader_released) return "playback_reader_releasing";
    if (!c.media_idle) return "media_busy";
    if (c.policy_pending) return "policy_pending";
    if (!c.offset_samples) return "offset_unknown";
    if (*c.offset_samples != 0) return "offset_unsupported";
    try { (void)complete_pcm_reservation(c.policy, complete_pcm_sha256_profile()); }
    catch (const std::invalid_argument&) { return "policy_budget_unsupported"; }
    return nullptr;
}

CompletePcmIdentity CompletePcmSession::identity(const CompletePcmContext& c) const {
    auto id = plan_->identity;
    id.disc_generation = c.disc_generation;
    id.device_observation_generation = c.observation_generation;
    id.policy_revision = c.policy_revision;
    id.offset_samples = c.offset_samples;
    return id;
}

bool CompletePcmSession::current(const CompletePcmContext& c) const {
    return plan_ && c.device_current && c.gate.ready() &&
        !c.policy_pending && identity(c) == plan_->identity;
}

ApiCommandResult CompletePcmSession::start(const ApiCommand& command, const CompletePcmContext& c) {
    if (command.session_id != session_id_ || command.disc_generation != c.disc_generation)
        return ApiCommandFailure::capture_context_changed;
    if (status_.busy) return ApiCommandFailure::capture_busy;
    if (start_rejection(c)) return ApiCommandFailure::capture_not_ready;
    if (next_job_ == std::numeric_limits<std::uint64_t>::max()) return false;
    CompletePcmPlan plan;
    plan.toc = *c.toc;
    plan.identity = {session_id_, ++next_job_, c.disc_generation, next_job_, c.policy_revision,
                     c.offset_samples, complete_pcm_sha256_profile(), c.observation_generation};
    plan.policy = c.policy;
    plan.duration = std::chrono::milliseconds(command.duration_ms);
    try {
        if (!worker_.start(plan, make_complete_pcm_sha256_consumer(), c.gate)) return false;
    } catch (const std::exception&) { return ApiCommandFailure::capture_not_ready; }
    plan_ = std::move(plan);
    manifest_.clear();
    status_ = worker_.status();
    return true;
}

ApiCommandResult CompletePcmSession::cancel(const ApiCommand& command) {
    if (command.session_id != session_id_) return ApiCommandFailure::capture_context_changed;
    if (!plan_ || command.job_id != plan_->identity.job_id) return ApiCommandFailure::capture_no_job;
    worker_.cancel();
    // A cancellation of a completed receipt revokes it too.
    if (!status_.busy) worker_.invalidate();
    manifest_.clear();
    status_ = worker_.status();
    return true;
}

bool CompletePcmSession::interrupt() {
    worker_.invalidate();
    manifest_.clear();
    status_ = worker_.status();
    return status_.busy;
}

void CompletePcmSession::poll(const CompletePcmContext& c) {
    if (!plan_) return;
    if (!current(c) || (status_.busy && !c.media_idle)) worker_.invalidate();
    status_ = worker_.poll(identity(c), c.gate);
    if (status_.phase == CompletePcmWorkerPhase::prepared && current(c)) {
        worker_.confirm(identity(c), c.gate);
        status_ = worker_.status();
    }
    if (status_.input_state != CompletePcmState::complete || !current(c)) {
        manifest_.clear();
        return;
    }
    if (!manifest_.empty()) return;
    const auto payload = worker_.result(identity(c));
    if (payload.empty()) return;
    Json tracks = Json::array();
    for (const auto& track : plan_->toc.tracks)
        tracks.push_back({{"number", track.number}, {"start_lba", track.start_lba}, {"length_frames", track.length_frames}});
    const auto& p = plan_->policy;
    const auto& read = status_.read_observations;
    Json manifest{{"schema_version", 1}, {"kind", "complete_pcm_manifest"},
        {"duration_ms", plan_->duration.count()},
        {"identity", identity_json(plan_->identity)}, {"backend", backend_},
        {"toc", {{"tracks", std::move(tracks)}, {"leadout_lba", plan_->toc.leadout_lba}}},
        {"coverage", {{"begin_lba", plan_->toc.tracks.front().start_lba}, {"end_lba", plan_->toc.leadout_lba},
                      {"frames", status_.accepted_frames}, {"bytes", status_.accepted_bytes}}},
        {"read_policy", {{"mode", read_verification_mode_name(p.mode)}, {"region_frames", p.region_frames},
                         {"required_matches", p.required_matches}, {"maximum_attempts", p.maximum_attempts},
                         {"time_budget_ms", p.time_budget_ms}}},
        {"reader_released", status_.reader_released}, {"cache_independence", "NOT_PROVEN"},
        {"local_read_observations", {{"read_calls", read.read_calls}, {"direct_retries", read.direct_retries},
            {"verification_attempts", read.verification_attempts}, {"verification_mismatches", read.verification_mismatches},
            {"verified_calls", read.verified_calls}, {"uncertain_reads", status_.uncertain_reads},
            {"recovered_reads", status_.recovered_reads}}},
        {"external_verification", "NOT_CHECKED"}, {"fingerprint", Json::parse(payload.begin(), payload.end())}};
    auto bytes = manifest.dump();
    if (bytes.size() > complete_pcm_result_limit) {
        worker_.invalidate(); status_ = worker_.status();
    } else manifest_ = std::move(bytes);
}

ApiResponse CompletePcmSession::response(const CompletePcmContext& c, bool result) const {
    if (result) {
        if (!current(c) || status_.input_state != CompletePcmState::complete || manifest_.empty())
            return {404, "application/json", R"({"error":"complete_pcm_result_unavailable"})"};
        return {200, "application/json", manifest_};
    }
    const auto* reason = start_rejection(c);
    const auto& profile = complete_pcm_sha256_profile();
    Json status{{"schema_version", 1}, {"session_id", session_id_}, {"disc_generation", c.disc_generation},
        {"duration_ms", plan_ ? plan_->duration.count() : 0},
        {"state", plan_ && !current(c) ? "INVALID" : state_name(status_.input_state)},
        {"failure", plan_ && !current(c) ? "CONTEXT_CHANGED" : failure_name(status_.failure)},
        {"job_id", plan_ ? plan_->identity.job_id : 0}, {"busy", status_.busy},
        {"reader_released", status_.reader_released}, {"waiting_for_access", status_.waiting_for_access},
        {"call_inflight", status_.call_inflight}, {"accepted_frames", status_.accepted_frames},
        {"accepted_bytes", status_.accepted_bytes}, {"reserved_bytes", status_.reserved_bytes},
        {"native_error", status_.native_error},
        {"core_failure", core_failure_name(status_.core_failure)},
        {"uncertain_reads", status_.uncertain_reads}, {"recovered_reads", status_.recovered_reads},
        {"can_start", reason == nullptr}, {"start_rejection", reason ? Json(reason) : Json(nullptr)},
        {"media_activity", c.media_activity},
        {"profile", {{"canonical", profile.canonical}, {"algorithm", profile.algorithm}, {"version", profile.version}}},
        {"external_verification", "NOT_CHECKED"}};
    return {200, "application/json", status.dump()};
}
