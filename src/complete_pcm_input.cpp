#include "complete_pcm_input.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
bool same_toc(const DiscToc& a, const DiscToc& b) {
    if (a.leadout_lba != b.leadout_lba || a.tracks.size() != b.tracks.size()) return false;
    for (std::size_t i = 0; i < a.tracks.size(); ++i) {
        if (a.tracks[i].number != b.tracks[i].number || a.tracks[i].start_lba != b.tracks[i].start_lba ||
            a.tracks[i].length_frames != b.tracks[i].length_frames) return false;
    }
    return true;
}
bool valid_name(const std::string& value) { return !value.empty() && value.size() <= 128; }
}

std::size_t complete_pcm_reservation(const ReadPolicy& policy, const CompletePcmProfile& profile) {
    if (policy.mode != ReadVerificationMode::single && policy.mode != ReadVerificationMode::repeat)
        throw std::invalid_argument("complete PCM read mode");
    if (profile.state_bytes > complete_pcm_consumer_limit || !profile.maximum_result_bytes ||
        profile.maximum_result_bytes > complete_pcm_result_limit)
        throw std::invalid_argument("complete PCM consumer budget");
    validate_read_policy(policy, 75);
    // Conservative reader reservation for B, plus core/consumer/manifest space.
    const auto bytes = (std::size_t(policy.maximum_attempts) + 2) * (std::size_t(policy.region_frames) + 75 + 1) * 2352
        + complete_pcm_chunk_bytes + profile.state_bytes + 2 * complete_pcm_result_limit;
    if (bytes > complete_pcm_memory_limit) throw std::invalid_argument("complete PCM memory budget");
    return bytes;
}

CompletePcmInput::CompletePcmInput(const CompletePcmPlan& plan,
    std::unique_ptr<CompletePcmConsumer> consumer, SteadyNow now)
    : consumer_(std::move(consumer)), now_(std::move(now)) {
    const auto& id = plan.identity;
    const auto& profile = id.profile;
    if (!consumer_ || !now_ || !valid_name(id.session_id) || !id.job_id || !id.disc_generation ||
        !id.reader_incarnation || !id.offset_samples || *id.offset_samples != 0 ||
        profile.canonical != complete_pcm_canonical_profile || !valid_name(profile.algorithm) ||
        !valid_name(profile.version) ||
        profile != consumer_->profile() || plan.duration.count() <= 0 || plan.duration > complete_pcm_deadline_limit ||
        plan.toc.tracks.empty() || plan.toc.tracks.size() > 99)
        throw std::invalid_argument("ineligible complete PCM plan");
    std::array<std::int32_t, 99> starts{};
    for (std::size_t i = 0; i < plan.toc.tracks.size(); ++i) starts[i] = plan.toc.tracks[i].start_lba;
    toc_ = make_audio_toc(plan.toc.tracks.front().number,
                         std::span(starts).first(plan.toc.tracks.size()), plan.toc.leadout_lba);
    if (!same_toc(toc_, plan.toc)) throw std::invalid_argument("inconsistent complete PCM TOC");
    reserved_bytes_ = complete_pcm_reservation(plan.policy, profile);
    identity_ = id;
    policy_ = plan.policy;
    next_lba_ = toc_.tracks.front().start_lba;
    last_now_ = now_();
    const auto duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(plan.duration);
    if (last_now_.time_since_epoch() > std::chrono::steady_clock::duration::max() - duration)
        throw std::invalid_argument("complete PCM deadline overflow");
    deadline_ = last_now_ + duration;
}

CompletePcmInput::~CompletePcmInput() {
    if (!discarded_) consumer_->discard();
}

void CompletePcmInput::discard_if_terminal() noexcept {
    if (!in_callback_ && !discarded_ &&
        (state_ == CompletePcmState::incomplete || state_ == CompletePcmState::invalid)) {
        discarded_ = true;
        consumer_->discard();
    }
}

