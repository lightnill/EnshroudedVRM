#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
namespace evrm {
struct QueueDispatch {
    std::shared_ptr<std::recursive_mutex> mutex;
    std::uint32_t family;
    std::unique_lock<std::recursive_mutex> guard;
    template<class MutexLookup,class FamilyLookup>
    QueueDispatch(MutexLookup lookup_mutex,FamilyLookup lookup_family)
        :mutex(lookup_mutex()),family(lookup_family()),guard(*mutex){}
};
}
