#define VK_NO_PROTOTYPES
#include "device_features.hpp"
#include <array>

namespace evrm {
bool native_rendering_environment_supported(std::uint32_t api,const DeviceFeaturesObservation& f)noexcept {
    if(VK_API_VERSION_VARIANT(api)||api<VK_API_VERSION_1_1||api>VK_MAKE_API_VERSION(0,1,4,4095)||
       !f.complete||f.status!=DeviceFeatureStatus::ok||!f.single_physical_device||
       !f.independent_blend.available||!f.independent_blend.enabled||!f.dynamic_rendering.available||!f.dynamic_rendering.enabled)return false;
    if(api>=VK_API_VERSION_1_3)return true;
    for(const auto& extension:f.enabled_extensions)if(extension=="VK_KHR_dynamic_rendering")return true;
    return false;
}
namespace {
void reject(DeviceFeaturesObservation& result,DeviceFeatureStatus status) noexcept {
    result.status=status;result.complete=false;
    result.geometry_shader={};result.uniform_buffer_update_after_bind={};result.independent_blend={};
    result.dynamic_rendering={};result.mesh_shader={};
    result.storage_buffer_update_after_bind={};result.update_unused_while_pending={};
    result.descriptor_binding_partially_bound={};
}
bool observe(DeviceFeaturesObservation& result,DeviceFeatureFlag& flag,VkBool32 value,DeviceFeatureSource source) noexcept {
    if(value!=VK_FALSE&&value!=VK_TRUE){result.status=DeviceFeatureStatus::invalid_feature_value;return false;}
    const bool enabled=value==VK_TRUE;
    if(flag.available&&flag.enabled!=enabled){result.status=DeviceFeatureStatus::conflicting_features;return false;}
    if(!flag.available){flag.available=true;flag.enabled=enabled;flag.source=source;}
    else if(flag.source!=source)flag.source=DeviceFeatureSource::multiple;
    return true;
}
template<class Features> bool indexing(DeviceFeaturesObservation& result,const Features& features,DeviceFeatureSource source) noexcept {
    return observe(result,result.uniform_buffer_update_after_bind,features.descriptorBindingUniformBufferUpdateAfterBind,source)&&
           observe(result,result.storage_buffer_update_after_bind,features.descriptorBindingStorageBufferUpdateAfterBind,source)&&
           observe(result,result.update_unused_while_pending,features.descriptorBindingUpdateUnusedWhilePending,source)&&
           observe(result,result.descriptor_binding_partially_bound,features.descriptorBindingPartiallyBound,source);
}
}

DeviceFeaturesObservation inspect_device_features(const VkDeviceCreateInfo& info) noexcept {
    DeviceFeaturesObservation result;
    try {
        if(info.sType!=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO){reject(result,DeviceFeatureStatus::invalid_create_info);return result;}
        result.p_enabled_features_present=info.pEnabledFeatures!=nullptr;
        result.declared_extension_count=info.enabledExtensionCount;
        if(info.pEnabledFeatures&&(!observe(result,result.geometry_shader,info.pEnabledFeatures->geometryShader,DeviceFeatureSource::enabled_features)||
            !observe(result,result.independent_blend,info.pEnabledFeatures->independentBlend,DeviceFeatureSource::enabled_features))) {
            reject(result,result.status);return result;
        }
        std::array<const VkBaseInStructure*,device_feature_chain_limit> visited{};
        std::size_t count=0;
        auto* node=static_cast<const VkBaseInStructure*>(info.pNext);
        while(node) {
            for(std::size_t i=0;i<count;++i)if(visited[i]==node){reject(result,DeviceFeatureStatus::chain_cycle);return result;}
            if(count==visited.size()){reject(result,DeviceFeatureStatus::chain_limit);return result;}
            visited[count++]=node;result.chain_s_types.push_back(node->sType);
            bool consistent=true;
            switch(node->sType) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
                consistent=observe(result,result.mesh_shader,reinterpret_cast<const VkPhysicalDeviceMeshShaderFeaturesEXT*>(node)->meshShader,DeviceFeatureSource::mesh_shader);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
                consistent=observe(result,result.dynamic_rendering,reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(node)->dynamicRendering,DeviceFeatureSource::vulkan13);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: {
                consistent=observe(result,result.dynamic_rendering,reinterpret_cast<const VkPhysicalDeviceDynamicRenderingFeatures*>(node)->dynamicRendering,DeviceFeatureSource::dynamic_rendering);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
                result.features2_present=true;
                const auto* features=reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node);
                consistent=observe(result,result.geometry_shader,features->features.geometryShader,DeviceFeatureSource::features2)&&
                    observe(result,result.independent_blend,features->features.independentBlend,DeviceFeatureSource::features2);
                break;
            }
            case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO: {
                const auto* group=reinterpret_cast<const VkDeviceGroupDeviceCreateInfo*>(node);
                result.single_physical_device=result.single_physical_device&&group->physicalDeviceCount==1&&group->pPhysicalDevices&&group->pPhysicalDevices[0];
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
                result.vulkan12_present=true;
                consistent=indexing(result,*reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node),DeviceFeatureSource::vulkan12);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: {
                result.descriptor_indexing_present=true;
                consistent=indexing(result,*reinterpret_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(node),DeviceFeatureSource::descriptor_indexing);
                break;
            }
            default:break; // Unrelated/loader nodes have no requested feature interpretation.
            }
            if(!consistent){reject(result,result.status);return result;}
            node=node->pNext;
        }
        if(info.enabledExtensionCount>device_feature_extension_limit){reject(result,DeviceFeatureStatus::extension_limit);return result;}
        if(info.enabledExtensionCount&&!info.ppEnabledExtensionNames){reject(result,DeviceFeatureStatus::invalid_extension_name);return result;}
        for(uint32_t i=0;i<info.enabledExtensionCount;++i) {
            const char* name=info.ppEnabledExtensionNames[i];
            if(!name){reject(result,DeviceFeatureStatus::invalid_extension_name);return result;}
            std::size_t length=0;
            while(length<device_feature_name_limit&&name[length])++length;
            if(length==device_feature_name_limit){reject(result,DeviceFeatureStatus::invalid_extension_name);return result;}
            result.enabled_extensions.emplace_back(name,length);
        }
        result.complete=true;
    }catch(...){reject(result,DeviceFeatureStatus::allocation_failure);}
    return result;
}
}
