#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "DiligentCore/Graphics/GraphicsEngineVulkan/interface/RenderDeviceVk.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <vector>
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

bool LitVulkanAdapterSupportsExtension(uint32_t vendorId, uint32_t deviceId, const char* extension) {
    static void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!loader) return false;
    auto getInstanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
    if (!getInstanceProc) return false;
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getInstanceProc(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!createInstance) return false;
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    if (createInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) return false;
    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getInstanceProc(instance, "vkDestroyInstance"));
    auto enumerateDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getInstanceProc(instance, "vkEnumeratePhysicalDevices"));
    auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(getInstanceProc(instance, "vkGetPhysicalDeviceProperties"));
    auto enumerateExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(getInstanceProc(instance, "vkEnumerateDeviceExtensionProperties"));
    bool supported = false;
    if (enumerateDevices && getProperties && enumerateExtensions) {
        uint32_t deviceCount = 0;
        enumerateDevices(instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> devices(deviceCount);
        enumerateDevices(instance, &deviceCount, devices.data());
        for (VkPhysicalDevice physical : devices) {
            VkPhysicalDeviceProperties properties{};
            getProperties(physical, &properties);
            if (properties.vendorID != vendorId || properties.deviceID != deviceId) continue;
            uint32_t extensionCount = 0;
            enumerateExtensions(physical, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            enumerateExtensions(physical, nullptr, &extensionCount, extensions.data());
            supported = std::ranges::any_of(extensions, [&](const VkExtensionProperties& candidate) { return std::strcmp(candidate.extensionName, extension) == 0; });
            break;
        }
    }
    if (destroyInstance) destroyInstance(instance, nullptr);
    return supported;
}
