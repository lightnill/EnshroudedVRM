#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "microphone_backend.hpp"
#include "voice_clock.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdexcept>
namespace evrm {
namespace {
template<class T>struct Com {
    T* value=nullptr;~Com(){reset();}
    void reset()noexcept{if(value)value->Release();value=nullptr;}
    T** put()noexcept{reset();return &value;}
    T* operator->()const noexcept{return value;}
    explicit operator bool()const noexcept{return value!=nullptr;}
};
std::string utf8(const wchar_t* text){
    if(!text)return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);
    if(count<=1||count>4096)return {};
    std::string result(std::size_t(count),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,result.data(),count,nullptr,nullptr))return {};
    result.pop_back();return result;
}
std::wstring wide(const std::string& text){
    if(text.empty()||text.size()>1024||text.find('\0')!=std::string::npos)return {};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(count<=0)return {};
    std::wstring result(std::size_t(count),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),count))return {};
    return result;
}
std::string device_id(IMMDevice* device){
    wchar_t* raw=nullptr;if(FAILED(device->GetId(&raw)))return {};
    struct Release {wchar_t* value;~Release(){CoTaskMemFree(value);}} release{raw};return utf8(raw);
}
class WasapiMicrophone final:public MicrophoneBackend {
    bool com_=false,started_=false;
    HRESULT error_=S_OK;
    Com<IMMDeviceEnumerator> enumerator_;
    Com<IMMDevice> endpoint_;
    Com<IAudioClient> client_;
    Com<IAudioCaptureClient> capture_;
    VoiceClock clock_;
    MicrophoneState failed(HRESULT error)noexcept{
        error_=error;close();
        if(error==E_ACCESSDENIED)return MicrophoneState::AccessDenied;
        if(error==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)||error==AUDCLNT_E_DEVICE_INVALIDATED)return MicrophoneState::NoDevice;
        return MicrophoneState::Unavailable;
    }
public:
    WasapiMicrophone(){
        error_=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);com_=SUCCEEDED(error_);
        if(com_)error_=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(enumerator_.put()));
    }
    ~WasapiMicrophone()override{close();enumerator_.reset();if(com_)CoUninitialize();}
    std::shared_ptr<const MicrophoneDeviceList> enumerate()override{
        auto list=std::make_shared<MicrophoneDeviceList>();if(!enumerator_)return list;
        Com<IMMDeviceCollection> collection;error_=enumerator_->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,collection.put());
        if(FAILED(error_))return list;
        UINT count=0;if(FAILED(collection->GetCount(&count)))return list;
        for(UINT n=0;n<count&&n<64;++n){Com<IMMDevice> device;if(FAILED(collection->Item(n,device.put())))continue;
            auto id=device_id(device.value);if(id.empty()||id.size()>1024)continue;
            std::string name="Microphone";Com<IPropertyStore> properties;
            if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,properties.put()))){PROPVARIANT value;PropVariantInit(&value);
                if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value))&&value.vt==VT_LPWSTR){auto label=utf8(value.pwszVal);if(!label.empty())name=std::move(label);}
                PropVariantClear(&value);}
            list->devices.push_back({std::move(id),std::move(name)});
        }
        list->available=true;return list;
    }
    std::string default_device()override{
        if(!enumerator_)return {};
        Com<IMMDevice> device;error_=enumerator_->GetDefaultAudioEndpoint(eCapture,eConsole,device.put());
        return SUCCEEDED(error_)?device_id(device.value):std::string{};
    }
    MicrophoneState open(const std::string& id,void* event)noexcept override{
        close();try{
            if(!enumerator_||!clock_.valid())return MicrophoneState::Unavailable;
            const auto name=wide(id);if(name.empty())return failed(E_INVALIDARG);
            auto result=enumerator_->GetDevice(name.c_str(),endpoint_.put());if(FAILED(result))return failed(result);
            result=endpoint_->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(client_.put()));if(FAILED(result))return failed(result);
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=1;format.nSamplesPerSec=48000;
            format.wBitsPerSample=32;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
            result=client_->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                100000,0,&format,nullptr);if(FAILED(result))return failed(result);
            result=client_->SetEventHandle(static_cast<HANDLE>(event));if(FAILED(result))return failed(result);
            result=client_->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void**>(capture_.put()));if(FAILED(result))return failed(result);
            result=client_->Start();if(FAILED(result))return failed(result);
            started_=true;error_=S_OK;return MicrophoneState::Ready;
        }catch(...){return failed(E_UNEXPECTED);}
    }
    bool drain(const MicrophonePacketSink& sink)noexcept override{
        if(!capture_||!sink.accept)return false;
        for(unsigned packet=0;packet<32;++packet){UINT available=0;auto result=capture_->GetNextPacketSize(&available);
            if(FAILED(result)){error_=result;return false;}if(!available)return true;
            BYTE* bytes=nullptr;UINT frames=0;DWORD flags=0;UINT64 position=0,qpc=0;
            result=capture_->GetBuffer(&bytes,&frames,&flags,&position,&qpc);if(FAILED(result)){error_=result;return false;}
            if(frames>48000){capture_->ReleaseBuffer(frames);error_=E_UNEXPECTED;return false;}
            const bool uncertain=(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)||!qpc;
            const auto now=clock_.now_us(),duration=std::uint64_t(frames)*1000000/48000;
            const auto start=uncertain?(now>duration?now-duration:0):qpc/10;
            sink.accept(sink.context,{reinterpret_cast<const float*>(bytes),frames,start,bool(flags&AUDCLNT_BUFFERFLAGS_SILENT),
                bool(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY),uncertain});
            result=capture_->ReleaseBuffer(frames);if(FAILED(result)){error_=result;return false;}
        }
        return true;
    }
    void close()noexcept override{
        if(started_&&client_)client_->Stop();
        started_=false;capture_.reset();client_.reset();endpoint_.reset();
    }
    std::uint32_t native_error()const noexcept override{return static_cast<std::uint32_t>(error_);}
};
}
std::unique_ptr<MicrophoneBackend> make_wasapi_microphone_backend(void*){return std::make_unique<WasapiMicrophone>();}
}
