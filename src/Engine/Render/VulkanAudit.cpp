// Vulkan headers must precede Diligent's Vulkan interface header.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "DiligentCore/Graphics/GraphicsEngineVulkan/interface/RenderDeviceVk.h"

#include <dlfcn.h>

#include <format>
#include <string>

std::string LitDescriptorIndexingAudit(Diligent::IRenderDevice* device) {
    if (!device || device->GetDeviceInfo().Type != Diligent::RENDER_DEVICE_TYPE_VULKAN) return "not a Vulkan device";
    auto* deviceVk = static_cast<Diligent::IRenderDeviceVk*>(device);
    static void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!loader) return "libvulkan.so.1 not loadable";
    auto getInstanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
    if (!getInstanceProc) return "vkGetInstanceProcAddr missing";
    const VkInstance instance = deviceVk->GetVkInstance();
    const VkPhysicalDevice physical = deviceVk->GetVkPhysicalDevice();
    auto getFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(getInstanceProc(instance, "vkGetPhysicalDeviceFeatures2"));
    auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(getInstanceProc(instance, "vkGetPhysicalDeviceProperties2"));
    if (!getFeatures || !getProperties) return "vkGetPhysicalDevice*2 missing";

    VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &indexing};
    getFeatures(physical, &features);
    VkPhysicalDeviceDescriptorIndexingProperties limits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &limits};
    getProperties(physical, &properties);
    return std::format("{} api {}.{}.{}: runtimeDescriptorArray={} sampledImageNonUniformIndexing={} partiallyBound={} variableDescriptorCount={} sampledImageUpdateAfterBind={} "
                       "maxPerStageSampledImages={} maxPerStageUpdateAfterBindSampledImages={} maxSetUpdateAfterBindSampledImages={} maxUpdateAfterBindDescriptorsInAllPools={}",
                       properties.properties.deviceName, VK_API_VERSION_MAJOR(properties.properties.apiVersion), VK_API_VERSION_MINOR(properties.properties.apiVersion), VK_API_VERSION_PATCH(properties.properties.apiVersion),
                       indexing.runtimeDescriptorArray, indexing.shaderSampledImageArrayNonUniformIndexing, indexing.descriptorBindingPartiallyBound, indexing.descriptorBindingVariableDescriptorCount,
                       indexing.descriptorBindingSampledImageUpdateAfterBind, properties.properties.limits.maxPerStageDescriptorSampledImages, limits.maxPerStageDescriptorUpdateAfterBindSampledImages,
                       limits.maxDescriptorSetUpdateAfterBindSampledImages, limits.maxUpdateAfterBindDescriptorsInAllPools);
}
