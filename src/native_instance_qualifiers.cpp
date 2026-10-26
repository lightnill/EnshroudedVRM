#include "native_instance_qualifiers.hpp"
#include <cstring>
#include <limits>

namespace evrm { namespace {
using Q=NativeInstanceQualifier;using V=std::vector<Q>;
constexpr std::size_t stride=128,record_cap=131072,qualifier_cap=64;
struct Rejected {const char* why;};
void need(bool ok,const char* why){if(!ok)throw Rejected{why};}
void reason(std::string& out,const char* why)noexcept{try{out=why;}catch(...){out.clear();}}
std::uint32_t word(const std::uint8_t* record,std::size_t offset){std::uint32_t value;std::memcpy(&value,record+offset,4);return value;}
Q qualifier(const std::uint8_t* record,std::uint32_t id){return {id,word(record,120),word(record,108),word(record,112)};}
const Q* find(const V& values,std::uint32_t id){for(const auto& q:values)if(q[0]==id)return &q;return nullptr;}
void unique(const V& values,std::size_t bound,const char* why){
    need(!values.empty()&&values.size()<=qualifier_cap,why);
    for(std::size_t i=0;i<values.size();++i){need(values[i][0]<bound,why);for(std::size_t j=0;j<i;++j)need(values[i][0]!=values[j][0],why);}
}
bool owned(const NativeOwnerCandidate& owner,std::uint32_t id){for(const auto& m:owner.models)if(m.index==id)return true;return false;}
} // namespace

bool collect_native_instance_qualifiers(const void* bytes,std::size_t captured,const V& selected,V& out,std::string& why)noexcept{
    why.clear();
    try{
        need(bytes&&captured&&captured%stride==0&&captured<=record_cap*stride,"invalid zero-based native128 prefix");
        need(reinterpret_cast<std::uintptr_t>(bytes)<=std::numeric_limits<std::uintptr_t>::max()-captured,"native128 prefix pointer wraps");
        const auto count=captured/stride;unique(selected,count,"invalid selected qualifier count/ID/duplicate");
        const auto* base=static_cast<const std::uint8_t*>(bytes);const auto* first=base+std::size_t(selected.front()[0])*stride;
        for(const auto& q:selected){const auto* record=base+std::size_t(q[0])*stride;
            need(!(word(record,44)&1u)&&qualifier(record,q[0])==q,"selected instance inactive or identity stamp changed");
            need(std::memcmp(record,first,32)==0,"selected instances do not share exact transform bytes");}
        V result;result.reserve(qualifier_cap);result.insert(result.end(),selected.begin(),selected.end());
        for(std::size_t id=0;id<count;++id){const auto* record=base+id*stride;
            if(std::memcmp(record,first,32)!=0||(word(record,44)&1u)||find(selected,static_cast<std::uint32_t>(id)))continue;
            need(result.size()<qualifier_cap,"same-transform qualifier limit exceeded");result.push_back(qualifier(record,static_cast<std::uint32_t>(id)));
        }
        out.swap(result);return true;
    }catch(const Rejected& error){reason(why,error.why);}catch(...){reason(why,"instance qualifier allocation/internal failure");}
    out.clear();return false;
}
bool select_native_owner_qualifiers(const NativeOwnerCandidate& owner,const V& selected,const V& candidates,V& out,std::string& why)noexcept{
    why.clear();
    try{
        need(owner.instance_capacity&&owner.instance_capacity<=record_cap&&!owner.models.empty()&&owner.models.size()<=24,"invalid trusted owner model extent");
        for(std::size_t i=0;i<owner.models.size();++i){need(owner.models[i].index<owner.instance_capacity,"owner model ID outside capacity");for(std::size_t j=0;j<i;++j)need(owner.models[i].index!=owner.models[j].index,"duplicate owner model ID");}
        unique(selected,record_cap,"invalid selected qualifier count/ID/duplicate");unique(candidates,record_cap,"invalid candidate qualifier count/ID/duplicate");
        V result;result.reserve(owner.models.size());
        for(const auto& q:selected){const auto* current=find(candidates,q[0]);need(current&&*current==q&&owned(owner,q[0]),"selected qualifier missing/mismatched or belongs to another owner");result.push_back(q);}
        for(const auto& model:owner.models){if(find(selected,model.index))continue;const auto* current=find(candidates,model.index);need(current,"owned model has no current qualifier");result.push_back(*current);}
        out.swap(result);return true;
    }catch(const Rejected& error){reason(why,error.why);}catch(...){reason(why,"owner qualifier allocation/internal failure");}
    out.clear();return false;
}
}
