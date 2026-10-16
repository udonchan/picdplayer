#pragma once

#include "disc_toc.hpp"
#include "integrity_state.hpp"
#include <array>
#include <limits>

inline constexpr std::string_view complete_pcm_canonical_profile = "s16le-44100-stereo-v1";
inline constexpr std::size_t complete_pcm_memory_limit = 8 * 1024 * 1024;
inline constexpr std::size_t complete_pcm_result_limit = 64 * 1024;
inline constexpr std::size_t complete_pcm_consumer_limit = 1024 * 1024;
inline constexpr std::size_t complete_pcm_chunk_bytes = 16 * 1024;
inline constexpr auto complete_pcm_deadline_limit = std::chrono::hours(3);

struct CompletePcmProfile {
    std::string canonical;
    std::string algorithm;
    std::string version;
    std::size_t state_bytes = std::numeric_limits<std::size_t>::max();
    std::size_t maximum_result_bytes = 0;
    bool operator==(const CompletePcmProfile&) const = default;
};

struct CompletePcmIdentity {
    std::string session_id;
    std::uint64_t job_id = 0, disc_generation = 0, reader_incarnation = 0, policy_revision = 0;
    std::optional<std::int32_t> offset_samples;
    CompletePcmProfile profile;
    bool operator==(const CompletePcmIdentity&) const = default;
};

struct CompletePcmPlan {
    DiscToc toc;
    CompletePcmIdentity identity;
    ReadPolicy policy;
    std::chrono::milliseconds duration{0};
};

// A fresh, trusted, job-owned CPU consumer. Limits are declarations verified by
// the adapter's own tests, not a sandbox for arbitrary allocations or I/O.
class CompletePcmConsumer {
public:
    virtual ~CompletePcmConsumer() = default;
    virtual const CompletePcmProfile& profile() const noexcept = 0;
    virtual void consume(int track, std::span<const std::uint8_t> bytes) = 0;
    // The span remains owned by the consumer until discard/destruction.
    virtual std::span<const std::uint8_t> finalize() = 0;
    virtual void discard() noexcept = 0;
};

enum class CompletePcmState { collecting, prepared, complete, incomplete, invalid };
enum class CompletePcmFailure {
    none, cancelled, context_changed, clock_regressed, deadline,
    read_error, invalid_block, verification_failed, consumer_failed,
    result_limit, unfinished, toc_changed, reentrant_call
};

// Thread-confined pure input model; no reader, thread, transport or file I/O.
// "complete" is this model's receipt, not a production capture/MATCH result.
class CompletePcmInput {
public:
    CompletePcmInput(const CompletePcmPlan& plan, std::unique_ptr<CompletePcmConsumer> consumer,
                     SteadyNow now = [] { return std::chrono::steady_clock::now(); });
    ~CompletePcmInput();
    CompletePcmInput(const CompletePcmInput&) = delete;
    CompletePcmInput& operator=(const CompletePcmInput&) = delete;
    bool append(const CompletePcmIdentity& current, const ReadResult& read,
                std::span<const std::int16_t> pcm);
    bool prepare(const CompletePcmIdentity& current);
    bool confirm(const CompletePcmIdentity& current, const DiscToc& final_toc, bool reader_released);
    void cancel() noexcept;
    void invalidate() noexcept;
    std::chrono::milliseconds call_budget(const CompletePcmIdentity& current);
    CompletePcmState state() const { return state_; }
    CompletePcmFailure failure() const { return failure_; }
    const CompletePcmIdentity& identity() const { return identity_; }
    std::int32_t next_lba() const { return next_lba_; }
    std::uint64_t accepted_frames() const;
    std::uint64_t accepted_bytes() const { return accepted_frames() * 2352; }
    std::uint64_t track_frames(std::size_t index) const;
    std::size_t reserved_bytes() const { return reserved_bytes_; }
    const IntegrityStats& read_observations() const { return observations_; }
    std::uint64_t uncertain_reads() const { return uncertain_reads_; }
    std::uint64_t recovered_reads() const { return recovered_reads_; }
    std::span<const std::uint8_t> result() const;
private:
    bool check(const CompletePcmIdentity& current);
    void fail(CompletePcmState state, CompletePcmFailure reason) noexcept;
    void discard_if_terminal() noexcept;
    DiscToc toc_;
    CompletePcmIdentity identity_;
    ReadPolicy policy_;
    std::unique_ptr<CompletePcmConsumer> consumer_;
    SteadyNow now_;
    std::chrono::steady_clock::time_point last_now_, deadline_;
    CompletePcmState state_ = CompletePcmState::collecting;
    CompletePcmFailure failure_ = CompletePcmFailure::none;
    std::int32_t next_lba_ = 0;
    std::size_t reserved_bytes_ = 0, preceding_frames_ = 0;
    bool in_callback_ = false, discarded_ = false;
    std::array<std::uint8_t, complete_pcm_chunk_bytes> scratch_{};
    std::span<const std::uint8_t> result_;
    IntegrityStats observations_;
    std::uint64_t uncertain_reads_ = 0, recovered_reads_ = 0;
};
