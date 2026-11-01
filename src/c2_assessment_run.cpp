#include "c2_assessment_run.hpp"
#include <array>
#include <bit>
#include <stdexcept>
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>

namespace {
std::string sector_digest(std::span<const std::int16_t> pcm) {
    std::array<unsigned char, 2352> bytes{};
    if (pcm.size() != cdda_samples_per_frame) throw std::invalid_argument("C2 sample must contain one sector");
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const auto word = std::bit_cast<std::uint16_t>(pcm[i]);
        bytes[2 * i] = static_cast<unsigned char>(word);
        bytes[2 * i + 1] = static_cast<unsigned char>(word >> 8);
    }
    SHA256_CTX context{};
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    if (SHA256_Init(&context) != 1 || SHA256_Update(&context, bytes.data(), bytes.size()) != 1 ||
        SHA256_Final(digest.data(), &context) != 1) throw std::runtime_error("C2 sector digest failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
}

C2AssessmentRunResult run_c2_assessment_raw(const C2Reference& reference,
    const C2AssessmentReaderFactory& factory, const std::function<bool()>& current, SteadyNow now,
    std::optional<std::chrono::steady_clock::time_point> supplied_deadline) {
    C2AssessmentRunResult result;
    std::unique_ptr<CddaReader> reader;
    try {
        C2ReferenceAssessment evaluator(reference); // Validate before open.
        if (!factory || !current || !now) throw std::invalid_argument("C2 assessment callback missing");
        const auto deadline = supplied_deadline ? *supplied_deadline : now() + std::chrono::milliseconds(4000);
        const auto allowed = [&] {
            if (!current()) { result.state = C2AssessmentRunState::cancelled; return false; }
            if (now() >= deadline) { result.state = C2AssessmentRunState::timed_out; return false; }
            return true;
        };
        if (!allowed()) return result;
        reader = factory();
        if (!reader) throw std::runtime_error("C2 reader unavailable");
        std::array<std::int16_t, cdda_samples_per_frame> pcm{};
        bool proceed = true;
        for (const auto& sector : reference.sectors) {
            for (unsigned attempt = 0; attempt < c2_assessment_attempts; ++attempt) {
                if (!allowed()) { proceed = false; break; }
                reader->seek(sector.lba);
                if (!allowed()) { proceed = false; break; }
                const auto read = reader->read(pcm);
                if (!allowed()) { proceed = false; break; }
                if (read.status != ReadStatus::ok || read.start_lba != sector.lba || read.frames_requested != 1 ||
                    read.frames_read != 1 || (read.c2_status != C2Status::clean && read.c2_status != C2Status::reported))
                    throw std::runtime_error("Complete direct C2 sector evidence unavailable");
                evaluator.observe({sector.lba, true, read.c2_status, sector_digest(pcm)});
                result.counts = evaluator.counts();
            }
            if (!proceed) break;
        }
        result.counts = evaluator.counts();
        reader.reset(); // Never hand an assessment back while retaining the reader.
        if (proceed && allowed()) {
            result.assessment = evaluator.outcome();
            result.state = C2AssessmentRunState::complete;
        }
    } catch (const std::exception& error) {
        result.state = C2AssessmentRunState::failed;
        result.error = error.what();
    }
    reader.reset();
    return result;
}
