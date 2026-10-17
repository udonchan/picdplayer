#include "complete_pcm_worker.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
unsigned cases = 0, checks = 0;
std::atomic<unsigned> gate_timeouts{0};
void check(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("complete PCM worker assertion failed");
}
void test(const std::string& name, const std::function<void()>& fn) {
    try { fn(); ++cases; }
    catch (const std::exception& error) { throw std::runtime_error(name + ": " + error.what()); }
}
enum class Point : unsigned { initial_probe, open, seek, read, consume, finalize, final_probe, close, discard, external, count };
constexpr unsigned index(Point p) { return static_cast<unsigned>(p); }
bool device_call(Point p) {
    return p == Point::initial_probe || p == Point::open || p == Point::seek || p == Point::read ||
        p == Point::final_probe || p == Point::close || p == Point::external;
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool blocked = false;
    unsigned entered = 0;
    void arm() { std::lock_guard lock(mutex); blocked = true; entered = 0; }
    void release() { std::lock_guard lock(mutex); blocked = false; changed.notify_all(); }
    bool wait_entered() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered != 0; });
    }
    void pass() {
        std::unique_lock lock(mutex);
        ++entered; changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return !blocked; })) ++gate_timeouts;
    }
};
struct Shared {
    std::array<Gate, index(Point::count)> gates;
    std::mutex trace_mutex;
    struct Event { Point point; std::thread::id thread; };
    std::vector<Event> events;
    unsigned active_io = 0, maximum_io = 0;
    std::atomic<int> throw_point{-1};
    std::atomic<bool> null_factory{false}, partial{false}, zero{false}, bad_start{false};
    std::atomic<bool> bad_initial_toc{false}, bad_final_toc{false}, bad_clock{false};
    std::atomic<int> fail_after_reads{-1};
    std::atomic<unsigned> probes{0}, opens{0}, closes{0}, seeks{0}, reads{0}, consumes{0}, finalizes{0}, discards{0};
    std::atomic<std::uint64_t> consumed_bytes{0};
    std::atomic<std::int64_t> clock_ms{0};
    std::function<void()> on_consume, on_finalize;
    void release_all() { for (auto& gate : gates) gate.release(); }
};
struct Trace {
    std::shared_ptr<Shared> shared;
    Point point;
    Trace(std::shared_ptr<Shared> s, Point p) : shared(std::move(s)), point(p) {
        std::lock_guard lock(shared->trace_mutex);
        shared->events.push_back({point,std::this_thread::get_id()});
        if (device_call(point)) shared->maximum_io = std::max(shared->maximum_io, ++shared->active_io);
    }
    ~Trace() {
        std::lock_guard lock(shared->trace_mutex);
        if (device_call(point)) --shared->active_io;
    }
    void pass(bool may_throw = true) {
        shared->gates[index(point)].pass();
        if (may_throw && shared->throw_point == static_cast<int>(point)) throw std::runtime_error("fake failure");
    }
};
std::int16_t sample(std::int64_t index) { return static_cast<std::int16_t>((index * 37) % 65536 - 32768); }
CompletePcmPlan plan() {
    CompletePcmPlan p;
    p.toc = make_audio_toc(7, std::vector<std::int32_t>{150,155,169}, 185);
    p.identity = {"worker-test",1,2,1,0,0,
        {std::string(complete_pcm_canonical_profile),"test-worker-byte-check","1",4096,4}};
    p.duration = std::chrono::seconds(10);
    p.policy.time_budget_ms = 1000;
    return p;
}
CompletePcmStartGate ready() { return {true,true,true,true,true}; }

