#include "complete_pcm_worker.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
struct Stopped {};
class GuardedReader final : public CddaReader {
    std::unique_ptr<CddaReader> reader_;
    std::function<bool()> allowed_;
public:
    GuardedReader(std::unique_ptr<CddaReader> reader, std::function<bool()> allowed)
        : reader_(std::move(reader)), allowed_(std::move(allowed)) {}
    void seek(std::int32_t position) override {
        if (!allowed_()) throw Stopped{};
        reader_->seek(position);
        if (!allowed_()) throw Stopped{};
    }
    ReadResult read(std::span<std::int16_t> output) override {
        if (!allowed_()) throw Stopped{};
        const auto result = reader_->read(output);
        if (!allowed_()) throw Stopped{};
        return result;
    }
};
bool same_toc(const DiscToc& a, const DiscToc& b) {
    if (a.leadout_lba != b.leadout_lba || a.tracks.size() != b.tracks.size()) return false;
    for (std::size_t i = 0; i < a.tracks.size(); ++i)
        if (a.tracks[i].number != b.tracks[i].number || a.tracks[i].start_lba != b.tracks[i].start_lba ||
            a.tracks[i].length_frames != b.tracks[i].length_frames) return false;
    return true;
}

class GuardedConsumer final : public CompletePcmConsumer {
    std::unique_ptr<CompletePcmConsumer> consumer_;
    std::function<bool()> allowed_;
public:
    GuardedConsumer(std::unique_ptr<CompletePcmConsumer> consumer, std::function<bool()> allowed)
        : consumer_(std::move(consumer)), allowed_(std::move(allowed)) {}
    const CompletePcmProfile& profile() const noexcept override { return consumer_->profile(); }
    void consume(int track, std::span<const std::uint8_t> bytes) override {
        if (!allowed_()) throw Stopped{};
        consumer_->consume(track, bytes);
        if (!allowed_()) throw Stopped{};
    }
    std::span<const std::uint8_t> finalize() override {
        if (!allowed_()) throw Stopped{};
        const auto bytes = consumer_->finalize();
        if (!allowed_()) throw Stopped{};
        return bytes;
    }
    void discard() noexcept override { consumer_->discard(); }
};
}

CompletePcmWorker::CompletePcmWorker(Factory factory, TocProbe probe,
    std::shared_ptr<DriveAccessCoordinator> access, SteadyNow now)
    : factory_(std::move(factory)), probe_(std::move(probe)), access_(std::move(access)), now_(std::move(now)) {
    if (!factory_ || !probe_ || !access_ || !now_) throw std::invalid_argument("incomplete PCM worker dependencies");
    thread_ = std::thread(&CompletePcmWorker::run, this);
}

CompletePcmWorker::~CompletePcmWorker() {
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
        ++epoch_;
        stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::cancelled);
    }
    changed_.notify_all();
    thread_.join();
}

bool CompletePcmWorker::start(const CompletePcmPlan& plan, std::unique_ptr<CompletePcmConsumer> consumer,
                               const CompletePcmStartGate& gate) {
    std::uint64_t before;
    {
        std::lock_guard lock(mutex_);
        if (closing_ || status_.busy || !gate.ready() || plan.identity.job_id <= last_job_id_) return false;
        before = epoch_;
    }
    if (!consumer) throw std::invalid_argument("empty complete PCM consumer");
    const auto start_time = now_();
    auto ticket = std::make_shared<Ticket>();
    auto guarded = std::make_unique<GuardedConsumer>(std::move(consumer), [this,ticket] { return permitted(ticket->value); });
    auto core = std::make_unique<CompletePcmInput>(plan, std::move(guarded), now_);
    const auto duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(plan.duration);
    if (start_time.time_since_epoch() > std::chrono::steady_clock::duration::max() - duration)
        throw std::invalid_argument("complete PCM worker deadline overflow");
    auto job = std::make_unique<Job>(Job{plan, ticket, std::move(core)});
    {
        std::lock_guard lock(mutex_);
        if (closing_ || status_.busy || before != epoch_ || plan.identity.job_id <= last_job_id_) return false;
        ticket->value = active_ticket_ = ++epoch_;
        last_job_id_ = plan.identity.job_id;
        status_ = {};
        status_.identity = plan.identity;
        status_.input_state = CompletePcmState::collecting;
        status_.phase = CompletePcmWorkerPhase::queued;
        status_.busy = true;
        status_.reader_released = false; // Includes pending admission, not only an open handle.
        status_.reserved_bytes = job->core->reserved_bytes();
        pending_result_.clear(); result_.clear();
        last_now_ = start_time;
        job_deadline_ = start_time + duration;
        call_budget_ms_ = plan.policy.time_budget_ms;
        call_deadline_.reset();
        queued_ = std::move(job);
    }
    changed_.notify_all();
    return true;
}

