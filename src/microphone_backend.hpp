#pragma once
#include "microphone_audio.hpp"
#include "voice_input_types.hpp"
#include <memory>
namespace evrm {
struct MicrophonePacketSink {void* context=nullptr;void (*accept)(void*,const MicrophonePacket&)noexcept=nullptr;};
class MicrophoneBackend {
public:
    virtual ~MicrophoneBackend()=default;
    virtual std::shared_ptr<const MicrophoneDeviceList> enumerate()=0;
    virtual std::string default_device()=0;
    virtual MicrophoneState open(const std::string& device,void* capture_event)noexcept=0;
    virtual bool drain(const MicrophonePacketSink&)noexcept=0;
    virtual void close()noexcept=0;
    virtual std::uint32_t native_error()const noexcept=0;
};
using MicrophoneBackendFactory=std::unique_ptr<MicrophoneBackend>(*)(void*);
std::unique_ptr<MicrophoneBackend> make_wasapi_microphone_backend(void*);
}
