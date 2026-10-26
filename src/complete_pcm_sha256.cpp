#include "complete_pcm_sha256.hpp"
#include <charconv>
#include <cstring>
#include <stdexcept>
// The fixed SHA256_CTX makes per-job storage explicit, unlike opaque provider
// allocations. This uses OpenSSL's existing SHA-256, not a private algorithm.
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>

namespace {
constexpr std::size_t result_limit = 16 * 1024;

class Sha256Consumer final : public CompletePcmConsumer {
    struct Track {
        int number = 0;
        std::uint64_t bytes = 0;
        std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    };
    SHA256_CTX disc_{}, track_{};
    std::array<Track, 99> tracks_{};
    std::array<std::uint8_t, result_limit> output_{};
    std::uint64_t bytes_ = 0, track_bytes_ = 0;
    std::size_t tracks_count_ = 0, output_size_ = 0;
    int current_track_ = 0;
    bool finalized_ = false, discarded_ = false;

    void append(std::string_view text) {
        if (text.size() > output_.size() - output_size_)
            throw std::length_error("local fingerprint result limit");
        std::memcpy(output_.data() + output_size_, text.data(), text.size());
        output_size_ += text.size();
    }
    void number(std::uint64_t value) {
        char text[20];
        const auto result = std::to_chars(std::begin(text), std::end(text), value);
        if (result.ec != std::errc{}) throw std::length_error("local fingerprint number");
        append({text, static_cast<std::size_t>(result.ptr - text)});
    }
    void digest(const std::array<unsigned char, SHA256_DIGEST_LENGTH>& value) {
        constexpr char hex[] = "0123456789abcdef";
        for (auto byte : value) {
            const char pair[] = {hex[byte >> 4], hex[byte & 15]};
            append({pair, 2});
        }
    }
    void finish_track() {
        if (!current_track_) return;
        if (tracks_count_ == tracks_.size()) throw std::length_error("local fingerprint track limit");
        auto& track = tracks_[tracks_count_++];
        track.number = current_track_;
        track.bytes = track_bytes_;
        if (SHA256_Final(track.digest.data(), &track_) != 1)
            throw std::runtime_error("local fingerprint track finalize");
    }
public:
    Sha256Consumer() {
        if (SHA256_Init(&disc_) != 1) throw std::runtime_error("local fingerprint initialize");
    }
    const CompletePcmProfile& profile() const noexcept override { return complete_pcm_sha256_profile(); }
    void consume(int track, std::span<const std::uint8_t> bytes) override {
        if (discarded_ || finalized_) throw std::logic_error("local fingerprint is terminal");
        if (track < 1 || track > 99 || track < current_track_ || bytes.empty() ||
            bytes.size() % 4 != 0 || bytes.size() > complete_pcm_chunk_bytes)
            throw std::invalid_argument("invalid local fingerprint chunk");
        if (track != current_track_) {
            finish_track();
            current_track_ = track;
            track_bytes_ = 0;
            if (SHA256_Init(&track_) != 1) throw std::runtime_error("local fingerprint track initialize");
        }
        if (bytes.size() > std::numeric_limits<std::uint64_t>::max() - bytes_)
            throw std::overflow_error("local fingerprint byte count");
        if (SHA256_Update(&disc_, bytes.data(), bytes.size()) != 1 ||
            SHA256_Update(&track_, bytes.data(), bytes.size()) != 1)
            throw std::runtime_error("local fingerprint update");
        bytes_ += bytes.size();
        track_bytes_ += bytes.size();
    }
    std::span<const std::uint8_t> finalize() override {
        if (discarded_ || finalized_) throw std::logic_error("local fingerprint is terminal");
        finalized_ = true;
        finish_track();
        std::array<unsigned char, SHA256_DIGEST_LENGTH> value{};
        if (SHA256_Final(value.data(), &disc_) != 1)
            throw std::runtime_error("local fingerprint finalize");
        append("{\"kind\":\"local_pcm_fingerprint\",\"algorithm\":\"sha256\",\"version\":\"");
        append(profile().version);
        append("\",\"canonical\":\""); append(profile().canonical);
        append("\",\"external_verification\":\"NOT_CHECKED\",\"bytes\":"); number(bytes_);
        append(",\"sha256\":\""); digest(value); append("\",\"tracks\":[");
        for (std::size_t i = 0; i < tracks_count_; ++i) {
            if (i) append(",");
            append("{\"number\":"); number(tracks_[i].number);
            append(",\"bytes\":"); number(tracks_[i].bytes);
            append(",\"sha256\":\""); digest(tracks_[i].digest); append("\"}");
        }
        append("]}");
        return std::span(output_).first(output_size_);
    }
    void discard() noexcept override {
        discarded_ = true;
        disc_ = {}; track_ = {};
        tracks_.fill({}); output_.fill(0);
        bytes_ = track_bytes_ = 0;
        current_track_ = 0;
        tracks_count_ = output_size_ = 0;
    }
};
const CompletePcmProfile sha256_profile{std::string(complete_pcm_canonical_profile),
    "sha256", "local-pcm-sha256-v1", sizeof(Sha256Consumer), result_limit};
}

const CompletePcmProfile& complete_pcm_sha256_profile() noexcept {
    return sha256_profile;
}

std::unique_ptr<CompletePcmConsumer> make_complete_pcm_sha256_consumer() {
    return std::make_unique<Sha256Consumer>();
}