void CompletePcmWorker::stop_locked(CompletePcmState state, CompletePcmWorkerFailure reason) {
    if (!status_.identity || status_.input_state == CompletePcmState::invalid) return;
    if (state != CompletePcmState::invalid &&
        (status_.input_state == CompletePcmState::incomplete || status_.input_state == CompletePcmState::complete)) return;
    status_.input_state = state;
    status_.failure = reason;
    pending_result_.clear(); result_.clear();
    if (status_.phase == CompletePcmWorkerPhase::prepared) {
        status_.busy = false;
        status_.phase = CompletePcmWorkerPhase::finished;
    }
}

void CompletePcmWorker::refresh_locked() {
    if (!status_.busy || (status_.input_state != CompletePcmState::collecting &&
                         status_.input_state != CompletePcmState::prepared)) return;
    std::chrono::steady_clock::time_point now;
    try { now = now_(); }
    catch (...) { stop_locked(CompletePcmState::invalid, CompletePcmWorkerFailure::clock_failed); return; }
    if (now < last_now_) {
        stop_locked(CompletePcmState::invalid, CompletePcmWorkerFailure::clock_regressed); return;
    }
    last_now_ = now;
    if (now >= job_deadline_) stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::job_deadline);
    else if (status_.call_inflight && call_deadline_ && now >= *call_deadline_)
        stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::call_deadline);
}

bool CompletePcmWorker::permitted_locked(std::uint64_t ticket) {
    refresh_locked();
    return ticket == active_ticket_ && !closing_ && status_.busy &&
        (status_.input_state == CompletePcmState::collecting || status_.input_state == CompletePcmState::prepared);
}
bool CompletePcmWorker::permitted(std::uint64_t ticket) {
    std::lock_guard lock(mutex_);
    return permitted_locked(ticket);
}

void CompletePcmWorker::cancel() {
    std::lock_guard lock(mutex_);
    ++epoch_; // Also cancels an admission whose CPU preflight has not finished.
    stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::cancelled);
}

void CompletePcmWorker::update_locked(const CompletePcmIdentity& current, const CompletePcmStartGate& gate) {
    if (status_.identity && current != *status_.identity) {
        ++epoch_;
        stop_locked(CompletePcmState::invalid, CompletePcmWorkerFailure::context_changed);
    }
    if (!gate.ready()) {
        ++epoch_;
        if (status_.busy) stop_locked(CompletePcmState::invalid, CompletePcmWorkerFailure::not_ready);
    }
    refresh_locked();
}

CompletePcmWorkerStatus CompletePcmWorker::poll(const CompletePcmIdentity& current, const CompletePcmStartGate& gate) {
    std::lock_guard lock(mutex_);
    update_locked(current, gate);
    return status_;
}

