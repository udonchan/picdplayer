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
        reader_->seek(lba);
    }

    ReadResult read(std::span<std::int16_t> pcm) override {
        if (!positioned_) throw std::logic_error("CDDA seek required before read");
        if (pcm.empty() || pcm.size() % cdda_samples_per_frame)
            throw std::invalid_argument("PCM buffer must contain whole CD frames");
        const auto frames = pcm.size() / cdda_samples_per_frame;
        if (frames > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - position_))
            throw std::invalid_argument("CDDA LBA overflow");

        ReadResult combined{position_, frames, 0, ReadStatus::read_error, EIO, 0};
        std::vector<Candidate> candidates;
        const auto started = now_();
        for (unsigned attempt = 0; attempt < policy_.maximum_attempts; ++attempt) {
            if (attempt && now_() - started >= policy_.time_budget) {
                combined.verification.time_budget_exhausted = true;
                break;
            }
            reader_->seek(position_);
            std::vector<std::int16_t> sample(pcm.size());
            const auto result = reader_->read(sample);
            ++combined.verification.attempts;
            auto& detail = combined.verification.details[combined.verification.detail_count++];
            detail.frames_read = result.frames_read;
            detail.complete = result.status == ReadStatus::ok && result.frames_read == frames;
            detail.native_error = result.native_error;
            detail.direct_retries = result.retries;
            combined.retries += result.retries;
            add(combined.paranoia, result.paranoia);
            if (result.native_error) combined.native_error = result.native_error;
            if (result.status != ReadStatus::ok || result.frames_read != frames) continue;

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
                std::copy(found->pcm.begin(), found->pcm.end(), pcm.begin());
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
};
}

void validate_repeated_read_policy(const RepeatedReadPolicy& policy) {
    if (policy.required_matches < 2 || policy.maximum_attempts < policy.required_matches ||
        policy.maximum_attempts > maximum_verification_attempts || policy.time_budget.count() < 1 ||
        policy.time_budget > std::chrono::seconds(60))
        throw std::invalid_argument(
            "repeated read policy requires 2..8 matches/attempts and a 1..60000ms budget");
}

std::unique_ptr<CddaReader> make_repeated_read_verifier(
    std::unique_ptr<CddaReader> reader, RepeatedReadPolicy policy, SteadyNow now) {
    return std::make_unique<RepeatedReadVerifier>(std::move(reader), policy, std::move(now));
}
