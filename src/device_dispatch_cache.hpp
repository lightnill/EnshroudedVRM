#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace evrm {
// Initialize exclusively before publishing its DeviceState. Thereafter this
// owned table is immutable; hot readers allocate nothing and take no mutex.
class DeviceDispatchCache {
    struct Entry {std::string name;PFN_vkVoidFunction function=nullptr;};
    std::vector<Entry> entries_;
    VkDevice device_{};PFN_vkGetDeviceProcAddr resolver_=nullptr;bool ready_=false;
public:
    bool seed(VkDevice device,PFN_vkGetDeviceProcAddr resolver,const char*const* names,std::size_t count)noexcept {
        if(ready_||!device||!resolver||!names||!count||count>1024)return false;
        try{
            std::vector<Entry> next;next.reserve(count);
            for(std::size_t n=0;n<count;++n){
                if(!names[n])return false;
                std::size_t length=0;while(length<256&&names[n][length])++length;
                if(!length||length==256)return false;
                next.push_back({std::string(names[n],length),nullptr});
            }
            std::sort(next.begin(),next.end(),[](const Entry& a,const Entry& b){return a.name<b.name;});
            next.erase(std::unique(next.begin(),next.end(),[](const Entry& a,const Entry& b){return a.name==b.name;}),next.end());
            for(auto& entry:next)entry.function=resolver(device,entry.name.c_str());
            entries_=std::move(next);device_=device;resolver_=resolver;ready_=true;return true;
        }catch(...){return false;} // Optional acceleration cannot veto startup.
    }
    bool ready()const noexcept {return ready_;}
    std::size_t size()const noexcept {return entries_.size();}
    PFN_vkVoidFunction lookup(VkDevice device,PFN_vkGetDeviceProcAddr resolver,const char* name)const {
        if(!device||!resolver||!name)return nullptr;
        if(ready_&&device==device_&&resolver==resolver_){
            const std::string_view key(name);
            const auto found=std::lower_bound(entries_.begin(),entries_.end(),key,
                [](const Entry& entry,std::string_view value){return std::string_view(entry.name)<value;});
            // A known null is a complete answer, not a reason to retry.
            if(found!=entries_.end()&&std::string_view(found->name)==key)return found->function;
        }
        return resolver(device,name);
    }
};
}