bool CompletePcmWorker::confirm(const CompletePcmIdentity& current, const CompletePcmStartGate& gate) {
    std::lock_guard lock(mutex_);
    update_locked(current, gate);
    if (!status_.busy || status_.phase != CompletePcmWorkerPhase::prepared ||
        status_.input_state != CompletePcmState::prepared || !status_.reader_released || pending_result_.empty()) return false;
    result_ = std::move(pending_result_);
    status_.input_state = CompletePcmState::complete;
    status_.phase = CompletePcmWorkerPhase::finished;
    status_.busy = false;
    return true;
}

std::vector<std::uint8_t> CompletePcmWorker::result(const CompletePcmIdentity& current) const {
    std::lock_guard lock(mutex_);
    return status_.identity && current == *status_.identity && status_.input_state == CompletePcmState::complete
        ? result_ : std::vector<std::uint8_t>{};
}

bool CompletePcmWorker::call(Job& job, CompletePcmWorkerPhase phase,
                              const std::function<void()>& action, bool cleanup) {
    {
        std::lock_guard lock(mutex_);
        if (!cleanup && !permitted_locked(job.ticket->value)) return false;
        status_.phase = phase;
        status_.waiting_for_access = true;
    }
    bool success = false;
    access_->invoke([&] {
        {
            std::lock_guard lock(mutex_);
            status_.waiting_for_access = false;
            const bool allowed = permitted_locked(job.ticket->value);
            if (!cleanup && !allowed) return;
            call_deadline_.reset();
            if (allowed) {
                const auto budget = std::min(std::chrono::milliseconds(call_budget_ms_),
                    std::chrono::duration_cast<std::chrono::milliseconds>(job_deadline_ - last_now_));
                if (budget.count() <= 0) {
                    stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::job_deadline);
                    if (!cleanup) return;
                } else call_deadline_ = last_now_ + budget;
            }
            // This reservation is the linearization point for an in-flight call.
            status_.call_inflight = true;
        }
        try { action(); success = true; }
        catch (...) {
            std::lock_guard lock(mutex_);
            stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::operation_failed);
        }
        {
            std::lock_guard lock(mutex_);
            refresh_locked();
            status_.call_inflight = false;
            call_deadline_.reset();
        }
    });
    return success && (cleanup || permitted(job.ticket->value));
}

void CompletePcmWorker::publish_core(Job& job) {
    std::lock_guard lock(mutex_);
    if (!permitted_locked(job.ticket->value)) return;
    status_.accepted_frames = job.core->accepted_frames();
    status_.accepted_bytes = job.core->accepted_bytes();
    status_.read_observations = job.core->read_observations();
    status_.uncertain_reads = job.core->uncertain_reads();
    status_.recovered_reads = job.core->recovered_reads();
    if (job.core->state() == CompletePcmState::incomplete || job.core->state() == CompletePcmState::invalid) {
        status_.core_failure = job.core->failure();
        stop_locked(job.core->state(), CompletePcmWorkerFailure::core_failed);
    }
}