void CompletePcmInput::fail(CompletePcmState state, CompletePcmFailure reason) noexcept {
    state_ = state;
    failure_ = reason;
    result_ = {};
    discard_if_terminal();
}

void CompletePcmInput::cancel() noexcept {
    if (state_ == CompletePcmState::collecting || state_ == CompletePcmState::prepared)
        fail(CompletePcmState::incomplete, CompletePcmFailure::cancelled);
}

void CompletePcmInput::invalidate() noexcept {
    fail(CompletePcmState::invalid, CompletePcmFailure::context_changed);
}

bool CompletePcmInput::check(const CompletePcmIdentity& current) {
    if (in_callback_) {
        fail(CompletePcmState::invalid, CompletePcmFailure::reentrant_call); return false;
    }
    if (state_ == CompletePcmState::incomplete || state_ == CompletePcmState::invalid) return false;
    if (current != identity_ || consumer_->profile() != identity_.profile) {
        invalidate(); return false;
    }
    const auto now = now_();
    if (now < last_now_) {
        fail(CompletePcmState::invalid, CompletePcmFailure::clock_regressed); return false;
    }
    last_now_ = now;
    if (state_ != CompletePcmState::complete && now >= deadline_) {
        fail(CompletePcmState::incomplete, CompletePcmFailure::deadline); return false;
    }
    return true;
}

std::chrono::milliseconds CompletePcmInput::call_budget(const CompletePcmIdentity& current) {
    if (!check(current) || state_ != CompletePcmState::collecting) return std::chrono::milliseconds{0};
    return std::min(std::chrono::milliseconds(policy_.time_budget_ms),
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - last_now_));
}

bool CompletePcmInput::append(const CompletePcmIdentity& current, const ReadResult& read,
                             std::span<const std::int16_t> pcm) {
    if (!check(current)) return false;
    if (state_ == CompletePcmState::prepared) {
        fail(CompletePcmState::incomplete, CompletePcmFailure::invalid_block); return false;
    }
    if (state_ != CompletePcmState::collecting) return false;
    const auto limit = policy_.mode == ReadVerificationMode::single ? 15 : policy_.region_frames;
    if (read.status != ReadStatus::ok) {
        fail(CompletePcmState::incomplete, CompletePcmFailure::read_error); return false;
    }
    if (!read.frames_requested || read.frames_requested > limit || read.frames_read != read.frames_requested ||
        read.start_lba != next_lba_ || read.frames_read > static_cast<std::size_t>(toc_.leadout_lba - next_lba_) ||
        pcm.size() != read.frames_read * cdda_samples_per_frame || !read.offset || read.offset->samples != 0) {
        fail(CompletePcmState::incomplete, CompletePcmFailure::invalid_block); return false;
    }
    const auto& verification = read.verification;
    const auto requested_overlap = policy_.mode == ReadVerificationMode::repeat
        ? repeated_read_policy(policy_).overlap_frames : 0;
    const auto compared_overlap = std::min<std::size_t>(requested_overlap, preceding_frames_);
    const auto expected_overlap = !requested_overlap ? OverlapVerification::not_requested
        : compared_overlap ? OverlapVerification::matched : OverlapVerification::stream_boundary;
    if (verification.time_budget_exhausted || verification.overlap != expected_overlap ||
        verification.overlap_frames_requested != requested_overlap ||
        verification.overlap_frames_compared != compared_overlap ||
        (policy_.mode == ReadVerificationMode::single && verification.attempts != 0) ||
        (policy_.mode == ReadVerificationMode::repeat &&
        (verification.attempts > policy_.maximum_attempts || verification.detail_count != verification.attempts ||
         verification.matching_reads < policy_.required_matches || verification.complete_reads < verification.matching_reads ||
         verification.complete_reads > verification.attempts || !verification.accepted_attempt ||
         !verification.accepted_candidate || *verification.accepted_attempt < 1 ||
         *verification.accepted_attempt > verification.attempts || *verification.accepted_candidate < 1 ||
         *verification.accepted_candidate > verification.complete_reads))) {
        fail(CompletePcmState::incomplete, CompletePcmFailure::verification_failed); return false;
    }
    observe_read(observations_, read);
    const auto evidence = make_read_evidence(read);
    uncertain_reads_ += evidence.status == IntegrityReadStatus::uncertain;
    recovered_reads_ += evidence.status == IntegrityReadStatus::recovered;
    std::size_t offset = 0, track = 0;
    while (offset < pcm.size()) {
        const auto position = std::int64_t(read.start_lba) * 588 + static_cast<std::int64_t>(offset / 2);
        while (position >= std::int64_t(toc_.tracks[track].start_lba + toc_.tracks[track].length_frames) * 588) ++track;
        const auto to_end = static_cast<std::size_t>(
            (std::int64_t(toc_.tracks[track].start_lba + toc_.tracks[track].length_frames) * 588 - position) * 2);
        const auto count = std::min({pcm.size() - offset, to_end, scratch_.size() / 2});
        for (std::size_t i = 0; i < count; ++i) {
            const auto sample = static_cast<std::uint16_t>(pcm[offset + i]);
            scratch_[2 * i] = static_cast<std::uint8_t>(sample);
            scratch_[2 * i + 1] = static_cast<std::uint8_t>(sample >> 8);
        }
        in_callback_ = true;
        try { consumer_->consume(toc_.tracks[track].number, std::span(scratch_).first(count * 2)); }
        catch (...) {
            in_callback_ = false;
            if (state_ == CompletePcmState::collecting)
                fail(CompletePcmState::incomplete, CompletePcmFailure::consumer_failed);
            else discard_if_terminal();
            return false;
        }
        in_callback_ = false;
        discard_if_terminal();
        if (!check(current)) return false;
        offset += count;
    }
    next_lba_ += static_cast<std::int32_t>(read.frames_read);
    preceding_frames_ = read.frames_read;
    return true;
}

