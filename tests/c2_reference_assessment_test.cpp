#include "c2_reference_assessment.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("C2 assessment invariant failed"); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid C2 assessment input accepted");
}
// Controlled digest inputs to the evaluator, not hashes captured from a drive.
const std::string a(64, 'a'), b(64, 'b'), c(64, 'c');
C2Reference reference() { return {"test-ref", "synthetic independent test source", true, true, 0, {{10, a}, {20, b}}}; }
}

int main() {
    try {
        const auto valid = reference();
        for (unsigned field = 0; field < 10; ++field) {
            auto invalid = valid;
            switch (field) {
            case 0: invalid.id.clear(); break;
            case 1: invalid.id = "bad id"; break;
            case 2: invalid.provenance.clear(); break;
            case 3: invalid.provenance = "bad\nsource"; break;
            case 4: invalid.independent = false; break;
            case 5: invalid.alignment_verified = false; break;
            case 6: invalid.offset_samples.reset(); break;
            case 7: invalid.offset_samples = 1; break;
            case 8: invalid.sectors.clear(); break;
            case 9: invalid.sectors.push_back({30, c}); break;
            }
            rejects([&] { C2ReferenceAssessment test(invalid); });
        }
        for (const auto lba : {-1, 10, 9}) {
            auto invalid = valid;
            if (lba == -1) invalid.sectors[0].lba = lba;
            else invalid.sectors[1].lba = lba;
            rejects([&] { C2ReferenceAssessment test(invalid); });
        }
        for (const auto& hash : {std::string{}, std::string(63, 'a'), std::string(64, 'A'), std::string(64, 'g')}) {
            auto invalid = valid;
            invalid.sectors[0].sha256 = hash;
            rejects([&] { C2ReferenceAssessment test(invalid); });
        }
        C2ReferenceAssessment input(valid);
        rejects([&] { input.observe({30, true, C2Status::clean, a}); });
        rejects([&] { input.observe({10, true, C2Status::clean, "invalid"}); });
        check(input.counts().samples == 0);
        for (unsigned attempt = 0; attempt < 3; ++attempt) input.observe({10, true, C2Status::clean, a});
        rejects([&] { input.observe({10, true, C2Status::clean, a}); });
        check(input.counts().samples == 3 && input.outcome().value == Knowledge::unknown);
        for (unsigned test = 0; test < 6; ++test) {
            C2ReferenceAssessment evaluator(valid);
            for (unsigned attempt = 0; attempt < 3; ++attempt) {
                evaluator.observe({10, true, test == 3 ? C2Status::reported : C2Status::clean, a});
                const auto status = test == 1 || test == 3 || test == 4 ? C2Status::reported :
                    test == 5 ? C2Status::unknown : C2Status::clean;
                evaluator.observe({20, true, status, test == 1 || test == 2 || test == 3 ? c : b});
            }
            const auto result = evaluator.outcome();
            const auto expected = test == 1 ? Knowledge::yes : test == 2 ? Knowledge::no : Knowledge::unknown;
            check(result.value == expected);
            check(result.source == (expected == Knowledge::unknown ? CapabilityEvidenceSource::none : CapabilityEvidenceSource::tested));
            check(result.detail.find("conditional sector-clean-filter") != std::string::npos);
            check(evaluator.counts().samples == 6);
        }
        C2ReferenceAssessment incomplete(valid);
        for (unsigned i = 0; i < 3; ++i) {
            incomplete.observe({10, true, C2Status::clean, a});
            incomplete.observe({20, false, C2Status::clean, {}});
        }
        check(incomplete.counts().unavailable == 3 && incomplete.outcome().value == Knowledge::unknown);
        for (const auto status : {C2Status::not_checked, C2Status::not_available}) {
            C2ReferenceAssessment unavailable(valid);
            unavailable.observe({10, true, status, {}});
            check(unavailable.counts().unavailable == 1);
        }
        std::cout << "PASS: bounded reference admission and conditional C2 challenge outcomes\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
