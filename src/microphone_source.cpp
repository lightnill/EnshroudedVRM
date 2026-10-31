#include "microphone_source.hpp"
#include "runtime_log.hpp"
#include <windows.h>
#include <mutex>
#include <algorithm>
namespace evrm {
struct MicrophoneSource::Impl {
    struct Request {bool enabled=false;VoiceAnalysisTuning tuning{};MicrophoneDeviceId device;std::uint64_t serial=1;};
    std::shared_ptr<const Request> request_=std::make_shared<const Request>();
    std::shared_ptr<const MicrophoneDeviceList> devices_;
    std::mutex changes;
    std::atomic<bool> stopping{false},refresh_requested{true},admission{false},alive{true};
    std::atomic<MicrophoneState> state_{MicrophoneState::Off};
    std::atomic<std::uint64_t> active_serial{0},last_poll{0},discarded{0};
    std::atomic<std::uint32_t> native_error{0};
    std::atomic<VoiceAnalysisWorker*> worker_pointer{nullptr};
    std::unique_ptr<VoiceAnalysisWorker> worker;
    MicrophoneAudioAssembler assembler;
    MicrophoneBackendFactory factory;void* factory_context;VoiceTimeSource time;VoiceClock clock;unsigned refresh_ms;
    HANDLE control=nullptr,capture=nullptr,thread=nullptr;
    MicrophoneState logged_state=MicrophoneState::Off;
    std::uint64_t processing_serial=0;
    Impl(MicrophoneBackendFactory fn,void* context,VoiceTimeSource input,unsigned interval):factory(fn?fn:make_wasapi_microphone_backend),factory_context(context),time(input),refresh_ms(std::clamp(interval,20u,10000u)){
        control=CreateEventW(nullptr,FALSE,FALSE,nullptr);capture=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(control&&capture)thread=CreateThread(nullptr,0,[](void* raw)->DWORD{static_cast<Impl*>(raw)->run();return 0;},this,0,nullptr);
    }
    ~Impl(){stopping=true;if(control)SetEvent(control);if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);}if(capture)CloseHandle(capture);if(control)CloseHandle(control);}
    std::shared_ptr<const Request> requested()const noexcept{return std::atomic_load_explicit(&request_,std::memory_order_acquire);}
    bool wanted(std::uint64_t serial)const noexcept{const auto value=requested();return !stopping.load()&&admission.load(std::memory_order_acquire)&&value->enabled&&value->serial==serial;}
    std::uint64_t now_us()const noexcept{return time.now_us?time.now_us(time.context):clock.now_us();}
    void state(MicrophoneState value,std::uint64_t serial){
        if(requested()->serial!=serial)return;
        state_.store(value,std::memory_order_release);
        if(value!=logged_state){logged_state=value;log_event_lazy([&]{const auto request=requested();return "voice-microphone.state status="+std::to_string(unsigned(value))+
            " selection="+std::string(request->device&&!request->device->empty()?"specific":"windows-default")+
            " generation="+std::to_string(serial)+" native-error="+std::to_string(native_error.load())+" samples-not-recorded=true";});}
    }
    void packet(const MicrophonePacket& packet)noexcept{
        if(!wanted(processing_serial)||!worker)return;
        const MicrophoneAudioSink sink{this,[](void* raw)noexcept{auto& self=*static_cast<Impl*>(raw);
            if(self.wanted(self.processing_serial)&&self.worker){self.worker->set_enabled(false);self.worker->set_enabled(true);}},
            [](void* raw,const std::array<float,480>& samples,std::uint64_t timestamp)noexcept{auto& self=*static_cast<Impl*>(raw);
                if(self.wanted(self.processing_serial)&&self.worker)self.worker->submit(samples,1,timestamp);}};
        assembler.push(packet,now_us(),sink);discarded.store(assembler.discarded_blocks(),std::memory_order_relaxed);
    }
    void run()noexcept{
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        try{
            auto backend=factory(factory_context);std::string active_device,selected_device;
            std::uint64_t opened_serial=0,resolved_serial=0,next_refresh=0,retry_at=0;
            const auto close=[&]{active_serial=0;if(worker)worker->set_enabled(false);assembler.clear();if(backend)backend->close();active_device.clear();opened_serial=0;ResetEvent(capture);};
            while(!stopping.load(std::memory_order_acquire)){
                auto request=requested();const auto now=GetTickCount64();
                const bool refresh=refresh_requested.exchange(false)||!next_refresh||now>=next_refresh;
                if(refresh){
                    auto list=backend?backend->enumerate():std::make_shared<const MicrophoneDeviceList>();
                    std::atomic_store_explicit(&devices_,std::move(list),std::memory_order_release);next_refresh=now+refresh_ms;
                }
                if(!request->enabled||!admission.load(std::memory_order_acquire)){close();state(MicrophoneState::Off,request->serial);retry_at=0;}
                else{
                    const auto list=std::atomic_load_explicit(&devices_,std::memory_order_acquire);
                    if(refresh||resolved_serial!=request->serial){
                        if(resolved_serial!=request->serial)retry_at=0;
                        selected_device=request->device?*request->device:std::string{};
                        if(selected_device.empty()&&backend)selected_device=backend->default_device();
                        resolved_serial=request->serial;
                    }
                    const auto& selected=selected_device;
                    const bool exists=list&&list->available&&std::any_of(list->devices.begin(),list->devices.end(),[&](const auto& device){return device.id==selected;});
                    if(opened_serial&&(opened_serial!=request->serial||selected!=active_device||!exists))close();
                    if(!exists){close();state(list&&list->available?MicrophoneState::NoDevice:MicrophoneState::Unavailable,request->serial);retry_at=0;}
                    else if(!opened_serial&&(refresh||!retry_at||now>=retry_at)){
                        state(MicrophoneState::Starting,request->serial);
                        if(!worker){worker=std::make_unique<VoiceAnalysisWorker>(nullptr,time);worker_pointer.store(worker.get(),std::memory_order_release);}
                        if(!wanted(request->serial)){close();continue;}
                        auto result=worker->ready()&&backend?backend->open(selected,capture):MicrophoneState::Unavailable;
                        native_error=backend?backend->native_error():0;retry_at=now+2000;
                        if(!wanted(request->serial)){close();continue;}
                        if(result==MicrophoneState::Ready){assembler.clear();worker->set_tuning(request->tuning);worker->set_enabled(true);
                            processing_serial=opened_serial=request->serial;active_device=selected;active_serial.store(opened_serial,std::memory_order_release);}
                        else close();
                        state(result,request->serial);
                    }
                    if(worker)worker->set_tuning(request->tuning);
                }
                if(stopping.load())break;
                const auto after=GetTickCount64();const auto timeout=DWORD(std::min<std::uint64_t>(refresh_ms,next_refresh>after?next_refresh-after:1));
                HANDLE waits[]={control,capture};const auto waited=WaitForMultipleObjects(2,waits,FALSE,timeout);
                if(waited==WAIT_FAILED)break;
                if(waited==WAIT_OBJECT_0+1&&opened_serial&&wanted(opened_serial)){
                    const MicrophonePacketSink sink{this,[](void* raw,const MicrophonePacket& packet)noexcept{static_cast<Impl*>(raw)->packet(packet);}};
                    if(!backend->drain(sink)){native_error=backend->native_error();close();state(MicrophoneState::Unavailable,request->serial);retry_at=GetTickCount64()+2000;}
                }
            }
            close();
        }catch(...){active_serial=0;if(worker)worker->set_enabled(false);alive=false;state_.store(MicrophoneState::Unavailable);log_event("voice-microphone.unavailable reason=capture-thread");}
        alive.store(false,std::memory_order_release);
    }
};
MicrophoneSource::MicrophoneSource(MicrophoneBackendFactory factory,void* context,VoiceTimeSource time,unsigned interval)noexcept{
    try{impl_=std::make_unique<Impl>(factory,context,time,interval);}catch(...){}
}
MicrophoneSource::~MicrophoneSource()=default;
bool MicrophoneSource::ready()const noexcept{return impl_&&impl_->thread&&impl_->alive.load(std::memory_order_acquire);}
bool MicrophoneSource::request(bool enabled,VoiceAnalysisTuning tuning,MicrophoneDeviceId device)noexcept{
    if(!ready())return false;
    if(!enabled){impl_->admission.store(false,std::memory_order_release);impl_->state_=MicrophoneState::Off;SetEvent(impl_->control);}
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto old=impl_->requested();const auto id=device?*device:std::string{};
        if(id.size()>1024||id.find('\0')!=std::string::npos)return false;
        const bool changed=old->enabled!=enabled||(old->device?*old->device:std::string{})!=id;
        if(changed&&old->serial==UINT64_MAX)return false;
        if(changed)impl_->admission.store(false,std::memory_order_release);
        auto next=std::make_shared<const Impl::Request>(Impl::Request{enabled,tuning,std::move(device),old->serial+std::uint64_t(changed)});
        std::atomic_store_explicit(&impl_->request_,std::move(next),std::memory_order_release);
        if(changed)impl_->state_.store(enabled?MicrophoneState::Starting:MicrophoneState::Off,std::memory_order_release);
        impl_->admission.store(enabled,std::memory_order_release);
        SetEvent(impl_->control);return true;
    }catch(...){impl_->admission=false;SetEvent(impl_->control);return false;}
}
void MicrophoneSource::refresh()noexcept{if(ready()){impl_->refresh_requested=true;SetEvent(impl_->control);}}
MicrophoneState MicrophoneSource::state()const noexcept{return ready()?impl_->state_.load(std::memory_order_acquire):MicrophoneState::Unavailable;}
std::shared_ptr<const MicrophoneDeviceList> MicrophoneSource::devices()const noexcept{return impl_?std::atomic_load_explicit(&impl_->devices_,std::memory_order_acquire):nullptr;}
MouthSnapshot MicrophoneSource::snapshot()const noexcept{
    MouthSnapshot out;if(!ready()){out.activity=VoiceActivity::UnsupportedSource;return out;}
    const auto request=impl_->requested();if(!request->enabled||!impl_->admission.load(std::memory_order_acquire))return out;
    const auto status=state();out.activity=status==MicrophoneState::Starting?VoiceActivity::Waiting:VoiceActivity::UnsupportedSource;
    if(status!=MicrophoneState::Ready||impl_->active_serial.load(std::memory_order_acquire)!=request->serial)return out;
    if(auto* worker=impl_->worker_pointer.load(std::memory_order_acquire))out=worker->snapshot();
    const auto after=impl_->requested();if(!after->enabled||!impl_->admission.load()||after->serial!=request->serial||impl_->active_serial.load()!=request->serial)return {};
    return out;
}
VoiceWorkerStats MicrophoneSource::stats()const noexcept{if(impl_)if(auto* worker=impl_->worker_pointer.load(std::memory_order_acquire))return worker->stats();return {};}
void MicrophoneSource::poll(std::uint64_t now)noexcept{
    if(!ready()||!impl_->requested()->enabled)return;
    auto previous=impl_->last_poll.load();if(now>=previous&&now-previous<1000)return;
    if(!impl_->last_poll.compare_exchange_strong(previous,now))return;
    log_event_lazy([&]{const auto value=stats();const auto mouth=snapshot();std::string weights;
        for(float weight:mouth.weights){if(!weights.empty())weights+=',';weights+=std::to_string(weight);}
        return "voice-microphone.counts submitted="+std::to_string(value.submitted)+" processed="+std::to_string(value.processed)+
        " discarded="+std::to_string(value.discarded)+" input-discarded="+std::to_string(impl_->discarded.load())+" dropped="+std::to_string(value.dropped)+
        " resets="+std::to_string(value.resets)+" epoch="+std::to_string(value.epoch)+" analysis-ns="+std::to_string(value.analysis_nanoseconds)+
        " state="+std::to_string(unsigned(state()))+" native-error="+std::to_string(impl_->native_error.load())+
        " levels-valid="+std::to_string(value.diagnostics.valid)+" rms-db="+std::to_string(value.diagnostics.rms_db)+
        " flatness="+std::to_string(value.diagnostics.flatness)+" activation="+std::to_string(value.diagnostics.activation)+
        " activity="+std::to_string(unsigned(mouth.activity))+" weights="+weights;});
}
}