bool CompletePcmInput::prepare(const CompletePcmIdentity& current) {
    if (!check(current) || state_ != CompletePcmState::collecting) return false;
    if (next_lba_ != toc_.leadout_lba) {
        fail(CompletePcmState::incomplete, CompletePcmFailure::unfinished); return false;
    }
    in_callback_ = true;
    std::span<const std::uint8_t> result;
    try { result = consumer_->finalize(); }
    catch (...) {
        in_callback_ = false;
        if (state_ == CompletePcmState::collecting)
            fail(CompletePcmState::incomplete, CompletePcmFailure::consumer_failed);
        else discard_if_terminal();
        return false;
    }
    in_callback_ = false;
    discard_if_terminal();
    if (!check(current)) return false;
    if (result.empty() || result.size() > identity_.profile.maximum_result_bytes) {
        fail(CompletePcmState::incomplete, CompletePcmFailure::result_limit); return false;
    }
    result_ = result;
    state_ = CompletePcmState::prepared;
    return true;
}

bool CompletePcmInput::confirm(const CompletePcmIdentity& current, const DiscToc& final_toc, bool reader_released) {
    if (!check(current) || (state_ != CompletePcmState::prepared && state_ != CompletePcmState::complete)) return false;
    if (!same_toc(toc_, final_toc)) {
        fail(CompletePcmState::invalid, CompletePcmFailure::toc_changed); return false;
    }
    if (!reader_released) return false;
    state_ = CompletePcmState::complete;
    return true;
}

std::uint64_t CompletePcmInput::accepted_frames() const {
    return static_cast<std::uint64_t>(next_lba_ - toc_.tracks.front().start_lba);
}

std::uint64_t CompletePcmInput::track_frames(std::size_t index) const {
    const auto& track = toc_.tracks.at(index);
    return static_cast<std::uint64_t>(std::clamp(std::int64_t(next_lba_) - track.start_lba,
                                              std::int64_t(0), track.length_frames));
}

std::span<const std::uint8_t> CompletePcmInput::result() const {
    return state_ == CompletePcmState::complete ? result_ : std::span<const std::uint8_t>{};
}
