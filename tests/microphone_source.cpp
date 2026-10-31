#include "../src/microphone_source.hpp"
#include <windows.h>
#include <atomic>
#include <deque>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>
namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
template<class F>void until(F fn,const char* why){const auto started=GetTickCount64();while(!fn()){need(GetTickCount64()-started<4000,why);Sleep(1);}}
struct Queued {std::array<float,480> samples;std::uint64_t start;};
struct Fake {
    std::mutex mutex;std::deque<Queued> packets;std::string active,default_id="a";
    bool b_present=true,denied=false,slow_open=false,slow_drain=false;HANDLE capture=nullptr;
    HANDLE entered=CreateEventW(nullptr,FALSE,FALSE,nullptr),released=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    std::atomic<unsigned> opens{0},closes{0};std::atomic<std::uint64_t> now{1000000};
    ~Fake(){CloseHandle(entered);CloseHandle(released);}
    void push(const std::array<float,480>& samples){std::lock_guard<std::mutex> lock(mutex);const auto end=now.fetch_add(10000)+10000;
        packets.push_back({samples,end-10000});if(capture)SetEvent(capture);}
    std::string selected(){std::lock_guard<std::mutex> lock(mutex);return active;}
};
class Backend final:public evrm::MicrophoneBackend {
    Fake& fake;
public:
    explicit Backend(Fake& f):fake(f){}
    std::shared_ptr<const evrm::MicrophoneDeviceList> enumerate()override{
        std::lock_guard<std::mutex> lock(fake.mutex);auto list=std::make_shared<evrm::MicrophoneDeviceList>();list->available=true;
        list->devices.push_back({"a","Input A"});if(fake.b_present)list->devices.push_back({"b","Input B"});return list;
    }
    std::string default_device()override{std::lock_guard<std::mutex> lock(fake.mutex);return fake.default_id;}
    evrm::MicrophoneState open(const std::string& id,void* event)noexcept override{
        bool slow=false,denied=false;{std::lock_guard<std::mutex> lock(fake.mutex);slow=fake.slow_open;denied=fake.denied;}
        ++fake.opens;if(slow){SetEvent(fake.entered);WaitForSingleObject(fake.released,4000);}
        if(denied)return evrm::MicrophoneState::AccessDenied;
        {std::lock_guard<std::mutex> lock(fake.mutex);fake.active=id;fake.capture=event;}return evrm::MicrophoneState::Ready;
    }
    bool drain(const evrm::MicrophonePacketSink& sink)noexcept override{
        for(unsigned n=0;n<32;++n){Queued packet;bool slow=false;
            {std::lock_guard<std::mutex> lock(fake.mutex);if(fake.packets.empty())return true;packet=fake.packets.front();fake.packets.pop_front();slow=fake.slow_drain;}
            if(slow){SetEvent(fake.entered);WaitForSingleObject(fake.released,4000);}
            sink.accept(sink.context,{packet.samples.data(),480,packet.start,false,false,false});
        }return true;
    }
    void close()noexcept override{std::lock_guard<std::mutex> lock(fake.mutex);if(fake.capture||!fake.active.empty())++fake.closes;fake.capture=nullptr;fake.active.clear();fake.packets.clear();}
    std::uint32_t native_error()const noexcept override{return 0;}
};
}
int main(int argc,char** argv){
    need(argc==2,"public input required");
    {evrm::MicrophoneSource unavailable([](void*)->std::unique_ptr<evrm::MicrophoneBackend>{throw std::runtime_error("backend factory fixture");});
        until([&]{return unavailable.state()==evrm::MicrophoneState::Unavailable;},"failed backend state");
        need(!unavailable.request(true,{},{}),"dead capture service accepted a request and claimed to start");}
    Fake fake;
    const evrm::VoiceTimeSource time{[](void* p)noexcept{return static_cast<Fake*>(p)->now.load();},&fake};
    evrm::MicrophoneSource mic([](void* p)->std::unique_ptr<evrm::MicrophoneBackend>{return std::make_unique<Backend>(*static_cast<Fake*>(p));},&fake,time,20);
    need(mic.ready(),"capture service thread unavailable");until([&]{return bool(mic.devices());},"device enumeration");
    need(fake.opens==0&&mic.state()==evrm::MicrophoneState::Off,"Off opened a microphone");
    need(mic.request(true,{.71f,27},{}),"enable default microphone");until([&]{return mic.state()==evrm::MicrophoneState::Ready;},"default microphone start");
    need(fake.selected()=="a","Windows default was not used");
    std::ifstream input(argv[1],std::ios::binary);std::array<float,480> samples{};float peak=0;unsigned blocks=0;
    while(blocks<400&&input.read(reinterpret_cast<char*>(samples.data()),sizeof samples)){
        const auto before=mic.stats().processed;fake.push(samples);until([&]{return mic.stats().processed>before;},"public microphone packet stalled");
        for(float weight:mic.snapshot().weights)peak=std::max(peak,weight);
        ++blocks;
    }
    need(blocks==400&&peak>.2f&&mic.stats().dropped==0,"public speech did not traverse actual microphone worker");
    const auto b=std::make_shared<const std::string>("b");need(mic.request(true,{.71f,27},b),"select B");
    for(float weight:mic.snapshot().weights)need(weight==0,"old device pose survived selection");
    until([&]{return mic.state()==evrm::MicrophoneState::Ready&&fake.selected()=="b";},"specific device start");
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.b_present=false;}mic.refresh();
    until([&]{return mic.state()==evrm::MicrophoneState::NoDevice;},"removed device not reported");need(fake.selected().empty(),"missing chosen device silently changed input");
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.b_present=true;fake.denied=true;}mic.refresh();
    until([&]{return mic.state()==evrm::MicrophoneState::AccessDenied;},"access denied not reported");
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.denied=false;}mic.refresh();
    until([&]{return mic.state()==evrm::MicrophoneState::Ready;},"permission recovery");
    need(mic.request(true,{.71f,27},{}),"restore Windows default");until([&]{return mic.state()==evrm::MicrophoneState::Ready&&fake.selected()=="a";},"default A restore");
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.default_id="b";}mic.refresh();
    until([&]{return mic.state()==evrm::MicrophoneState::Ready&&fake.selected()=="b";},"changed Windows default not followed");
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.slow_drain=true;}
    const auto submitted=mic.stats().submitted;fake.push(samples);need(WaitForSingleObject(fake.entered,4000)==WAIT_OBJECT_0,"delayed read entered");
    need(mic.request(false,{.71f,27},{}),"Off during read");need(mic.snapshot().activity==evrm::VoiceActivity::Off,"Off did not clear mouth immediately");
    SetEvent(fake.released);until([&]{return fake.selected().empty();},"Off did not close capture");
    need(mic.stats().submitted==submitted,"late read admitted old microphone samples after Off");
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.slow_drain=false;fake.slow_open=true;}
    need(mic.request(true,{.71f,27},{}),"delayed open request");need(WaitForSingleObject(fake.entered,4000)==WAIT_OBJECT_0,"delayed open entered");
    const auto closed_before_cancel=fake.closes.load();
    need(mic.request(false,{.71f,27},{}),"Off during open");SetEvent(fake.released);
    until([&]{return fake.closes>closed_before_cancel&&fake.selected().empty()&&!mic.stats().enabled;},"late open was not cancelled");
    {std::lock_guard<std::mutex> lock(fake.mutex);fake.slow_open=false;}
    need(mic.request(true,{.71f,27},{}),"resume after cancellation");until([&]{return mic.state()==evrm::MicrophoneState::Ready;},"cancellation recovery");
    need(mic.request(false,{.71f,27},{}),"final Off");until([&]{return fake.selected().empty();},"final close");
    std::printf("PASS fake-device selection/default changes, removal/denial/recovery, delayed open/read cancellation; %u public blocks peak %.3f; no real microphone opened\n",blocks,peak);
}
