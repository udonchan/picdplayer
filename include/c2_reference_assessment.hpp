#pragma once

#include "cdda_reader.hpp"
#include "drive_capabilities.hpp"
#include "disc_toc.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view c2_assessment_profile = "c2-sector-reference-v1";
inline constexpr unsigned c2_assessment_max_sectors = 2;
inline constexpr unsigned c2_assessment_attempts = 3;

struct C2ReferenceSector {
    std::int32_t lba = 0;
    std::string sha256; // Independent reference of one raw-LBA sector, canonical S16LE.
};
struct C2Reference {
    std::string id;
    std::string provenance; // Operator assertion, not independently authenticated by this evaluator.
    bool independent = false;
    bool alignment_verified = false;
    std::optional<int> offset_samples;
    std::vector<C2ReferenceSector> sectors;
    std::optional<DiscToc> expected_toc{}; // Server-owned context; never accepted from API reference input.
};
struct C2SectorSample {
    std::int32_t lba = 0;
    bool complete = false;
    C2Status c2 = C2Status::unknown;
    std::string sha256;
};
struct C2AssessmentCounts {
    unsigned samples = 0;
    unsigned unavailable = 0;
    unsigned clean_equal = 0;
    unsigned clean_different = 0;
    unsigned flagged_equal = 0;
    unsigned flagged_different = 0;
};

// Conditional sector-clean-filter assessment only. No pointer-byte accuracy,
// original-disc correctness, cache independence, or universal drive trust.
class C2ReferenceAssessment {
public:
    explicit C2ReferenceAssessment(C2Reference reference);
    void observe(const C2SectorSample& sample);
    CapabilityFlag outcome() const;
    const C2AssessmentCounts& counts() const noexcept { return counts_; }
private:
    C2Reference reference_;
    std::array<unsigned, c2_assessment_max_sectors> attempts_{};
    C2AssessmentCounts counts_;
};
