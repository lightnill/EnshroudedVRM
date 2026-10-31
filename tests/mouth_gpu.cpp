#include "native_skinning_fixture.hpp"
#include "../src/mouth_deformation.hpp"
#include <map>
namespace {
std::map<VkBuffer,VkDeviceMemory> memory_for_buffer;
std::map<VkDeviceMemory,void*> mapped_memory;
VkDescriptorBufferInfo last_source{},last_uniform{};
VkResult VKAPI_CALL track_bind(VkDevice device,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset){
    const auto result=reinterpret_cast<PFN_vkBindBufferMemory>(native_get(device,"vkBindBufferMemory"))(device,buffer,memory,offset);
    if(result==VK_SUCCESS){require(offset==0,"fixture requires private zero-offset allocation");memory_for_buffer[buffer]=memory;}return result;
}
VkResult VKAPI_CALL track_map(VkDevice device,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** output){
    const auto result=reinterpret_cast<PFN_vkMapMemory>(native_get(device,"vkMapMemory"))(device,memory,offset,size,flags,output);
    if(result==VK_SUCCESS)mapped_memory[memory]=*output;
    return result;
}
void VKAPI_CALL track_descriptors(VkDevice device,std::uint32_t count,const VkWriteDescriptorSet* writes,std::uint32_t copies,const VkCopyDescriptorSet* copy){
    reinterpret_cast<PFN_vkUpdateDescriptorSets>(native_get(device,"vkUpdateDescriptorSets"))(device,count,writes,copies,copy);
    for(unsigned n=0;n<count;++n)if(writes[n].pBufferInfo){if(writes[n].dstBinding==0)last_source=*writes[n].pBufferInfo;
        if(writes[n].dstBinding==7)last_uniform=*writes[n].pBufferInfo;}
}
PFN_vkVoidFunction VKAPI_CALL proof_get(VkDevice device,const char* name){
    if(std::strcmp(name,"vkBindBufferMemory")==0)return reinterpret_cast<PFN_vkVoidFunction>(track_bind);
    if(std::strcmp(name,"vkMapMemory")==0)return reinterpret_cast<PFN_vkVoidFunction>(track_map);
    if(std::strcmp(name,"vkUpdateDescriptorSets")==0)return reinterpret_cast<PFN_vkVoidFunction>(track_descriptors);
    return observed_get(device,name);
}
Input mouth_input(std::size_t count=3){auto input=fixture(count);auto data=std::make_shared<vrm::MouthData>();data->available=31;
    for(std::size_t n=0;n<count;++n){vrm::MouthDeltaRecord record;record.vertex=std::uint32_t(n);
        for(unsigned c=0;c<5;++c){record.position[c]={float(c+1)*.02f,float(n+1)*-.03f,float(c%2?-.04:.05)};record.normal[c]={.01f*float(c+1),-.02f,0};}
        data->records.push_back(record);}
    data->ranges.push_back({0,0,0,std::uint32_t(count)});input.avatar.mouth=data;return input;
}
}
int main(int argc,char** argv){try{
    require(argc==2,"shader directory required");Context c;native_get=c.get;native_push=c.fn<PFN_vkCmdPushConstants>("vkCmdPushConstants");
    evrm::NativeSkinningGpu gpu(c.device,proof_get,c.memory,c.limits,std::filesystem::path(argv[1]),&c.uncertain);
    auto input=mouth_input();auto source=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(input.avatar,input.pose));require(bool(*source)&&source->mouth,"mouth CPU asset");
    const auto upload=[&](evrm::NativeSkinningGpu& owner,const std::shared_ptr<evrm::NativeSkinAsset>& data){auto asset=owner.asset(data);c.begin();owner.upload(c.command,asset);c.submit();owner.finish_upload(asset);return asset;};
    auto asset=upload(gpu,source);require(asset->mouth_available(),"GPU asset did not retain supported mouth data");unsigned cases=0;
    std::vector<evrm::NativeAvatarVertex> previous_vertices;
    const auto execute=[&](const vrm::MouthWeights& weights,std::shared_ptr<const evrm::NativeSkinGpuGeometry> previous={},int corrupt=0,bool good=true){
        const auto pose=evrm::make_native_skin_pose(source,input.pose);require(bool(pose),"pose packing failed");HostBuffer prepared(c,224),readback(c,source->vertices.size()*44);
        auto* words=static_cast<std::uint32_t*>(prepared.mapped);words[48]=1;words[49]=previous?1:0;
        auto work=gpu.frame(asset,pose,prepared.binding(),previous,weights);
        require(last_uniform.range==64,"mouth control must be exactly64 bytes");
        auto* control=static_cast<std::uint32_t*>(mapped_memory.at(memory_for_buffer.at(last_uniform.buffer)));
        if(corrupt==1)control[4]=0xfffffff0u;
        if(corrupt==2)control[8]=0x7fc00000u;
        c.begin();
        if(corrupt==3){c.fn<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(c.command,last_source.buffer,control[4],4,0xfffffffeu);
            auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);}
        gpu.record(c.command,work);const auto binding=work->geometry()->binding();VkBufferCopy copy{0,0,readback.size};
        c.fn<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(c.command,binding.buffer,readback.buffer,1,&copy);
        auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);c.submit();
        require((words[48]==1)==good&&(work->status_after_completion()==0)==good,"mouth errors did not revoke common commit");
        if(good){auto reference=input.avatar;for(const auto& record:reference.mouth->records)require(vrm::apply_mouth_delta(reference.meshes[0].primitives[0].vertices[record.vertex],record,weights),"reference mouth rejected");
            const auto expected=evrm::make_native_avatar_pose(reference,input.pose);require(bool(expected),"reference skin rejected");
            const auto* actual=static_cast<const evrm::NativeAvatarVertex*>(readback.mapped);
            for(std::size_t n=0;n<expected.vertices.size();++n){float a[11],b[11];std::memcpy(a,&actual[n],44);std::memcpy(b,&expected.vertices[n],44);
                for(unsigned k=0;k<8;++k)require(std::isfinite(a[k])&&std::abs(double(a[k])-b[k])<4e-5*(1+std::abs(double(b[k]))),"mouth CPU/GPU differential");
                for(unsigned axis=0;axis<3;++axis)require(actual[n].previous_position[axis]==(previous?previous_vertices[n].position[axis]:actual[n].position[axis]),"mouth history mismatch");}
            previous_vertices.assign(actual,actual+expected.vertices.size());}
        ++cases;return work->geometry();
    };
    execute({});auto previous=execute({1,0,0,0,0});execute({0,0,0,0,1},previous);
    input.pose.node_world[0][0]=-2;input.pose.node_world[0][4]=.3f;input.pose.node_world[0][5]=1.7f;input.pose.node_world[0][10]=.6f;
    execute({.2f,.3f,.1f,.2f,.2f});execute({});execute({1,0,0,0,0},{},1,false);execute({1,0,0,0,0},{},2,false);
    execute({1,0,0,0,0},{},3,false);
    auto limited=c.limits;limited.maxStorageBufferRange=384;auto larger=mouth_input(6);
    auto larger_source=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(larger.avatar,larger.pose));require(bool(*larger_source),"low-range source");
    evrm::NativeSkinningGpu restricted(c.device,proof_get,c.memory,limited,std::filesystem::path(argv[1]),&c.uncertain);
    const auto fallback=upload(restricted,larger_source);require(!fallback->mouth_available(),"low storage range did not disable only optional data");++cases;
    HostBuffer fallback_frame(c,224),fallback_readback(c,larger_source->vertices.size()*44);static_cast<std::uint32_t*>(fallback_frame.mapped)[48]=1;
    const auto fallback_pose=evrm::make_native_skin_pose(larger_source,larger.pose);
    auto fallback_work=restricted.frame(fallback,fallback_pose,fallback_frame.binding(),{},{1,0,0,0,0});c.begin();restricted.record(c.command,fallback_work);
    const auto fallback_binding=fallback_work->geometry()->binding();VkBufferCopy fallback_copy{0,0,fallback_readback.size};
    c.fn<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(c.command,fallback_binding.buffer,fallback_readback.buffer,1,&fallback_copy);
    auto fallback_barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);fallback_barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;fallback_barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&fallback_barrier,0,nullptr,0,nullptr);c.submit();
    require(static_cast<std::uint32_t*>(fallback_frame.mapped)[48]==1&&fallback_work->status_after_completion()==0,"optional range fallback suppressed GPU base avatar");
    const auto fallback_expected=evrm::make_native_avatar_pose(larger.avatar,larger.pose);require(bool(fallback_expected),"fallback CPU baseline");
    const auto* fallback_actual=static_cast<const evrm::NativeAvatarVertex*>(fallback_readback.mapped);
    for(std::size_t n=0;n<fallback_expected.vertices.size();++n)for(unsigned axis=0;axis<3;++axis){
        require(std::abs(double(fallback_actual[n].position[axis])-fallback_expected.vertices[n].position[axis])<4e-5,"low-range fallback animated mouth or changed base geometry");}
    ++cases;
    require(private_push_writes==0,"mouth disturbed native push constants");
    std::cout<<"PASS MouthGPU cases="<<cases<<" CPU/GPU positions/normals/history/common commit/storage fallback; no gameplay claim\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL MouthGPU: "<<error.what()<<'\n';return 1;}}