class Reader final : public CddaReader {
    std::shared_ptr<Shared> shared_;
    std::int32_t position_ = 0;
public:
    explicit Reader(std::shared_ptr<Shared> s) : shared_(std::move(s)) { ++shared_->opens; }
    ~Reader() override { Trace trace(shared_,Point::close); trace.pass(false); ++shared_->closes; }
    void seek(std::int32_t position) override {
        Trace trace(shared_,Point::seek); ++shared_->seeks; trace.pass(); position_ = position;
    }
    ReadResult read(std::span<std::int16_t> pcm) override {
        Trace trace(shared_,Point::read); const auto call = ++shared_->reads; trace.pass();
        const auto frames = pcm.size()/cdda_samples_per_frame;
        for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = sample(std::int64_t(position_)*cdda_samples_per_frame+i);
        ReadResult r{position_,frames,frames,ReadStatus::ok,0,0};
        if (shared_->partial) --r.frames_read;
        if (shared_->zero) r.frames_read = 0;
        if (shared_->bad_start) ++r.start_lba;
        if (shared_->fail_after_reads >= 0 && call > static_cast<unsigned>(shared_->fail_after_reads.load())) {
            r.status = ReadStatus::read_error; r.native_error = EIO; r.frames_read = 0;
        }
        position_ += static_cast<std::int32_t>(frames);
        return r;
    }
};
class Consumer final : public CompletePcmConsumer {
    std::shared_ptr<Shared> shared_;
    CompletePcmPlan plan_;
    std::uint64_t bytes_ = 0;
    std::array<std::uint8_t,4> result_;
public:
    Consumer(std::shared_ptr<Shared> s, CompletePcmPlan p)
        : shared_(std::move(s)), plan_(std::move(p)), result_{static_cast<std::uint8_t>(plan_.identity.job_id),2,3,4} {}
    const CompletePcmProfile& profile() const noexcept override { return plan_.identity.profile; }
    void consume(int track, std::span<const std::uint8_t> bytes) override {
        Trace trace(shared_,Point::consume); ++shared_->consumes; trace.pass();
        const auto begin = plan_.toc.tracks.front().start_lba;
        for (const auto byte : bytes) {
            const auto position = std::int64_t(begin)*cdda_samples_per_frame + static_cast<std::int64_t>(bytes_/2);
            const auto value = static_cast<std::uint16_t>(sample(position));
            if (byte != ((bytes_%2 ? value >> 8 : value) & 255)) throw std::runtime_error("canonical byte mismatch");
            const auto lba = begin + bytes_/2352;
            const auto it = std::find_if(plan_.toc.tracks.begin(), plan_.toc.tracks.end(), [&](const auto& t) {
                return lba >= static_cast<std::uint64_t>(t.start_lba) && lba < static_cast<std::uint64_t>(t.start_lba+t.length_frames);
            });
            if (it == plan_.toc.tracks.end() || it->number != track) throw std::runtime_error("track boundary mismatch");
            ++bytes_;
        }
        shared_->consumed_bytes += bytes.size();
        if (shared_->on_consume) shared_->on_consume();
    }
    std::span<const std::uint8_t> finalize() override {
        Trace trace(shared_,Point::finalize); ++shared_->finalizes; trace.pass();
        if (bytes_ != static_cast<std::uint64_t>(plan_.toc.leadout_lba-plan_.toc.tracks.front().start_lba)*2352)
            throw std::runtime_error("missing input");
        if (shared_->on_finalize) shared_->on_finalize();
        return result_;
    }
    void discard() noexcept override { Trace trace(shared_,Point::discard); trace.pass(false); ++shared_->discards; result_.fill(0); }
};

