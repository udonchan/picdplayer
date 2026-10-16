#include "complete_pcm_input.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0, cases = 0;
void check(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("complete PCM assertion failed");
}
void test(const char* name, const std::function<void()>& function) {
    try { function(); ++cases; }
    catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}

CompletePcmPlan plan() {
    CompletePcmPlan p;
    p.toc = make_audio_toc(7, std::vector<std::int32_t>{150, 152, 169}, 185);
    p.identity = {"test-session", 3, 2, 1, 0, 0,
        {std::string(complete_pcm_canonical_profile), "test-byte-recorder", "1", complete_pcm_consumer_limit, 4}};
    p.duration = std::chrono::seconds(10);
    return p;
}

struct Probe {
    CompletePcmProfile profile;
    struct Chunk { int track; std::size_t offset, size; };
    std::vector<Chunk> chunks;
    std::vector<std::uint8_t> bytes, output;
    unsigned consumed = 0, finalized = 0, discarded = 0;
    std::size_t output_size = 4;
    bool throw_consume = false, throw_finalize = false;
    std::function<void()> on_consume, on_finalize;
};
class Consumer final : public CompletePcmConsumer {
    std::shared_ptr<Probe> p_;
public:
    explicit Consumer(std::shared_ptr<Probe> p) : p_(std::move(p)) {}
    const CompletePcmProfile& profile() const noexcept override { return p_->profile; }
    void consume(int track, std::span<const std::uint8_t> bytes) override {
        ++p_->consumed;
        p_->chunks.push_back({track, p_->bytes.size(), bytes.size()});
        p_->bytes.insert(p_->bytes.end(), bytes.begin(), bytes.end());
        if (p_->on_consume) p_->on_consume();
        if (p_->throw_consume) throw std::bad_alloc();
    }
    std::span<const std::uint8_t> finalize() override {
        ++p_->finalized;
        if (p_->on_finalize) p_->on_finalize();
        if (p_->throw_finalize) throw std::runtime_error("fake consumer failure");
        p_->output.assign(p_->output_size, 42);
        return p_->output;
    }
    void discard() noexcept override {
        ++p_->discarded;
        p_->bytes.clear(); p_->output.clear();
    }
};

std::vector<std::int16_t> pcm(std::int32_t start, std::size_t frames) {
    std::vector<std::int16_t> values(frames * cdda_samples_per_frame);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto position = std::int64_t(start) * 588 + static_cast<std::int64_t>(i / 2);
        values[i] = static_cast<std::int16_t>((position * 23 + (i % 2) * 1009) % 65536 - 32768);
    }
    return values;
}
ReadResult read(const CompletePcmPlan& p, std::int32_t start, std::size_t frames) {
    ReadResult r{start, frames, frames, ReadStatus::ok, 0, 0};
    r.offset = OffsetReadEvidence{0, PhysicalReadEvidence{start, frames, start, frames, 0}};
    if (p.policy.mode == ReadVerificationMode::repeat) {
        r.offset->single_physical_read.reset();
        auto& v = r.verification;
        v.attempts = v.complete_reads = v.matching_reads = v.detail_count = 2;
        v.accepted_attempt = 2; v.accepted_candidate = 1;
        const unsigned overlap = start == p.toc.tracks.front().start_lba ? 0 : 15;
        v.overlap_frames_requested = 15;
        v.overlap_frames_compared = overlap;
        v.overlap = overlap ? OverlapVerification::matched : OverlapVerification::stream_boundary;
        for (unsigned i = 0; i < 2; ++i) {
            auto& d = v.details[i];
            d.physical_start_lba = d.observed_start_lba = d.logical_start_lba = start - overlap;
            d.physical_frames_requested = d.logical_frames_requested = d.frames_read = frames + overlap;
            d.complete = true; d.candidate = 1;
        }
    }
    return r;
}

