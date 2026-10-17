#include "device_lifecycle.hpp"
#include "drive_capabilities.hpp"
#include "pcm_worker.hpp"
#include "read_policy.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
unsigned checks=0, cases=0;
std::atomic<unsigned> timeouts{0};
void check(bool value) {++checks;if(!value)throw std::runtime_error("device lifecycle assertion failed");}
void test(const char* name,const std::function<void()>& fn) {
    try {fn();++cases;} catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}
}
template<class F> void wait(F predicate) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do {if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error("device lifecycle wait timeout");
}
struct Gate {
    std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
    void pass(){std::unique_lock lock(mutex);entered=true;changed.notify_all();
        if(!changed.wait_for(lock,std::chrono::seconds(5),[&]{return open;}))++timeouts;}
    void reached(){std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}));}
    void release(){std::lock_guard lock(mutex);open=true;changed.notify_all();}
};
struct ReleaseGate {Gate& gate;~ReleaseGate(){gate.release();}};
struct ObserverFixture {
    Gate first,second;
    std::unique_ptr<DeviceLifecycleObserver> observer;
    ~ObserverFixture(){first.release();second.release();observer.reset();}
};
struct Temp {
    std::filesystem::path root;
    Temp(){char pattern[]="/tmp/picdplayer-observation-XXXXXX";const auto result=mkdtemp(pattern);
        if(!result)throw std::runtime_error("temporary observation root");
        root=result;}
    ~Temp(){std::error_code error;std::filesystem::remove_all(root,error);}
};
struct ReaderState {Gate read;std::atomic<unsigned> calls{0},closed{0};std::thread::id created_on,closed_on;};
class Reader final:public CddaReader {
    std::shared_ptr<ReaderState> state_;
public:
    explicit Reader(std::shared_ptr<ReaderState> state):state_(std::move(state)){state_->created_on=std::this_thread::get_id();}
    ~Reader()override{state_->closed_on=std::this_thread::get_id();++state_->closed;}
    void seek(std::int32_t)override{}
    ReadResult read(std::span<std::int16_t> pcm)override{
        ++state_->calls;state_->read.pass();std::fill(pcm.begin(),pcm.end(),123);
        return {0,pcm.size()/cdda_samples_per_frame,pcm.size()/cdda_samples_per_frame,ReadStatus::ok,0,0};
    }
};
}
int main(){
    try{
        test("observation state and signature epochs",[]{
            DeviceObservationTracker tracker;
            check(tracker.snapshot().generation==0&&!tracker.snapshot().sampled);
            auto s=tracker.observe({DevicePresence::present,"path-a:dev9",{}});
            check(s.generation==1&&s.reason==DeviceChangeReason::initial);
            check(tracker.observe({DevicePresence::present,"path-a:dev9",{}}).generation==1);
            s=tracker.observe({DevicePresence::present,"path-a:dev10",{}});
            check(s.generation==2&&s.reason==DeviceChangeReason::signature_changed);
            check(tracker.observe({DevicePresence::absent,{},{}}).generation==3);
            check(tracker.observe({DevicePresence::absent,{},{}}).generation==3);
            check(tracker.observe({DevicePresence::present,"path-a:dev10",{}}).generation==4);
            check(tracker.observe({DevicePresence::unknown,{},"unavailable"}).generation==5);
            check(tracker.observe({DevicePresence::unknown,{},"different error"}).generation==5);
            check(tracker.observe({DevicePresence::present,"path-a:dev10",{}}).generation==6);
            s=tracker.reader_failed();check(s.generation==7&&s.reason==DeviceChangeReason::reader_failure);
            check(tracker.observe({DevicePresence::present,"path-a:dev10",{}}).generation==7);
            // An unobserved reset with exactly the same metadata is not detectable.
            check(tracker.observe({DevicePresence::present,"path-a:dev10",{}}).generation==7);
        });
        test("invalid/oversized metadata is unknown",[]{
            DeviceObservationTracker t;
            check(t.observe({DevicePresence::present,{},{}}).presence==DevicePresence::unknown);
            check(t.observe({DevicePresence::present,std::string(4097,'x'),{}}).presence==DevicePresence::unknown);
            check(t.observe({static_cast<DevicePresence>(99),"x",{}}).presence==DevicePresence::unknown);
            check(t.observe({DevicePresence::unknown,{},std::string(257,'x')}).error=="METADATA_LIMIT");
        });
        test("read-only node/sysfs parser",[]{
            Temp temp;const auto devices=temp.root/"usb1"/"host0"/"block"/"sr0";
            const auto root=temp.root/"dev";std::filesystem::create_directories(devices);std::filesystem::create_directory(root);
            std::filesystem::create_directory_symlink(devices,root/"11:0");
            const auto usb=temp.root/"usb1";
            {std::ofstream(usb/"busnum")<<"1\n";std::ofstream(usb/"devnum")<<"9\n";}
            DeviceNodeInfo info{DevicePresence::present,11,0,903};
            const auto probe=[&](const std::string&){return info;};
            const auto a=observe_device_node("/fake",root,probe);
            check(a.presence==DevicePresence::present&&!a.signature.empty());
            check(observe_device_node("/fake",root,probe).signature==a.signature);
            {std::ofstream(usb/"devnum")<<"10\n";}
            check(observe_device_node("/fake",root,probe).signature!=a.signature);
            ++info.inode;const auto changed=observe_device_node("/fake",root,probe);
            check(changed.presence==DevicePresence::present&&changed.signature!=a.signature);
            {std::ofstream(usb/"devnum")<<"bad\n";}
            check(observe_device_node("/fake",root,probe).presence==DevicePresence::unknown);
            {std::ofstream(usb/"devnum")<<std::string(200,'1');}
            check(observe_device_node("/fake",root,probe).presence==DevicePresence::unknown);
            info.presence=DevicePresence::absent;
            check(observe_device_node("/fake",root,probe).presence==DevicePresence::absent);
            info.presence=DevicePresence::unknown;
            check(observe_device_node("/fake",root,probe).presence==DevicePresence::unknown);
            check(observe_device_node("/fake",temp.root/"missing",[](const auto&){return DeviceNodeInfo{DevicePresence::present,11,0,1};}).presence==DevicePresence::unknown);
        });
        test("probe does not hold the cached snapshot mutex",[]{
            ObserverFixture f;
            f.observer=std::make_unique<DeviceLifecycleObserver>([&]{f.first.pass();return DeviceObservation{DevicePresence::present,"a",{}};},std::chrono::milliseconds(1));
            f.first.reached();
            auto cached=std::async(std::launch::async,[&]{return f.observer->snapshot();});
            const bool ready=cached.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;
            if(!ready)f.first.release();
            check(cached.get().presence==DevicePresence::unknown);check(ready);
            f.first.release();wait([&]{return f.observer->snapshot().sampled;});
        });
        test("manual invalidation drops a sample started in the old context",[]{
            std::atomic<unsigned> calls{0};ObserverFixture f;
            f.observer=std::make_unique<DeviceLifecycleObserver>([&]{
                const auto n=++calls;if(n==1)f.first.pass();else if(n==2)f.second.pass();
                return DeviceObservation{DevicePresence::present,n==1?"old":"new",{}};
            },std::chrono::milliseconds(1));
            f.first.reached();auto s=f.observer->reader_failed();check(s.generation==1&&!s.sampled);
            f.first.release();f.second.reached();s=f.observer->snapshot();
            check(s.generation==1&&!s.sampled&&s.reason==DeviceChangeReason::reader_failure);
            f.second.release();wait([&]{return f.observer->snapshot().sampled;});
            check(f.observer->snapshot().generation==2);
        });
        test("capability invalidation and binding preserve operator configuration",[]{
            DeviceObservationSnapshot s{1,DevicePresence::present,DeviceChangeReason::initial,{},true};
            auto c=invalidated_drive_capabilities("/fake",7,s);
            DriveCapabilities fresh;fresh.device="/fake";fresh.vendor="SAME";fresh.model="SAME";
            fresh.c2_supported={Knowledge::yes,CapabilityEvidenceSource::drive_reported,"fresh report"};
            check(bind_drive_probe(c,fresh,1,s));
            check(c.read_offset_samples==7&&c.c2_supported.value==Knowledge::yes);
            auto selected=select_read_strategy(ReadPolicy{},CddaBackend::direct,true,true,c.c2_supported.value);
            check(!selected.pending&&selected.downgrade==ReadStrategyDowngrade::none);
            c.requested_speed_x=4;
            s.generation=2;s.reason=DeviceChangeReason::signature_changed;
            c=invalidated_drive_capabilities(c.device,c.read_offset_samples,s);
            check(c.vendor.empty()&&c.model.empty()&&!c.requested_speed_x&&c.read_offset_samples==7);
            check(c.c2_supported.value==Knowledge::unknown&&c.c2_trustworthy.value==Knowledge::unknown);
            check(!bind_drive_probe(c,fresh,1,s)&&c.c2_supported.value==Knowledge::unknown);
            selected=select_read_strategy(ReadPolicy{},CddaBackend::direct,true,false,c.c2_supported.value);
            check(selected.pending&&selected.downgrade==ReadStrategyDowngrade::c2_probe_pending);
            check(bind_drive_probe(c,fresh,2,s)&&c.c2_supported.value==Knowledge::yes);
            auto different=fresh;different.c2_supported.value=Knowledge::no;
            check(bind_drive_probe(c,different,2,s)&&c.c2_supported.value==Knowledge::no);
            selected=select_read_strategy(ReadPolicy{},CddaBackend::direct,true,true,c.c2_supported.value);
            check(!selected.pending&&selected.downgrade==ReadStrategyDowngrade::c2_unsupported);
            s.generation=3;s.presence=DevicePresence::unknown;
            check(!bind_drive_probe(c,fresh,3,s)&&c.c2_supported.value==Knowledge::unknown);
            check(c.read_offset_samples==7);
        });
        test("old in-flight PCM cannot cross an observation generation",[]{
            auto observer=std::make_shared<DeviceLifecycleObserver>([]{return DeviceObservation{DevicePresence::present,"a",{}};},std::chrono::milliseconds(1));
            wait([&]{return observer->snapshot().sampled;});const auto epoch=observer->snapshot().generation;
            auto accepted=std::make_shared<std::atomic<std::uint64_t>>(epoch);
            auto state=std::make_shared<ReaderState>();
            {
                PcmWorker worker([=]{const auto current=accepted->load();
                    return make_device_epoch_reader(std::make_unique<Reader>(state),[=]{return observer->permits(current);});},"fake",{15,15});
                ReleaseGate release{state->read};
                worker.set_disc_generation(1);worker.start(0,15);state->read.reached();
                observer->reader_failed();worker.cancel();worker.discard_reader();worker.set_disc_generation(2);
                state->read.release();wait([&]{return worker.device_released();});
                PcmBlock block;check(!worker.pop(block));
                check(worker.status().diagnostics.stats.frames_accepted==0);
                PlayerEvent event;check(!worker.pop_event(event));
                accepted->store(observer->snapshot().generation);worker.start(0,15);
                wait([&]{return worker.status().done;});check(worker.pop(block));
                check(block.evidence.device_generation==2&&block.evidence.disc_generation==2);
            }
            check(state->calls==2&&state->closed==2);
        });
        test("cancelled coordinator-waiting factory never opens",[]{
            auto access=std::make_shared<DriveAccessCoordinator>();Gate held;
            auto holder=std::async(std::launch::async,[&]{access->invoke([&]{held.pass();});});held.reached();
            auto state=std::make_shared<ReaderState>();state->read.release();std::atomic<unsigned> opens{0};
            PcmWorker worker([&]{++opens;return std::make_unique<Reader>(state);},"fake",{15,15},access);
            ReleaseGate release{held};worker.start(0,15);wait([&]{return !worker.device_released();});
            worker.cancel();worker.discard_reader();held.release();holder.get();wait([&]{return worker.device_released();});
            check(opens==0&&state->calls==0&&state->closed==0&&worker.status().error.empty());
        });
        test("shutdown close stays on owner and inside coordinator",[]{
            auto access=std::make_shared<DriveAccessCoordinator>();auto state=std::make_shared<ReaderState>();state->read.release();
            auto worker=std::make_unique<PcmWorker>([=]{return std::make_unique<Reader>(state);},"fake",PcmBufferConfig{15,15},access);
            worker->start(0,15);wait([&]{return worker->status().done;});worker->cancel();
            Gate held;auto holder=std::async(std::launch::async,[&]{access->invoke([&]{held.pass();});});held.reached();
            ReleaseGate release{held};std::promise<std::thread::id> beginning;auto began=beginning.get_future();
            auto closing=std::async(std::launch::async,[w=std::move(worker),&beginning]()mutable{
                beginning.set_value(std::this_thread::get_id());w.reset();});
            const auto destructor_thread=began.get();
            const bool waiting=closing.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout;
            const auto early=state->closed.load();held.release();holder.get();closing.get();
            check(waiting&&early==0&&state->closed==1);
            check(state->created_on==state->closed_on&&state->closed_on!=destructor_thread);
        });
        check(timeouts==0);
        std::cout<<"PASS: "<<cases<<" device lifecycle cases, "<<checks<<" checks (no real device I/O)\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
