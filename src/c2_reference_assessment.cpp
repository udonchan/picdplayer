#include "c2_reference_assessment.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
bool digest_valid(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool id_valid(std::string_view value) {
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
}

C2ReferenceAssessment::C2ReferenceAssessment(C2Reference reference) : reference_(std::move(reference)) {
    if (!id_valid(reference_.id) || reference_.provenance.empty() || reference_.provenance.size() > 128 ||
        !std::all_of(reference_.provenance.begin(), reference_.provenance.end(), [](unsigned char c) { return c >= 32 && c <= 126; }) ||
        !reference_.independent || !reference_.alignment_verified || !reference_.offset_samples || *reference_.offset_samples != 0 ||
        reference_.sectors.empty() || reference_.sectors.size() > c2_assessment_max_sectors)
        throw std::invalid_argument("Independent aligned C2 reference required");
    std::optional<std::int32_t> previous;
    for (const auto& sector : reference_.sectors) {
        if (sector.lba < 0 || (previous && sector.lba <= *previous) || !digest_valid(sector.sha256))
            throw std::invalid_argument("Invalid C2 reference sector");
        previous = sector.lba;
    }
}

void C2ReferenceAssessment::observe(const C2SectorSample& sample) {
    std::size_t index = 0;
    while (index < reference_.sectors.size() && reference_.sectors[index].lba != sample.lba) ++index;
    if (index == reference_.sectors.size()) throw std::invalid_argument("C2 sample outside reference scope");
    if (attempts_[index] == c2_assessment_attempts) throw std::logic_error("C2 attempt budget exceeded");
    const bool known = sample.complete && (sample.c2 == C2Status::clean || sample.c2 == C2Status::reported);
    if (known && !digest_valid(sample.sha256)) throw std::invalid_argument("Invalid C2 sample digest");
    ++attempts_[index];
    ++counts_.samples;
    if (!known) { ++counts_.unavailable; return; }
    const bool equal = sample.sha256 == reference_.sectors[index].sha256;
    if (sample.c2 == C2Status::clean) {
        if (equal) ++counts_.clean_equal;
        else ++counts_.clean_different;
    } else {
        if (equal) ++counts_.flagged_equal;
        else ++counts_.flagged_different;
    }
}

CapabilityFlag C2ReferenceAssessment::outcome() const {
    const auto scoped = [&](Knowledge value, std::string_view reason) {
        return CapabilityFlag{value, value == Knowledge::unknown ? CapabilityEvidenceSource::none : CapabilityEvidenceSource::tested,
            std::string(c2_assessment_profile) + " ref=" + reference_.id + ": " + std::string(reason) +
            "; conditional sector-clean-filter against asserted independent/aligned operator reference; "
            "not universal drive/pointer-byte accuracy, cache independence or original-disc proof"};
    };
    for (std::size_t i = 0; i < reference_.sectors.size(); ++i)
        if (attempts_[i] != c2_assessment_attempts) return scoped(Knowledge::unknown, "incomplete assessment");
    if (counts_.unavailable) return scoped(Knowledge::unknown, "complete C2 evidence unavailable");
    if (!counts_.clean_equal) return scoped(Knowledge::unknown, "no clean reference control");
    if (counts_.clean_different) return scoped(Knowledge::no, "clean reply contradicted the supplied reference");
    if (counts_.flagged_different) return scoped(Knowledge::yes, "reference difference challenge was flagged in this scope");
    return scoped(Knowledge::unknown, "no challenge; clean equality alone is not proof");
}