class SampleReader final : public CddaReader {
    std::int32_t position_ = 0;
public:
    void seek(std::int32_t position) override { position_ = position; }
    ReadResult read(std::span<std::int16_t> output) override {
        const auto frames = output.size() / cdda_samples_per_frame;
        const auto values = pcm(position_, frames);
        std::copy(values.begin(), values.end(), output.begin());
        ReadResult result{position_, frames, frames, ReadStatus::ok, 0, 0};
        result.offset = OffsetReadEvidence{0, PhysicalReadEvidence{position_,frames,position_,frames,0}};
        position_ += static_cast<std::int32_t>(frames);
        return result;
    }
};

struct Rig {
    CompletePcmPlan p;
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    std::chrono::steady_clock::time_point now{std::chrono::seconds(1)};
    std::unique_ptr<CompletePcmInput> core;
    explicit Rig(CompletePcmPlan input = plan()) : p(std::move(input)) {
        probe->profile = p.identity.profile;
        core = std::make_unique<CompletePcmInput>(p, std::make_unique<Consumer>(probe), [this] { return now; });
    }
    bool append(std::int32_t start, std::size_t frames) {
        const auto values = pcm(start, frames);
        return core->append(p.identity, read(p, start, frames), values);
    }
    void all() {
        while (core->next_lba() < p.toc.leadout_lba) {
            const auto n = std::min<std::size_t>(15, p.toc.leadout_lba - core->next_lba());
            check(append(core->next_lba(), n));
        }
    }
    void complete() {
        all(); check(core->prepare(p.identity)); check(core->confirm(p.identity, p.toc, true));
    }
};

void invalid(const CompletePcmPlan& p) {
    bool rejected = false;
    try { Rig r(p); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
}
}