struct Fixture {
    CompletePcmPlan p = plan();
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::shared_ptr<DriveAccessCoordinator> access = std::make_shared<DriveAccessCoordinator>();
    std::unique_ptr<CompletePcmWorker> worker;
    Fixture() {
        worker = std::make_unique<CompletePcmWorker>([s=shared]() -> std::unique_ptr<CddaReader> {
            Trace trace(s,Point::open); trace.pass();
            if (s->null_factory) return {};
            return std::make_unique<Reader>(s);
        }, [s=shared,toc=p.toc] {
            ++s->probes;
            const bool first = s->opens.load() == s->closes.load();
            Trace trace(s,first ? Point::initial_probe : Point::final_probe); trace.pass();
            auto result = toc;
            if ((first && s->bad_initial_toc) || (!first && s->bad_final_toc)) ++result.leadout_lba;
            return result;
        }, access, [s=shared] {
            if (s->bad_clock) throw std::runtime_error("fake clock");
            return std::chrono::steady_clock::time_point{std::chrono::milliseconds(s->clock_ms.load())};
        });
    }
    ~Fixture() { shared->release_all(); worker.reset(); }
    bool start(CompletePcmStartGate gate = ready()) { return worker->start(p,std::make_unique<Consumer>(shared,p),gate); }
    CompletePcmWorkerStatus status() { return worker->poll(p.identity,ready()); }
    CompletePcmWorkerStatus wait(const std::function<bool(const CompletePcmWorkerStatus&)>& predicate) {
        const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(3);
        do {
            auto s = status(); if (predicate(s)) return s;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now()<end);
        throw std::runtime_error("worker state timed out");
    }
    CompletePcmWorkerStatus released() { return wait([](const auto& s) {return !s.busy && s.reader_released;}); }
    CompletePcmWorkerStatus prepared() { return wait([](const auto& s) {return s.phase == CompletePcmWorkerPhase::prepared;}); }
    void arm(Point point) { shared->gates[index(point)].arm(); }
    void entered(Point point) { check(shared->gates[index(point)].wait_entered()); }
    void release(Point point) { shared->gates[index(point)].release(); }
    void nonblocking(const std::function<void()>& fn) {
        auto result = std::async(std::launch::async,fn);
        const bool returned = result.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
        if (!returned) shared->release_all();
        result.get(); check(returned);
    }
};
}

