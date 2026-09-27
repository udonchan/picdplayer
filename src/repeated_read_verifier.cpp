#include "cdda_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void add(ParanoiaEvents& to, const ParanoiaEvents& from) {
    to.reads += from.reads; to.verifies += from.verifies;
    to.fixups += from.fixups; to.skips += from.skips;
    to.read_errors += from.read_errors; to.cache_errors += from.cache_errors;
    to.other += from.other;
}

struct Candidate {
    std::vector<std::int16_t> pcm;
    unsigned matches = 1;
};

class RepeatedReadVerifier final : public CddaReader {
public:
    RepeatedReadVerifier(std::unique_ptr<CddaReader> reader, RepeatedReadPolicy policy,
                         SteadyNow now)
        : reader_(std::move(reader)), policy_(policy), now_(std::move(now)) {
        if (!reader_ || !now_) throw std::invalid_argument("repeated reader requires reader and clock");
        validate_repeated_read_policy(policy_);
    }

    void seek(std::int32_t lba) override {
        if (lba < 0) throw std::invalid_argument("negative CDDA LBA");
        position_ = lba;
        positioned_ = true;
        prior_tail_.clear();
        reader_->seek(lba);
    }

    ReadResult read(std::span<std::int16_t> pcm) override {
        if (!positioned_) throw std::logic_error("CDDA seek required before read");
        if (pcm.empty() || pcm.size() % cdda_samples_per_frame)
            throw std::invalid_argument("PCM buffer must contain whole CD frames");
        const auto frames = pcm.size() / cdda_samples_per_frame;
        if (frames > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - position_))
            throw std::invalid_argument("CDDA LBA overflow");

        const auto overlap_frames = std::min<std::size_t>(policy_.overlap_frames,
                                                           prior_tail_.size() / cdda_samples_per_frame);
        if (frames > std::numeric_limits<std::size_t>::max() - overlap_frames)
            throw std::invalid_argument("CDDA overlap size overflow");
        const auto physical_frames = frames + overlap_frames;
        const auto physical_start = position_ - static_cast<std::int32_t>(overlap_frames);
        ReadResult combined{position_, frames, 0, ReadStatus::read_error, EIO, 0};
        combined.read_independence = ReadIndependence::cache_possible;
        combined.verification.overlap_frames_requested = policy_.overlap_frames;
        combined.verification.overlap_frames_compared = static_cast<unsigned>(overlap_frames);
        combined.verification.overlap = overlap_frames ? OverlapVerification::mismatched
                                                        : OverlapVerification::stream_boundary;
        std::vector<Candidate> candidates;
        const auto started = now_();
        for (unsigned attempt = 0; attempt < policy_.maximum_attempts; ++attempt) {
            if (attempt && now_() - started >= policy_.time_budget) {
                combined.verification.time_budget_exhausted = true;
                break;
            }
            reader_->seek(physical_start);
            std::vector<std::int16_t> sample(physical_frames * cdda_samples_per_frame);
            const auto result = reader_->read(sample);
            ++combined.verification.attempts;
            auto& detail = combined.verification.details[combined.verification.detail_count++];
            detail.frames_read = result.frames_read;
            detail.complete = result.status == ReadStatus::ok && result.start_lba == physical_start &&
                              result.frames_read == physical_frames;
            detail.native_error = result.native_error;
            detail.direct_retries = result.retries;
            detail.physical_start_lba = physical_start;
            detail.observed_start_lba = result.start_lba;
            detail.physical_frames_requested = physical_frames;
            combined.retries += result.retries;
            add(combined.paranoia, result.paranoia);
            if (result.native_error) combined.native_error = result.native_error;
            if (!detail.complete) continue;

            ++combined.verification.complete_reads;
            auto found = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& candidate) {
                return candidate.pcm == sample;
            });
            if (found == candidates.end()) {
                if (!candidates.empty()) ++combined.verification.mismatches;
                candidates.push_back({std::move(sample), 1});
                found = std::prev(candidates.end());
            } else {
                ++found->matches;
            }
            detail.candidate = static_cast<unsigned>(std::distance(candidates.begin(), found)) + 1;
            combined.verification.matching_reads = std::max(combined.verification.matching_reads,
                                                             found->matches);
            if (found->matches >= policy_.required_matches) {
                combined.verification.accepted_candidate = detail.candidate;
                combined.verification.accepted_attempt = combined.verification.attempts;
                const auto output_begin = found->pcm.begin() +
                    static_cast<std::ptrdiff_t>(overlap_frames * cdda_samples_per_frame);
                if (overlap_frames && !std::equal(found->pcm.begin(), output_begin,
                                                   prior_tail_.begin(), prior_tail_.end())) {
                    combined.verification.overlap = OverlapVerification::mismatched;
                    combined.native_error = EILSEQ;
                    positioned_ = false;
                    return combined;
                }
                combined.verification.overlap = overlap_frames ? OverlapVerification::matched
                                                               : OverlapVerification::stream_boundary;
                std::copy(output_begin, found->pcm.end(), pcm.begin());
                const auto tail_frames = std::min<std::size_t>(policy_.overlap_frames, frames);
                const auto tail_samples = tail_frames * cdda_samples_per_frame;
                prior_tail_.assign(pcm.end() - static_cast<std::ptrdiff_t>(tail_samples), pcm.end());
                combined.frames_read = frames;
                combined.status = ReadStatus::ok;
                combined.native_error = 0;
                position_ += static_cast<std::int32_t>(frames);
                return combined;
            }
        }
        // Never expose an unmatched candidate. A subsequent call requires an
        // explicit seek, matching the base CddaReader failure contract.
        positioned_ = false;
        return combined;
    }

private:
    std::unique_ptr<CddaReader> reader_;
    RepeatedReadPolicy policy_;
    SteadyNow now_;
    std::int32_t position_ = 0;
    bool positioned_ = false;
    std::vector<std::int16_t> prior_tail_;
};
}

void validate_repeated_read_policy(const RepeatedReadPolicy& policy) {
    if (policy.required_matches < 2 || policy.maximum_attempts < policy.required_matches ||
        policy.maximum_attempts > maximum_verification_attempts || policy.time_budget.count() < 1 ||
        policy.overlap_frames > 75 ||
        policy.time_budget > std::chrono::seconds(60))
        throw std::invalid_argument(
            "repeated read policy requires 2..8 matches/attempts, a 1..60000ms budget, and at most 75 overlap frames");
}

std::unique_ptr<CddaReader> make_repeated_read_verifier(
    std::unique_ptr<CddaReader> reader, RepeatedReadPolicy policy, SteadyNow now) {
    return std::make_unique<RepeatedReadVerifier>(std::move(reader), policy, std::move(now));
}