int main() {
    try {
        test("all samples, track boundaries, no early receipt", [] {
            Rig r;
            r.all();
            check(r.core->accepted_frames() == 35 && r.core->accepted_bytes() == 35 * 2352);
            check(r.core->track_frames(0) == 2 && r.core->track_frames(1) == 17 && r.core->track_frames(2) == 16);
            const auto values = pcm(150, 35);
            check(r.probe->bytes.size() == values.size() * 2);
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto value = static_cast<std::uint16_t>(values[i]);
                check(r.probe->bytes[2*i] == (value & 255));
                check(r.probe->bytes[2*i+1] == (value >> 8));
            }
            for (const auto& c : r.probe->chunks) {
                check(c.size > 0 && c.size <= complete_pcm_chunk_bytes && c.size % 4 == 0);
                const auto position = 150 + c.offset / 2352;
                check(c.track == (position < 152 ? 7 : position < 169 ? 8 : 9));
            }
            check(r.core->result().empty());
            check(r.core->prepare(r.p.identity) && r.core->state() == CompletePcmState::prepared);
            check(!r.core->confirm(r.p.identity, r.p.toc, false) && r.core->result().empty());
            check(r.core->confirm(r.p.identity, r.p.toc, true));
            check(r.core->state() == CompletePcmState::complete && r.core->result().size() == 4);
            r.core->cancel(); check(r.core->state() == CompletePcmState::complete);
            r.core->invalidate(); check(r.core->state() == CompletePcmState::invalid && r.core->result().empty());
            check(r.probe->discarded == 1);
        });
        test("explicit endian extremes", [] {
            Rig r; auto values = pcm(150, 1);
            values[0] = 0x1234; values[1] = -2; values[2] = -32768; values[3] = 32767;
            check(r.core->append(r.p.identity, read(r.p, 150, 1), values));
            const std::array<std::uint8_t, 8> expected{0x34,0x12,0xfe,0xff,0,0x80,0xff,0x7f};
            check(std::equal(expected.begin(), expected.end(), r.probe->bytes.begin()));
        });
        test("coverage is not the 128-read window", [] {
            auto p = plan(); p.toc = make_audio_toc(3, std::vector<std::int32_t>{0}, 150);
            Rig r(p);
            for (int i = 0; i < 150; ++i) check(r.append(i, 1));
            check(r.core->accepted_frames() == 150 && r.core->track_frames(0) == 150);
            check(r.core->prepare(p.identity) && r.core->confirm(p.identity, p.toc, true));
        });
        test("repeat normalized output excludes overlap", [] {
            auto p = plan(); p.policy.mode = ReadVerificationMode::repeat; p.policy.region_frames = 15;
            Rig r(p); r.complete();
            check(r.probe->bytes.size() == 35 * 2352 && r.core->read_observations().verification_attempts == 6);
        });
        test("actual repeat wrapper over fake PCM", [] {
            auto p = plan(); p.policy.mode = ReadVerificationMode::repeat; p.policy.region_frames = 15;
            Rig r(p);
            auto reader = make_repeated_read_verifier(std::make_unique<SampleReader>());
            reader->seek(150);
            unsigned calls = 0;
            while (r.core->next_lba() < 185) {
                const auto frames = calls == 0 ? 1 : std::min(15,185-r.core->next_lba());
                std::vector<std::int16_t> values(frames * cdda_samples_per_frame);
                const auto result = reader->read(values);
                ++calls;
                if (!r.core->append(p.identity,result,values)) {
                    throw std::runtime_error("append failure=" + std::to_string(static_cast<int>(r.core->failure()))
                        + " lba=" + std::to_string(result.start_lba) + " attempts=" + std::to_string(result.verification.attempts)
                        + " overlap=" + std::to_string(result.verification.overlap_frames_requested)
                        + "/" + std::to_string(result.verification.overlap_frames_compared));
                }
                check(r.core->next_lba() == result.start_lba + static_cast<std::int32_t>(result.frames_read));
            }
            check(r.core->prepare(p.identity) && r.core->confirm(p.identity,p.toc,true));
            check(r.core->accepted_frames() == 35 && r.probe->bytes.size() == 35*2352);
            check(r.core->read_observations().verification_attempts == 2*calls);
        });
        test("nonzero LBA near int32 maximum", [] {
            auto p = plan(); p.toc = make_audio_toc(99, std::vector<std::int32_t>{2147483642}, 2147483647);
            Rig r(p); r.complete(); check(r.core->accepted_bytes() == 5 * 2352);
        });
        test("single policy rejects repeat producer evidence", [] {
            Rig r;
            RepeatedReadPolicy repeated; repeated.overlap_frames = 0;
            auto reader = make_repeated_read_verifier(std::make_unique<SampleReader>(),repeated);
            reader->seek(150);
            auto values = pcm(150,15);
            const auto result = reader->read(values);
            check(result.status == ReadStatus::ok && result.verification.attempts == 2);
            check(!r.core->append(r.p.identity,result,values));
            check(r.core->failure() == CompletePcmFailure::verification_failed && r.probe->consumed == 0);
        });
        for (unsigned condition = 0; condition < 20; ++condition) test("preflight rejection", [=] {
            auto p = plan();
            switch (condition) {
            case 0: p.identity.offset_samples.reset(); break;
            case 1: p.identity.offset_samples = 1; break;
            case 2: p.identity.offset_samples = -1; break;
            case 3: p.identity.profile.canonical = "unknown"; break;
            case 4: p.identity.profile.algorithm.clear(); break;
            case 5: p.identity.profile.version.clear(); break;
            case 6: p.identity.profile.state_bytes = complete_pcm_consumer_limit + 1; break;
            case 7: p.identity.profile.maximum_result_bytes = complete_pcm_result_limit + 1; break;
            case 8: p.identity.profile.maximum_result_bytes = 0; break;
            case 9: p.duration = std::chrono::milliseconds(0); break;
            case 10: p.duration = std::chrono::hours(4); break;
            case 11: p.identity.session_id.assign(129, 'x'); break;
            case 12: p.identity.job_id = 0; break;
            case 13: p.identity.disc_generation = 0; break;
            case 14: p.identity.reader_incarnation = 0; break;
            case 15: p.policy.region_frames = 90; break;
            case 16: p.policy.maximum_attempts = 9; break;
            case 17: p.policy.time_budget_ms = 0; break;
            case 18: ++p.toc.tracks[0].length_frames; break;
            case 19: p.policy.mode = static_cast<ReadVerificationMode>(99); break;
            }
            invalid(p);
        });
        test("budget reservation", [] {
            auto p = plan(); p.policy.maximum_attempts = 8;
            Rig r(p); check(r.core->reserved_bytes() == 4747552);
            check(r.core->reserved_bytes() < complete_pcm_memory_limit);
        });
        test("TOC shape and absent clock preflight", [] {
            auto p = plan(); p.toc.tracks.clear(); invalid(p);
            p = plan(); ++p.toc.tracks[1].number; invalid(p);
            p = plan(); p.toc.leadout_lba = p.toc.tracks.back().start_lba; invalid(p);
            p = plan(); auto probe = std::make_shared<Probe>(); probe->profile = p.identity.profile;
            bool rejected = false;
            try { CompletePcmInput input(p,std::make_unique<Consumer>(probe),{}); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected);
        });
        test("null consumer and mismatched declared profile", [] {
            auto p = plan(); bool rejected = false;
            try { CompletePcmInput input(p,nullptr); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected);
            auto probe = std::make_shared<Probe>(); probe->profile = p.identity.profile; probe->profile.version = "2";
            rejected = false;
            try { CompletePcmInput input(p,std::make_unique<Consumer>(probe)); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected);
            p.identity.profile.state_bytes = std::numeric_limits<std::size_t>::max(); invalid(p);
        });
        for (unsigned condition = 0; condition < 10; ++condition) test("bad block leaves prefix unchanged", [=] {
            Rig r; auto result = read(r.p, 150, 15); auto values = pcm(150, 15);
            switch (condition) {
            case 0: ++result.start_lba; break;
            case 1: --result.start_lba; break;
            case 2: --result.frames_read; break;
            case 3: result.status = ReadStatus::read_error; break;
            case 4: result.frames_read = result.frames_requested = 0; result.offset->single_physical_read = PhysicalReadEvidence{150,0,150,0,0}; values.clear(); break;
            case 5: result.frames_requested = result.frames_read = 16; result.offset->single_physical_read = PhysicalReadEvidence{150,16,150,16,0}; values = pcm(150, 16); break;
            case 6: values.pop_back(); break;
            case 7: result.offset.reset(); break;
            case 8: result.offset->samples = 1; break;
            case 9: result.frames_requested = std::numeric_limits<std::size_t>::max(); break;
            }
            check(!r.core->append(r.p.identity, result, values));
            check(r.core->accepted_frames() == 0 && r.probe->consumed == 0 && r.probe->discarded == 1);
            check(r.core->state() == CompletePcmState::incomplete && !r.append(150,15));
            check(!r.core->prepare(r.p.identity) && r.core->result().empty());
        });
        test("duplicate, gap and premature finalization", [] {
            Rig duplicate; check(duplicate.append(150, 1)); check(!duplicate.append(150, 1));
            check(duplicate.core->accepted_frames() == 1);
            Rig gap; check(gap.append(150, 1)); check(!gap.append(152, 1));
            Rig partial; check(partial.append(150, 1)); check(!partial.core->prepare(partial.p.identity));
            check(partial.core->failure() == CompletePcmFailure::unfinished && partial.probe->finalized == 0);
        });
        test("extra PCM before confirmation invalidates prepared input", [] {
            Rig r; r.all(); check(r.core->prepare(r.p.identity));
            check(!r.append(180,5) && r.core->state() == CompletePcmState::incomplete);
            check(!r.core->confirm(r.p.identity,r.p.toc,true) && r.core->result().empty());
        });
        for (unsigned condition = 0; condition < 7; ++condition) test("reject invalid repeat evidence", [=] {
            auto p = plan(); p.policy.mode = ReadVerificationMode::repeat;
            Rig r(p); auto result = read(p,150,15); const auto values = pcm(150,15);
            switch (condition) {
            case 0: result.verification.matching_reads = 1; break;
            case 1: result.verification.accepted_attempt.reset(); break;
            case 2: result.verification.accepted_candidate = 0; break;
            case 3: result.verification.time_budget_exhausted = true; break;
            case 4: result.verification.overlap = OverlapVerification::mismatched; break;
            case 5: result.verification.overlap = OverlapVerification::not_checked; break;
            case 6: result.verification.detail_count = 0; break;
            }
            check(!r.core->append(p.identity,result,values) && r.probe->consumed == 0);
            check(r.core->failure() == CompletePcmFailure::verification_failed);
        });
        test("UNCERTAIN and RECOVERED are not adoption failures", [] {
            Rig r; auto result = read(r.p,150,15); auto values = pcm(150,15);
            result.retries = 1;
            check(r.core->append(r.p.identity,result,values) && r.core->uncertain_reads() == 1);
            result = read(r.p,165,15); values = pcm(165,15); result.paranoia.fixups = 1;
            check(r.core->append(r.p.identity,result,values) && r.core->recovered_reads() == 1);
            check(r.append(180,5)); check(r.core->prepare(r.p.identity));
            check(r.core->confirm(r.p.identity,r.p.toc,true));
            check(r.core->read_observations().direct_retries == 1 && r.core->read_observations().backend_fixups == 1);
        });
        for (unsigned field = 0; field < 8; ++field) test("context invalidation", [=] {
            Rig r; auto changed = r.p.identity;
            switch (field) {
            case 0: changed.session_id = "next-session"; break;
            case 1: ++changed.job_id; break;
            case 2: ++changed.disc_generation; break;
            case 3: ++changed.reader_incarnation; break;
            case 4: ++changed.policy_revision; break;
            case 5: changed.profile.version = "2"; break;
            case 6: changed.offset_samples = 1; break;
            case 7: changed.profile.algorithm = "other"; break;
            }
            check(!r.core->append(changed,read(r.p,150,15),pcm(150,15)));
            check(r.core->state() == CompletePcmState::invalid && r.probe->consumed == 0);
        });
        test("consumer profile mutation", [] {
            Rig r; r.probe->profile.version = "other";
            check(!r.append(150,15) && r.core->state() == CompletePcmState::invalid);
        });
        test("consumer errors discard partial state", [] {
            Rig r; r.probe->throw_consume = true;
            check(!r.append(150,15) && r.core->accepted_bytes() == 0);
            check(r.core->failure() == CompletePcmFailure::consumer_failed && r.probe->discarded == 1);
            Rig final; final.all(); final.probe->throw_finalize = true;
            check(!final.core->prepare(final.p.identity) && final.core->result().empty());
        });
        test("cancel inside consume does not discard an active callback", [] {
            Rig r; r.probe->on_consume = [&] { r.core->cancel(); check(r.probe->discarded == 0); };
            check(!r.append(150,15)); check(r.core->accepted_frames() == 0 && r.probe->discarded == 1);
        });
        test("cancel/finalize race and late confirmation", [] {
            Rig r; r.all(); r.probe->on_finalize = [&] { r.core->cancel(); };
            check(!r.core->prepare(r.p.identity) && r.core->result().empty());
            check(!r.core->confirm(r.p.identity,r.p.toc,true));
            Rig ready; ready.all(); check(ready.core->prepare(ready.p.identity)); ready.core->cancel();
            check(!ready.core->confirm(ready.p.identity,ready.p.toc,true));
        });
        test("context loss is not overwritten by consumer exception", [] {
            Rig r; r.probe->on_consume = [&] { r.core->invalidate(); }; r.probe->throw_consume = true;
            check(!r.append(150,15) && r.core->state() == CompletePcmState::invalid);
        });
        test("reentrant append cannot complete input", [] {
            Rig r; r.probe->on_consume = [&] { check(!r.append(150,1)); };
            check(!r.append(150,15) && r.core->failure() == CompletePcmFailure::reentrant_call);
        });
        test("deadline and call budget", [] {
            auto p = plan(); p.duration = std::chrono::milliseconds(100);
            Rig r(p); check(r.core->call_budget(p.identity).count() == 100);
            r.now += std::chrono::milliseconds(45); check(r.core->call_budget(p.identity).count() == 55);
            r.now += std::chrono::milliseconds(55); check(r.core->call_budget(p.identity).count() == 0);
            check(r.core->failure() == CompletePcmFailure::deadline && !r.append(150,15));
            Rig read_budget; check(read_budget.core->call_budget(read_budget.p.identity).count() == 10000);
        });
        test("deadline during consumer and before commit", [] {
            Rig r; r.probe->on_consume = [&] { r.now += std::chrono::seconds(10); };
            check(!r.append(150,15) && r.core->failure() == CompletePcmFailure::deadline);
            Rig pending; pending.all(); check(pending.core->prepare(pending.p.identity));
            pending.now += std::chrono::seconds(10);
            check(!pending.core->confirm(pending.p.identity,pending.p.toc,true));
        });
        test("read budget never rounds beyond remaining job time", [] {
            auto p = plan(); p.duration = std::chrono::milliseconds(100);
            Rig r(p); r.now += std::chrono::microseconds(99999);
            check(r.core->call_budget(p.identity).count() == 0);
            check(r.core->state() == CompletePcmState::collecting);
            r.now += std::chrono::microseconds(1);
            check(r.core->call_budget(p.identity).count() == 0 && r.core->failure() == CompletePcmFailure::deadline);
        });
        test("consumer finalize cannot change epochs or exceed deadline", [] {
            Rig epoch; epoch.all(); epoch.probe->on_finalize = [&] { ++epoch.p.identity.disc_generation; };
            check(!epoch.core->prepare(epoch.p.identity) && epoch.core->state() == CompletePcmState::invalid);
            Rig expired; expired.all(); expired.probe->on_finalize = [&] { expired.now += std::chrono::seconds(10); };
            check(!expired.core->prepare(expired.p.identity) && expired.core->failure() == CompletePcmFailure::deadline);
        });
        test("clock regression and deadline overflow", [] {
            Rig r; r.now -= std::chrono::nanoseconds(1);
            check(!r.append(150,15) && r.core->failure() == CompletePcmFailure::clock_regressed);
            auto p = plan(); auto probe = std::make_shared<Probe>(); probe->profile = p.identity.profile;
            bool rejected = false;
            try { CompletePcmInput input(p,std::make_unique<Consumer>(probe),[] {return std::chrono::steady_clock::time_point::max();}); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected);
        });
        test("empty and oversized consumer result", [] {
            for (auto size : {std::size_t(0), std::size_t(5)}) {
                Rig r; r.all(); r.probe->output_size = size;
                check(!r.core->prepare(r.p.identity) && r.core->failure() == CompletePcmFailure::result_limit);
            }
        });
        test("final TOC and late epochs", [] {
            Rig r; r.all(); check(r.core->prepare(r.p.identity)); auto changed = r.p.toc; ++changed.leadout_lba;
            check(!r.core->confirm(r.p.identity,changed,true) && r.core->state() == CompletePcmState::invalid);
            Rig old; old.complete(); auto next = old.p.identity; ++next.job_id;
            check(!old.core->confirm(next,old.p.toc,true) && old.core->result().empty());
        });
        std::cout << "PASS: " << cases << " complete PCM core cases; " << checks << " checks (no drive/provider I/O)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