int main() {
    try {
        for (bool repeat : {false,true}) test(repeat ? "repeat normal completion" : "single normal completion", [=] {
            Fixture f;
            if (repeat) { f.p.policy.mode = ReadVerificationMode::repeat; f.p.policy.region_frames = 15; }
            check(f.start());
            auto s = f.prepared();
            check(s.busy && s.reader_released && !s.call_inflight && !s.waiting_for_access);
            check(s.input_state == CompletePcmState::prepared && s.accepted_frames == 35 && s.accepted_bytes == 35*2352);
            check(s.pcm_capacity_bytes == 0 && s.peak_pcm_bytes == 15*2352);
            check(s.reserved_bytes < complete_pcm_memory_limit && f.worker->result(f.p.identity).empty());
            check(f.shared->opens == 1 && f.shared->closes == 1 && f.shared->probes == 2);
            check(f.shared->reads == (repeat ? 6 : 3) && f.shared->consumed_bytes == 35*2352);
            check(f.worker->confirm(f.p.identity,ready()));
            s = f.status(); check(!s.busy && s.input_state == CompletePcmState::complete);
            check(f.worker->result(f.p.identity) == std::vector<std::uint8_t>({1,2,3,4}));
            check(!f.start()); // No same-job replay.
            f.worker->cancel(); check(f.status().input_state == CompletePcmState::complete);
            std::lock_guard lock(f.shared->trace_mutex);
            check(f.shared->maximum_io == 1 && !f.shared->events.empty());
            const auto owner = f.shared->events.front().thread;
            check(owner != std::this_thread::get_id());
            for (const auto& event : f.shared->events) check(event.thread == owner);
        });
        test("maximum repeat region stays bounded", [] {
            Fixture f; f.p.policy.mode=ReadVerificationMode::repeat;
            check(f.start()); auto s=f.prepared();
            check(s.accepted_frames==35 && s.peak_pcm_bytes==75*2352 && s.pcm_capacity_bytes==0);
            check(f.shared->reads==2 && s.reserved_bytes<complete_pcm_memory_limit);
            check(f.worker->confirm(f.p.identity,ready()));
        });
        for (unsigned field=0; field<5; ++field) test("start readiness gate", [=] {
            Fixture f; auto gate = ready();
            switch(field) {
            case 0: gate.playback_stopped=false; break;
            case 1: gate.output_stopped=false; break;
            case 2: gate.queue_cancelled=false; break;
            case 3: gate.playback_reader_released=false; break;
            case 4: gate.audio_ready=false; break;
            }
            check(!f.start(gate)); check(f.shared->probes == 0 && f.shared->opens == 0 && !f.status().busy);
        });
        test("invalid plan before all drive callbacks", [] {
            Fixture f; f.p.identity.offset_samples.reset(); bool rejected=false;
            try { f.start(); } catch(const std::invalid_argument&) {rejected=true;}
            check(rejected && f.shared->probes==0 && f.shared->opens==0);
            f.p.identity.offset_samples=1; rejected=false;
            try { f.start(); } catch(const std::invalid_argument&) {rejected=true;}
            check(rejected && !f.status().busy);
        });
        for (auto point : {Point::initial_probe,Point::open,Point::seek,Point::read,Point::consume,Point::finalize,Point::final_probe,Point::close,Point::discard})
            test("cancel while callback is blocked " + std::to_string(index(point)), [=] {
                Fixture f; f.arm(point); check(f.start()); f.entered(point);
                CompletePcmWorkerStatus snapshot;
                f.nonblocking([&] { f.worker->cancel(); snapshot=f.status(); });
                check(snapshot.busy && !snapshot.reader_released && snapshot.input_state==CompletePcmState::incomplete);
                auto next=f.p; ++next.identity.job_id; ++next.identity.reader_incarnation;
                check(!f.worker->start(next,std::make_unique<Consumer>(f.shared,next),ready()));
                check(f.worker->result(f.p.identity).empty());
                f.release(point); snapshot=f.released();
                check(snapshot.input_state==CompletePcmState::incomplete && snapshot.failure==CompletePcmWorkerFailure::cancelled);
                check(f.shared->opens==f.shared->closes);
                if(point==Point::initial_probe) check(f.shared->opens==0);
                if(point==Point::open || point==Point::seek) check(f.shared->reads==0);
                if(point==Point::read) check(f.shared->reads==1 && f.shared->consumes==0 && snapshot.accepted_frames==0);
                check(!f.worker->confirm(f.p.identity,ready()) && f.worker->result(f.p.identity).empty());
            });
        test("coordinator contention cancels before open", [] {
            Fixture f; f.arm(Point::external);
            auto holder=std::async(std::launch::async,[&] { f.access->invoke([&] {
                Trace trace(f.shared,Point::external); trace.pass(false);
            }); });
            f.entered(Point::external); check(f.start());
            f.wait([](const auto& s) {return s.waiting_for_access;});
            f.nonblocking([&] { f.worker->cancel(); check(f.status().busy); });
            check(f.shared->probes==0 && f.shared->opens==0);
            f.release(Point::external); holder.get(); f.released();
            check(f.shared->opens==0 && f.shared->probes==0);
        });
        test("cancel first physical repeat read prevents all later attempts", [] {
            Fixture f; f.p.policy.mode=ReadVerificationMode::repeat; f.p.policy.region_frames=15;
            f.arm(Point::read); check(f.start()); f.entered(Point::read); f.worker->cancel();
            f.release(Point::read); const auto s=f.released();
            check(f.shared->reads==1 && f.shared->consumes==0 && s.accepted_frames==0);
            check(s.failure==CompletePcmWorkerFailure::cancelled);
        });
        for (unsigned field=0; field<7; ++field) test("late read with changed identity", [=] {
            Fixture f; f.arm(Point::read); check(f.start()); f.entered(Point::read);
            auto changed=f.p.identity;
            switch(field) {
            case 0: changed.session_id="other"; break;
            case 1: ++changed.job_id; break;
            case 2: ++changed.disc_generation; break;
            case 3: ++changed.reader_incarnation; break;
            case 4: ++changed.policy_revision; break;
            case 5: changed.offset_samples=1; break;
            case 6: changed.profile.version="2"; break;
            }
            auto s=f.worker->poll(changed,ready());
            check(s.busy && s.input_state==CompletePcmState::invalid && !s.reader_released);
            f.release(Point::read); s=f.released();
            check(s.input_state==CompletePcmState::invalid && s.accepted_frames==0 && f.shared->consumes==0);
            check(f.worker->result(changed).empty() && f.worker->result(f.p.identity).empty());
        });
        test("readiness lost during read", [] {
            Fixture f; f.arm(Point::read); check(f.start()); f.entered(Point::read);
            auto gate=ready(); gate.playback_stopped=false;
            auto s=f.worker->poll(f.p.identity,gate);
            check(s.input_state==CompletePcmState::invalid && s.failure==CompletePcmWorkerFailure::not_ready);
            f.release(Point::read); f.released(); check(f.shared->consumes==0);
        });
        for (bool job : {false,true}) test(job ? "job deadline in blocked read" : "call deadline in blocked read", [=] {
            Fixture f; if(job) f.p.duration=std::chrono::milliseconds(500);
            f.arm(Point::read); check(f.start()); f.entered(Point::read);
            f.shared->clock_ms=job?500:1000;
            auto s=f.status(); check(s.busy && s.call_inflight && !s.reader_released);
            check(s.failure==(job?CompletePcmWorkerFailure::job_deadline:CompletePcmWorkerFailure::call_deadline));
            f.release(Point::read); s=f.released(); check(s.accepted_frames==0 && f.shared->consumes==0);
        });
        for (bool regression : {false,true}) test("clock invalidation while blocked", [=] {
            Fixture f; f.arm(Point::read); check(f.start()); f.entered(Point::read);
            if(regression) f.shared->clock_ms=-1; else f.shared->bad_clock=true;
            auto s=f.status(); check(s.input_state==CompletePcmState::invalid);
            check(s.failure==(regression?CompletePcmWorkerFailure::clock_regressed:CompletePcmWorkerFailure::clock_failed));
            f.release(Point::read); f.released(); check(f.shared->consumes==0);
        });
        for (auto point : {Point::initial_probe,Point::open,Point::seek,Point::read,Point::consume,Point::finalize,Point::final_probe})
            test("callback exception " + std::to_string(index(point)), [=] {
                Fixture f; f.shared->throw_point=static_cast<int>(point); check(f.start());
                const auto s=f.released();
                check(s.input_state==CompletePcmState::incomplete && f.worker->result(f.p.identity).empty());
                check(f.shared->opens==f.shared->closes);
            });
        for (unsigned mode=0; mode<5; ++mode) test("partial zero wrong-position and failed read", [=] {
            Fixture f;
            if(mode==0) f.shared->partial=true;
            if(mode==1) f.shared->zero=true;
            if(mode==2) f.shared->bad_start=true;
            if(mode>=3) f.shared->fail_after_reads=mode==3?0:1;
            check(f.start()); const auto s=f.released();
            check(s.input_state==CompletePcmState::incomplete && s.native_error==EIO);
            check(s.accepted_frames==(mode==4?15:0) && f.shared->finalizes==0);
            check(f.worker->result(f.p.identity).empty() && f.shared->opens==f.shared->closes);
        });
        for (bool final : {false,true}) test("TOC mismatch", [=] {
            Fixture f; if(final) f.shared->bad_final_toc=true; else f.shared->bad_initial_toc=true;
            check(f.start()); const auto s=f.released();
            check(s.input_state==CompletePcmState::invalid && f.worker->result(f.p.identity).empty());
            if(!final) check(f.shared->opens==0);
            check(f.shared->opens==f.shared->closes);
        });
        test("null factory result", [] {
            Fixture f; f.shared->null_factory=true; check(f.start());
            const auto s=f.released(); check(s.input_state==CompletePcmState::incomplete && f.shared->reads==0);
        });
        test("cancel prepared and start a new incarnation", [] {
            Fixture f; check(f.start()); f.prepared();
            check(!f.start()); f.worker->cancel(); check(!f.worker->confirm(f.p.identity,ready()));
            const auto previous=f.p.identity;
            ++f.p.identity.job_id; ++f.p.identity.reader_incarnation;
            check(f.start()); f.prepared(); check(f.worker->confirm(f.p.identity,ready()));
            check(f.worker->result(previous).empty());
            check(f.worker->result(f.p.identity)==std::vector<std::uint8_t>({2,2,3,4}));
            check(f.shared->opens==2 && f.shared->closes==2);
        });
        test("cancelled read then a fresh job has its own initial probe", [] {
            Fixture f; f.arm(Point::read); check(f.start()); f.entered(Point::read);
            f.worker->cancel(); f.release(Point::read); f.released();
            const auto previous=f.p.identity;
            ++f.p.identity.job_id; ++f.p.identity.disc_generation; ++f.p.identity.reader_incarnation;
            f.arm(Point::initial_probe); check(f.start()); f.entered(Point::initial_probe);
            check(f.shared->opens==1 && f.worker->result(previous).empty());
            f.release(Point::initial_probe); f.prepared();
            check(f.worker->confirm(f.p.identity,ready()));
            check(f.shared->probes==3 && f.shared->opens==2 && f.shared->closes==2);
            check(f.shared->consumed_bytes==35*2352 && f.worker->result(previous).empty());
        });
        test("completed receipt invalidated by later context", [] {
            Fixture f; check(f.start()); f.prepared(); check(f.worker->confirm(f.p.identity,ready()));
            auto current=f.p.identity; ++current.disc_generation;
            check(f.worker->poll(current,ready()).input_state==CompletePcmState::invalid);
            check(f.worker->result(current).empty() && f.worker->result(f.p.identity).empty());
        });
        test("consumer calls cancel without worker mutex deadlock", [] {
            Fixture f; f.shared->on_consume=[worker=f.worker.get()] {worker->cancel();};
            check(f.start()); const auto s=f.released();
            check(s.input_state==CompletePcmState::incomplete && s.accepted_frames==0);
            check(f.shared->consumes==1 && f.worker->result(f.p.identity).empty());
        });
        test("finalize cancellation never publishes success", [] {
            Fixture f; f.shared->on_finalize=[worker=f.worker.get()] {worker->cancel();};
            check(f.start()); const auto s=f.released();
            check(s.input_state==CompletePcmState::incomplete && s.accepted_frames==35);
            check(!f.worker->confirm(f.p.identity,ready()) && f.worker->result(f.p.identity).empty());
        });
        test("prepared receipt deadline and readiness gates", [] {
            Fixture f; check(f.start()); f.prepared(); f.shared->clock_ms=10000;
            check(!f.worker->confirm(f.p.identity,ready()) && !f.status().busy);
            check(f.worker->result(f.p.identity).empty());
            Fixture other; check(other.start()); other.prepared(); auto gate=ready(); gate.audio_ready=false;
            check(!other.worker->confirm(other.p.identity,gate) && !other.status().busy);
        });
        test("close deadline preserves release wait and rejects completion", [] {
            Fixture f; f.arm(Point::close); check(f.start()); f.entered(Point::close);
            f.shared->clock_ms=1000;
            auto s=f.status(); check(s.busy && !s.reader_released && s.failure==CompletePcmWorkerFailure::call_deadline);
            f.release(Point::close); s=f.released();
            check(s.accepted_frames==35 && !f.worker->confirm(f.p.identity,ready()));
        });
        test("deadline during consumer and finalize", [] {
            for(bool finalize : {false,true}) {
                Fixture f;
                if(finalize) f.shared->on_finalize=[&] {f.shared->clock_ms=10000;};
                else f.shared->on_consume=[&] {f.shared->clock_ms=10000;};
                check(f.start()); const auto s=f.released();
                check(s.failure==CompletePcmWorkerFailure::job_deadline && f.worker->result(f.p.identity).empty());
            }
        });
        test("confirm and cancel use one publication order", [] {
            for(unsigned i=0;i<8;++i) {
                Fixture f; check(f.start()); f.prepared();
                std::promise<void> release; auto start=release.get_future().share();
                auto commit=std::async(std::launch::async,[&] {start.wait();return f.worker->confirm(f.p.identity,ready());});
                auto cancel=std::async(std::launch::async,[&] {start.wait();f.worker->cancel();});
                release.set_value(); const bool committed=commit.get(); cancel.get(); const auto s=f.status();
                check(!s.busy && s.reader_released);
                check(s.input_state==(committed?CompletePcmState::complete:CompletePcmState::incomplete));
                check(f.worker->result(f.p.identity).empty()!=committed);
            }
        });
        test("destruction joins outstanding close on owner", [] {
            Fixture f; f.arm(Point::close); check(f.start()); f.entered(Point::close);
            auto worker=std::move(f.worker);
            auto destruction=std::async(std::launch::async,[w=std::move(worker)]() mutable {w.reset();});
            const bool waiting=destruction.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout;
            f.release(Point::close); destruction.get();
            check(waiting && f.shared->opens==f.shared->closes);
        });
        check(gate_timeouts==0);
        std::cout << "PASS: " << cases << " acquisition-worker cases, " << checks << " checks (fake I/O only)\n";
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
