#include "integrity_state.hpp"
#include <iostream>
#include <stdexcept>

namespace { void check(bool value) { if (!value) throw std::runtime_error("integrity state test failed"); } }

int main() {
    try {
        ReadResult result{150, 15, 15, ReadStatus::ok, 0, 0};
        result.paranoia.reads = 4;
        result.paranoia.verifies = 1;
        const auto evidence = make_read_evidence(result);
        check(evidence.start_lba == 150 && evidence.frames_read == 15);
        check(evidence.status == IntegrityReadStatus::clean);
        check(evidence.local_verification == LocalVerification::backend_reported);
        check(evidence.c2_status == C2Status::not_checked);
        check(evidence.offset_status == OffsetStatus::unknown);

        IntegrityStats stats;
        observe_read(stats, result);
        check(stats.read_calls == 1 && stats.frames_accepted == 15);
        check(stats.direct_retries == 0 && stats.backend_reads == 4 && stats.backend_verifies == 1);

        ReadResult retried{165, 15, 15, ReadStatus::ok, 0, 2};
        check(make_read_evidence(retried).status == IntegrityReadStatus::uncertain);
        observe_read(stats, retried);
        check(stats.direct_retries == 2);

        ReadResult fixed{180, 15, 15, ReadStatus::ok, 0, 0};
        fixed.paranoia.fixups = 1;
        check(make_read_evidence(fixed).status == IntegrityReadStatus::recovered);

        result.status = ReadStatus::read_error;
        result.frames_read = 7;
        result.paranoia = {};
        const auto failed = make_read_evidence(result);
        check(failed.status == IntegrityReadStatus::uncertain);
        check(failed.local_verification == LocalVerification::single_read);
        observe_read(stats, result);
        check(stats.failed_calls == 1 && stats.frames_accepted == 30);

        ReadResult verified{200, 15, 15, ReadStatus::ok, 0, 0};
        verified.verification = {3, 3, 2, 1, false};
        const auto recovered = make_read_evidence(verified);
        check(recovered.status == IntegrityReadStatus::recovered);
        check(recovered.local_verification == LocalVerification::multiple_match);
        check(recovered.verification.attempts == 3);
        observe_read(stats, verified);
        check(stats.verification_attempts == 3 && stats.verification_mismatches == 1);
        check(stats.verified_calls == 1 && stats.verification_failures == 0);

        ReadResult unresolved{215, 15, 0, ReadStatus::read_error, EIO, 0};
        unresolved.verification = {3, 3, 1, 2, false};
        check(make_read_evidence(unresolved).status == IntegrityReadStatus::uncertain);
        observe_read(stats, unresolved);
        check(stats.verification_failures == 1);
        ReadCoverage coverage;
        coverage.observe(ReadResult{100, 20, 20, ReadStatus::ok, 0, 0});
        coverage.observe(ReadResult{110, 20, 20, ReadStatus::ok, 0, 0});
        coverage.observe(ReadResult{100, 20, 20, ReadStatus::ok, 0, 3});
        check(coverage.accepted_unique_frames == 30 && coverage.size == 1);
        coverage.observe(ReadResult{90, 5, 5, ReadStatus::ok, 0, 0});
        coverage.observe(ReadResult{95, 5, 5, ReadStatus::ok, 0, 0});
        check(coverage.accepted_unique_frames == 40 && coverage.size == 1);
        coverage.observe(ReadResult{200, 15, 7, ReadStatus::read_error, EIO, 0});
        check(coverage.accepted_unique_frames == 40);
        ReadCoverage full;
        for (unsigned i = 0; i < ReadCoverage::capacity; ++i)
            full.observe(ReadResult{static_cast<int>(i * 2), 1, 1, ReadStatus::ok, 0, 0});
        check(full.complete && full.accepted_unique_frames == 128);
        full.observe(ReadResult{1000, 1, 1, ReadStatus::ok, 0, 0});
        check(!full.complete && full.accepted_unique_frames == 128);
        full.observe(ReadResult{0, 1500, 1500, ReadStatus::ok, 0, 0});
        check(full.accepted_unique_frames == 128);

        std::cout << "PASS: truthful read evidence and aggregate counters\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
