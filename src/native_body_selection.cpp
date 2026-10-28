#include "native_body_selection.hpp"
#include <algorithm>
#include <stdexcept>
namespace evrm {
bool make_native_menu_body_mask(const NativeMenuOwnerCandidate& menu,
    const std::vector<std::array<std::uint32_t,4>>& parts,
    const std::vector<std::array<std::uint32_t,4>>& candidates,
    const NativeBufferBinding& buffer,NativeBodyMask& out,
    std::vector<std::array<std::uint32_t,4>>& qualified,std::string& reason)noexcept{
    out={};qualified.clear();reason.clear();
    try{
        const auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
        require(menu.client&&menu.controller&&!menu.guards.empty(),"menu context missing");
        require(menu.kind==NativeMenuOwnerKind::frontend?bool(menu.scene_owner):
            menu.kind==NativeMenuOwnerKind::character&&menu.renderer,"menu kind/renderer missing");
        require(buffer.info.buffer&&buffer.set&&buffer.generation&&buffer.set_generation&&buffer.set_version&&
            !buffer.info.offset&&buffer.info.range==buffer.size&&buffer.size&&buffer.size%128==0&&
            buffer.size<=262144ull*128&&(buffer.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),"menu instance source invalid");
        if(menu.kind==NativeMenuOwnerKind::character){
            const auto& owner=menu.preview;
            require(owner.native_buffer==reinterpret_cast<std::uintptr_t>(buffer.info.buffer)&&
                owner.native_buffer_bytes==buffer.size&&owner.instance_capacity&&owner.instance_capacity<=131072&&
                std::uint64_t(owner.instance_capacity)+owner.instance_extra<=262144&&
                owner.native_buffer_bytes==(std::uint64_t(owner.instance_capacity)+owner.instance_extra)*128&&
                !owner.models.empty()&&owner.models.size()<=112,"Character allocation/source mismatch");
            for(const auto& model:owner.models)
                require(model.index<owner.instance_capacity&&model.generation<=255&&
                    model.handle==((model.generation<<24)|model.index)&&model.record,"Character model identity invalid");
            for(const auto& part:parts)require(std::any_of(owner.models.begin(),owner.models.end(),
                [&](const auto& model){return model.index==part[0];}),"part is not owned by Character renderer");
        }
        require(!parts.empty()&&parts.size()<=64&&!candidates.empty()&&candidates.size()<=64,"menu part selection missing or oversized");
        for(std::size_t i=0;i<candidates.size();++i){
            require(candidates[i][0]<131072&&candidates[i][0]<buffer.size/128,"menu qualifier outside current source");
            for(std::size_t j=0;j<i;++j)require(candidates[j][0]!=candidates[i][0],"duplicate menu qualifier");
        }
        NativeBodyMask mask;std::vector<std::array<std::uint32_t,4>> selected;selected.reserve(parts.size());
        for(const auto& part:parts){
            require(part[0]<131072&&part[0]<buffer.size/128,"menu part outside current source");
            const auto found=std::find(candidates.begin(),candidates.end(),part);
            require(found!=candidates.end(),"menu part identity changed or missing");
            auto& word=mask.words[part[0]>>5];const auto bit=std::uint32_t(1)<<(part[0]&31);
            require(!(word&bit),"duplicate menu part");word|=bit;++mask.selected;selected.push_back(part);
        }
        out=mask;qualified=std::move(selected);return true;
    }catch(const std::exception& error){try{reason=error.what();}catch(...){}}catch(...){try{reason="menu selection allocation/internal failure";}catch(...){}}
    out={};qualified.clear();return false;
}
bool make_native_body_mask(const NativeOwnerCandidate& owner,
    const std::vector<std::array<std::uint32_t,4>>& parts,const NativeBufferBinding& buffer,
    NativeBodyMask& out,std::string& reason) noexcept {
    out={};reason.clear();
    try {
        const auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
        require(!owner.guards.empty()&&!owner.models.empty()&&owner.models.size()<=24,"owner model observation missing or oversized");
        require(owner.instance_capacity&&owner.instance_capacity<=131072&&owner.instance_extra<=131072-owner.instance_capacity&&
            owner.native_buffer_bytes==(std::uint64_t(owner.instance_capacity)+owner.instance_extra)*128,"owner instance extent mismatch");
        require(owner.native_buffer&&buffer.info.buffer&&reinterpret_cast<std::uintptr_t>(buffer.info.buffer)==owner.native_buffer&&
            buffer.generation&&buffer.set&&buffer.set_generation&&buffer.set_version&&buffer.info.offset==0&&
            buffer.info.range==owner.native_buffer_bytes&&buffer.size==owner.native_buffer_bytes&&
            (buffer.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),"current native instance buffer mismatch");
        require(!parts.empty()&&parts.size()<=64,"retarget part list missing or oversized");
        NativeBodyMask mask;std::array<bool,24> equipment{};
        for(const auto& model:owner.models) {
            require(model.equipment_slot<24&&!equipment[model.equipment_slot]&&model.index<owner.instance_capacity&&
                model.generation<=255&&model.handle==((model.generation<<24)|model.index)&&model.record,
                "owner model identity mismatch");
            equipment[model.equipment_slot]=true;
            const auto bit=std::uint32_t(1)<<(model.index&31);auto& word=mask.words[model.index>>5];
            require(!(word&bit),"duplicate owned model index");word|=bit;++mask.selected;
        }
        for(std::size_t i=0;i<parts.size();++i) {
            const auto index=parts[i][0];require(index<131072&&(mask.words[index>>5]&(std::uint32_t(1)<<(index&31))),"retarget part belongs to a different owner");
            for(std::size_t j=0;j<i;++j)require(parts[j][0]!=index,"duplicate retarget part");
        }
        out=mask;return true;
    }catch(const std::exception& error){try{reason=error.what();}catch(...){}}catch(...){try{reason="body selection unavailable";}catch(...){}}
    return false;
}
}