void CompletePcmWorker::execute(Job& job) {
    std::unique_ptr<CddaReader> reader;
    DiscToc final_toc;
    bool prepared = false;
    std::vector<std::uint8_t> candidate;
    try {
        DiscToc initial_toc;
        if (!call(job, CompletePcmWorkerPhase::initial_toc, [&] { initial_toc = probe_(); })) throw Stopped{};
        if (!same_toc(initial_toc, job.plan.toc)) {
            std::lock_guard lock(mutex_);
            stop_locked(CompletePcmState::invalid, CompletePcmWorkerFailure::toc_changed); throw Stopped{};
        }
        if (!call(job, CompletePcmWorkerPhase::opening, [&] {
            reader = factory_();
            if (!reader) throw std::runtime_error("empty raw reader");
            reader = std::make_unique<GuardedReader>(std::move(reader),
                [this,ticket=job.ticket] { return permitted(ticket->value); });
            reader = make_offset_reader(std::move(reader), make_read_offset_window(
                job.plan.toc.tracks.front().start_lba, job.plan.toc.leadout_lba, 0));
            if (job.plan.policy.mode == ReadVerificationMode::repeat)
                reader = make_repeated_read_verifier(std::move(reader), repeated_read_policy(job.plan.policy), now_);
        })) throw Stopped{};
        if (!call(job, CompletePcmWorkerPhase::seeking, [&] { reader->seek(job.core->next_lba()); })) throw Stopped{};
        const auto block = job.plan.policy.mode == ReadVerificationMode::single ? 15 : job.plan.policy.region_frames;
        std::vector<std::int16_t> pcm(block * cdda_samples_per_frame);
        {
            std::lock_guard lock(mutex_);
            status_.pcm_capacity_bytes = pcm.capacity() * sizeof(std::int16_t);
            status_.peak_pcm_bytes = std::max(status_.peak_pcm_bytes, status_.pcm_capacity_bytes);
            if (status_.pcm_capacity_bytes > 75 * cdda_samples_per_frame * sizeof(std::int16_t)) {
                stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::memory_limit); throw Stopped{};
            }
        }
        while (job.core->next_lba() < job.plan.toc.leadout_lba) {
            if (!permitted(job.ticket->value)) throw Stopped{};
            const auto frames = std::min<std::size_t>(block, job.plan.toc.leadout_lba - job.core->next_lba());
            const auto output = std::span(pcm).first(frames * cdda_samples_per_frame);
            ReadResult read{};
            if (!call(job, CompletePcmWorkerPhase::reading, [&] { read = reader->read(output); })) throw Stopped{};
            {
                std::lock_guard lock(mutex_);
                if (!permitted_locked(job.ticket->value)) throw Stopped{};
                status_.native_error = read.native_error;
                status_.peak_pcm_bytes = std::max(status_.peak_pcm_bytes, output.size_bytes());
            }
            if (!job.core->append(job.plan.identity, read, output)) {
                publish_core(job); throw Stopped{};
            }
            publish_core(job);
        }
        {
            std::lock_guard lock(mutex_);
            if (!permitted_locked(job.ticket->value)) throw Stopped{};
            status_.phase = CompletePcmWorkerPhase::finalizing;
        }
        if (!job.core->prepare(job.plan.identity)) { publish_core(job); throw Stopped{}; }
        if (!call(job, CompletePcmWorkerPhase::final_toc, [&] { final_toc = probe_(); })) throw Stopped{};
        prepared = true;
    } catch (const Stopped&) {
    } catch (...) {
        std::lock_guard lock(mutex_);
        stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::operation_failed);
    }
    // Cleanup is the only device operation permitted after cancellation.
    if (reader) call(job, CompletePcmWorkerPhase::releasing, [&] { reader.reset(); }, true);
    try {
        if (prepared && permitted(job.ticket->value)) {
            if (job.core->confirm(job.plan.identity, final_toc, true)) {
                const auto bytes = job.core->result();
                candidate.assign(bytes.begin(), bytes.end());
            } else publish_core(job);
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::operation_failed);
    }
    job.core.reset(); // Consumer cleanup also belongs to the worker, outside its status mutex.
    {
        std::lock_guard lock(mutex_);
        const bool allowed = permitted_locked(job.ticket->value);
        status_.reader_released = true;
        status_.pcm_capacity_bytes = 0;
        status_.call_inflight = status_.waiting_for_access = false;
        if (allowed && !candidate.empty()) {
            pending_result_ = std::move(candidate);
            status_.input_state = CompletePcmState::prepared;
            status_.phase = CompletePcmWorkerPhase::prepared;
        } else {
            if (allowed) stop_locked(CompletePcmState::incomplete, CompletePcmWorkerFailure::core_failed);
            status_.busy = false;
            status_.phase = CompletePcmWorkerPhase::finished;
        }
    }
}

void CompletePcmWorker::run() {
    for (;;) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] { return closing_ || queued_; });
            if (!queued_) return;
            job = std::move(queued_);
        }
        execute(*job);
    }
}
