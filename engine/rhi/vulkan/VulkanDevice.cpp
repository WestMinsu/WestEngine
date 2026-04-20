// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan device implementation — Instance, PhysicalDevice, LogicalDevice
// =============================================================================
#include "rhi/vulkan/VulkanDevice.h"

#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHITexture.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanCommandList.h"
#include "rhi/vulkan/VulkanFence.h"
#include "rhi/vulkan/VulkanMemoryAllocator.h"
#include "rhi/vulkan/VulkanPipeline.h"
#include "rhi/vulkan/VulkanQueue.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanSemaphore.h"
#include "rhi/vulkan/VulkanSwapChain.h"
#include "rhi/vulkan/VulkanTexture.h"

// Win32 surface extension name — defined directly to avoid pulling in Windows headers.
// VulkanSwapChain.cpp includes Win32Headers.h and vulkan_win32.h for actual surface creation.
#if defined(_WIN32) && !defined(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace west::rhi
{

VulkanDevice::VulkanDevice() = default;

// ── Debug Callback ────────────────────────────────────────────────────────

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                           VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                                                           const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                           void* /*userData*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "[Vulkan] {}", callbackData->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        WEST_LOG_WARNING(LogCategory::RHI, "[Vulkan] {}", callbackData->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        WEST_LOG_INFO(LogCategory::RHI, "[Vulkan] {}", callbackData->pMessage);
    }

    return VK_FALSE;
}

// ── Destructor ────────────────────────────────────────────────────────────

VulkanDevice::~VulkanDevice()
{
    if (m_device)
    {
        WaitIdle();
    }

    m_deletionQueue.FlushAll();
    DestroyBindlessDescriptors();
    m_memoryAllocator.reset();
    m_graphicsQueue.reset();

    if (m_device)
    {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_debugMessenger && m_instance)
    {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func)
        {
            func(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_instance)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan Device destroyed.");
}

// ── Initialize ────────────────────────────────────────────────────────────

bool VulkanDevice::Initialize(const RHIDeviceConfig& config)
{
    WEST_LOG_INFO(LogCategory::RHI, "Initializing Vulkan Device...");

    CreateInstance(config.enableValidation);

    if (m_validationEnabled)
    {
        SetupDebugMessenger();
    }

    SelectPhysicalDevice(config.preferredGPUIndex);
    CreateLogicalDevice(m_validationEnabled);
    QueryDeviceCaps();
    WEST_CHECK(m_caps.maxBindlessResources > 0, "Vulkan bindless requires descriptor indexing support");

    // Create graphics queue wrapper
    VkQueue graphicsQueue;
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &graphicsQueue);
    m_graphicsQueue = std::make_unique<VulkanQueue>();
    m_graphicsQueue->Initialize(graphicsQueue, m_graphicsQueueFamily, RHIQueueType::Graphics);

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan Device initialized: {}", m_deviceName);
    WEST_LOG_INFO(LogCategory::RHI, "  VRAM: {} MB", m_caps.dedicatedVideoMemory / (1024 * 1024));
    WEST_LOG_INFO(LogCategory::RHI, "  Ray Tracing: {}", m_caps.supportsRayTracing ? "Yes" : "No");
    WEST_LOG_INFO(LogCategory::RHI, "  Mesh Shaders: {}", m_caps.supportsMeshShaders ? "Yes" : "No");

    // Phase 2: Initialize VMA
    m_memoryAllocator = std::make_unique<VulkanMemoryAllocator>();
    if (!m_memoryAllocator->Initialize(m_instance, m_physicalDevice, m_device))
    {
        WEST_LOG_FATAL(LogCategory::RHI, "Failed to initialize VMA");
        return false;
    }
    WEST_LOG_INFO(LogCategory::RHI, "  Resizable BAR: {}", m_memoryAllocator->SupportsReBAR() ? "Yes" : "No");

    CreateBindlessDescriptors();

    return true;
}

// ── Instance Creation ─────────────────────────────────────────────────────

void VulkanDevice::CreateInstance(bool enableValidation)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "WestEngine";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.pEngineName = "WestEngine";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Required extensions
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(_WIN32)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };

    std::vector<const char*> layers;

    m_validationEnabled = enableValidation && IsValidationLayerAvailable();
    if (enableValidation && !m_validationEnabled)
    {
        WEST_LOG_WARNING(LogCategory::RHI,
                         "VK_LAYER_KHRONOS_validation is not available. Vulkan validation disabled.");
    }

    if (m_validationEnabled)
    {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    // Enable debug messenger during instance creation/destruction
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_validationEnabled)
    {
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;

        createInfo.pNext = &debugCreateInfo;
    }

    WEST_VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
    WEST_LOG_INFO(LogCategory::RHI, "Vulkan Instance created (API 1.3).");
}

bool VulkanDevice::IsValidationLayerAvailable() const
{
    uint32_t layerCount = 0;
    WEST_VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));

    std::vector<VkLayerProperties> layers(layerCount);
    WEST_VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()));

    for (const auto& layer : layers)
    {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            return true;
        }
    }

    return false;
}

// ── Debug Messenger ───────────────────────────────────────────────────────

void VulkanDevice::SetupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));

    if (func)
    {
        WEST_VK_CHECK(func(m_instance, &createInfo, nullptr, &m_debugMessenger));
        WEST_LOG_INFO(LogCategory::RHI, "Vulkan Debug Messenger created.");
    }
}

// ── Physical Device Selection ─────────────────────────────────────────────

void VulkanDevice::SelectPhysicalDevice(uint32_t preferredIndex)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    WEST_CHECK(deviceCount > 0, "No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    uint32_t bestIndex = UINT32_MAX;
    uint64_t bestVRAM = 0;
    bool preferredFound = false;
    bool bestIsDiscrete = false;

    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(devices[i], &memProps);

        // Calculate dedicated VRAM
        uint64_t vram = 0;
        for (uint32_t j = 0; j < memProps.memoryHeapCount; ++j)
        {
            if (memProps.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                vram += memProps.memoryHeaps[j].size;
            }
        }

        WEST_LOG_INFO(LogCategory::RHI, "  GPU[{}]: {} ({} MB VRAM)", i, props.deviceName, vram / (1024 * 1024));

        // Find graphics queue family
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueFamilyCount, queueFamilies.data());

        bool hasGraphics = false;
        for (uint32_t q = 0; q < queueFamilyCount; ++q)
        {
            if (queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                hasGraphics = true;
                break;
            }
        }

        if (!hasGraphics)
            continue;

        if (i == preferredIndex)
        {
            bestIndex = i;
            preferredFound = true;
            bestVRAM = vram;
            break;
        }

        const bool isDiscrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        const bool betterType = isDiscrete && !bestIsDiscrete;
        const bool betterMemory = isDiscrete == bestIsDiscrete && vram > bestVRAM;
        if (!preferredFound && (bestIndex == UINT32_MAX || betterType || betterMemory))
        {
            bestIndex = i;
            bestVRAM = vram;
            bestIsDiscrete = isDiscrete;
        }
    }

    WEST_CHECK(bestIndex != UINT32_MAX, "No Vulkan-capable GPU with graphics queue found");

    m_physicalDevice = devices[bestIndex];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    m_deviceName = props.deviceName;

    // Find graphics queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            m_graphicsQueueFamily = i;
            break;
        }
    }

    WEST_CHECK(m_graphicsQueueFamily != UINT32_MAX, "No graphics queue family found on selected GPU");
}

// ── Logical Device Creation ───────────────────────────────────────────────

void VulkanDevice::CreateLogicalDevice(bool enableValidation)
{
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Required device extensions
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    bool descriptorBufferExtensionAvailable = false;

    // Check optional/required device extensions
    {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availableExts(extCount);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, availableExts.data());

        for (const auto& ext : availableExts)
        {
            if (std::strcmp(ext.extensionName, VK_EXT_DEVICE_FAULT_EXTENSION_NAME) == 0)
            {
                deviceExtensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
                m_deviceFaultSupported = true;
                WEST_LOG_INFO(LogCategory::RHI, "VK_EXT_device_fault supported — GPU crash diagnostics enabled.");
                break;
            }
        }

        for (const auto& ext : availableExts)
        {
            if (std::strcmp(ext.extensionName, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0)
            {
                descriptorBufferExtensionAvailable = true;
                break;
            }
        }
    }

    WEST_CHECK(descriptorBufferExtensionAvailable, "VK_EXT_descriptor_buffer is required for Vulkan bindless");
    deviceExtensions.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);

    // Query support first. WestEngine Phase 3 is fallback-free: bindless is mandatory.
    VkPhysicalDeviceVulkan13Features supported13{};
    supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT supportedDescriptorBuffer{};
    supportedDescriptorBuffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;

    VkPhysicalDeviceVulkan12Features supported12{};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    supported12.pNext = &supported13;
    supported13.pNext = &supportedDescriptorBuffer;

    VkPhysicalDeviceFeatures2 supportedFeatures{};
    supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportedFeatures.pNext = &supported12;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supportedFeatures);

    WEST_CHECK(supported13.dynamicRendering == VK_TRUE, "Vulkan 1.3 dynamic rendering is required");
    WEST_CHECK(supported13.synchronization2 == VK_TRUE, "Vulkan 1.3 synchronization2 is required");
    WEST_CHECK(supported12.timelineSemaphore == VK_TRUE, "Vulkan timeline semaphore is required");
    WEST_CHECK(supported12.bufferDeviceAddress == VK_TRUE, "Vulkan buffer device address is required");
    WEST_CHECK(supported12.descriptorIndexing == VK_TRUE, "Vulkan descriptor indexing is required");
    WEST_CHECK(supported12.descriptorBindingPartiallyBound == VK_TRUE,
               "Vulkan partially-bound descriptors are required");
    WEST_CHECK(supported12.runtimeDescriptorArray == VK_TRUE, "Vulkan runtime descriptor arrays are required");
    WEST_CHECK(supported12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE,
               "Vulkan non-uniform sampled image indexing is required");
    WEST_CHECK(supported12.shaderStorageBufferArrayNonUniformIndexing == VK_TRUE,
               "Vulkan non-uniform storage buffer indexing is required");
    WEST_CHECK(supportedDescriptorBuffer.descriptorBuffer == VK_TRUE, "VK_EXT_descriptor_buffer feature is required");
    WEST_CHECK(supportedFeatures.features.samplerAnisotropy == VK_TRUE,
               "Vulkan sampler anisotropy is required for Phase 3 samplers");

    // Vulkan 1.3 features — Timeline Semaphore, Dynamic Rendering, Synchronization2
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures{};
    descriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptorBufferFeatures.descriptorBuffer = VK_TRUE;
    features13.pNext = &descriptorBufferFeatures;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features12;
    features2.features.samplerAnisotropy = VK_TRUE;

    // Device fault feature
    VkPhysicalDeviceFaultFeaturesEXT faultFeatures{};
    if (m_deviceFaultSupported)
    {
        faultFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        faultFeatures.deviceFault = VK_TRUE;
        descriptorBufferFeatures.pNext = &faultFeatures;
    }

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &features2;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Validation layers on device (deprecated but still accepted)
    std::vector<const char*> layers;
    if (enableValidation)
    {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        deviceCreateInfo.ppEnabledLayerNames = layers.data();
    }

    WEST_VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device));
    WEST_LOG_INFO(LogCategory::RHI, "Vulkan Logical Device created.");
}

// ── Device Capabilities ───────────────────────────────────────────────────

void VulkanDevice::QueryDeviceCaps()
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    m_caps.dedicatedVideoMemory = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
    {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            m_caps.dedicatedVideoMemory += memProps.memoryHeaps[i].size;
        }
    }

    // Ray Tracing (Ray Query)
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;

    VkPhysicalDeviceFeatures2 queryFeatures2{};
    queryFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    queryFeatures2.pNext = &rayQueryFeatures;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &queryFeatures2);
    m_caps.supportsRayTracing = (rayQueryFeatures.rayQuery == VK_TRUE);

    // Mesh Shaders
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

    VkPhysicalDeviceFeatures2 meshQuery{};
    meshQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    meshQuery.pNext = &meshFeatures;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &meshQuery);
    m_caps.supportsMeshShaders = (meshFeatures.meshShader == VK_TRUE);

    // Bindless: descriptor indexing supplies shader array limits, descriptor buffer supplies descriptor byte sizes.
    VkPhysicalDeviceDescriptorIndexingProperties indexingProps{};
    indexingProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProps{};
    descriptorBufferProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
    indexingProps.pNext = &descriptorBufferProps;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &indexingProps;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);

    m_sampledImageDescriptorSize = descriptorBufferProps.sampledImageDescriptorSize;
    m_samplerDescriptorSize = descriptorBufferProps.samplerDescriptorSize;
    m_storageBufferDescriptorSize = descriptorBufferProps.storageBufferDescriptorSize;
    m_maxSamplerAnisotropy = props.limits.maxSamplerAnisotropy;

    uint32_t descriptorCapacity = std::min<uint32_t>(
        kBindlessCapacity,
        std::min(props.limits.maxDescriptorSetSampledImages,
                 std::min(props.limits.maxDescriptorSetSamplers, props.limits.maxDescriptorSetStorageBuffers)));

    auto descriptorLimitFromRange = [](VkDeviceSize range, size_t descriptorSize) -> uint32_t
    {
        if (descriptorSize == 0)
        {
            return 0;
        }

        const VkDeviceSize descriptorCount = range / descriptorSize;
        if (descriptorCount > std::numeric_limits<uint32_t>::max())
        {
            return std::numeric_limits<uint32_t>::max();
        }

        return static_cast<uint32_t>(descriptorCount);
    };

    if (m_sampledImageDescriptorSize > 0)
    {
        descriptorCapacity = std::min<uint32_t>(
            descriptorCapacity, descriptorLimitFromRange(descriptorBufferProps.maxResourceDescriptorBufferRange,
                                                         m_sampledImageDescriptorSize));
    }
    if (m_storageBufferDescriptorSize > 0)
    {
        descriptorCapacity = std::min<uint32_t>(
            descriptorCapacity, descriptorLimitFromRange(descriptorBufferProps.maxResourceDescriptorBufferRange,
                                                         m_storageBufferDescriptorSize));
    }
    if (m_samplerDescriptorSize > 0)
    {
        descriptorCapacity = std::min<uint32_t>(
            descriptorCapacity, descriptorLimitFromRange(descriptorBufferProps.maxSamplerDescriptorBufferRange,
                                                         m_samplerDescriptorSize));
    }

    m_caps.maxBindlessResources = descriptorCapacity;

    // ReBAR: check for large host-visible device-local heap
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        {
            m_caps.supportsResizableBar = true;
            break;
        }
    }
}

void VulkanDevice::LoadDescriptorBufferFunctions()
{
    m_vkGetDescriptorSetLayoutSizeEXT = reinterpret_cast<PFN_vkGetDescriptorSetLayoutSizeEXT>(
        vkGetDeviceProcAddr(m_device, "vkGetDescriptorSetLayoutSizeEXT"));
    m_vkGetDescriptorSetLayoutBindingOffsetEXT = reinterpret_cast<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>(
        vkGetDeviceProcAddr(m_device, "vkGetDescriptorSetLayoutBindingOffsetEXT"));
    m_vkGetDescriptorEXT = reinterpret_cast<PFN_vkGetDescriptorEXT>(
        vkGetDeviceProcAddr(m_device, "vkGetDescriptorEXT"));
    m_vkCmdBindDescriptorBuffersEXT = reinterpret_cast<PFN_vkCmdBindDescriptorBuffersEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdBindDescriptorBuffersEXT"));
    m_vkCmdSetDescriptorBufferOffsetsEXT = reinterpret_cast<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdSetDescriptorBufferOffsetsEXT"));

    WEST_CHECK(m_vkGetDescriptorSetLayoutSizeEXT != nullptr, "vkGetDescriptorSetLayoutSizeEXT is unavailable");
    WEST_CHECK(m_vkGetDescriptorSetLayoutBindingOffsetEXT != nullptr,
               "vkGetDescriptorSetLayoutBindingOffsetEXT is unavailable");
    WEST_CHECK(m_vkGetDescriptorEXT != nullptr, "vkGetDescriptorEXT is unavailable");
    WEST_CHECK(m_vkCmdBindDescriptorBuffersEXT != nullptr, "vkCmdBindDescriptorBuffersEXT is unavailable");
    WEST_CHECK(m_vkCmdSetDescriptorBufferOffsetsEXT != nullptr,
               "vkCmdSetDescriptorBufferOffsetsEXT is unavailable");
}

void VulkanDevice::CreateBindlessDescriptors()
{
    m_bindlessCapacity = std::min(kBindlessCapacity, m_caps.maxBindlessResources);
    WEST_CHECK(m_bindlessCapacity > 0, "Vulkan bindless descriptor capacity is zero");
    WEST_CHECK(m_memoryAllocator != nullptr, "Vulkan bindless descriptor buffer requires VMA");
    WEST_CHECK(m_sampledImageDescriptorSize > 0, "Vulkan sampled image descriptor size is zero");
    WEST_CHECK(m_samplerDescriptorSize > 0, "Vulkan sampler descriptor size is zero");
    WEST_CHECK(m_storageBufferDescriptorSize > 0, "Vulkan storage buffer descriptor size is zero");

    LoadDescriptorBufferFunctions();

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = m_bindlessCapacity;
    bindings[0].stageFlags = VK_SHADER_STAGE_ALL;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = m_bindlessCapacity;
    bindings[1].stageFlags = VK_SHADER_STAGE_ALL;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = m_bindlessCapacity;
    bindings[2].stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorBindingFlags bindingFlags[3] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = 3;
    bindingFlagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    WEST_VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_bindlessSetLayout));

    m_vkGetDescriptorSetLayoutSizeEXT(m_device, m_bindlessSetLayout, &m_bindlessDescriptorBufferSize);
    m_vkGetDescriptorSetLayoutBindingOffsetEXT(m_device, m_bindlessSetLayout, 0, &m_textureDescriptorOffset);
    m_vkGetDescriptorSetLayoutBindingOffsetEXT(m_device, m_bindlessSetLayout, 1, &m_samplerDescriptorOffset);
    m_vkGetDescriptorSetLayoutBindingOffsetEXT(m_device, m_bindlessSetLayout, 2, &m_bufferDescriptorOffset);
    WEST_CHECK(m_bindlessDescriptorBufferSize > 0, "Vulkan descriptor buffer layout size is zero");

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_bindlessDescriptorBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                       VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocationInfo{};
    WEST_VK_CHECK(vmaCreateBuffer(m_memoryAllocator->GetAllocator(), &bufferInfo, &allocInfo,
                                  &m_bindlessDescriptorBuffer, &m_bindlessDescriptorAllocation,
                                  &allocationInfo));
    m_bindlessDescriptorMapped = allocationInfo.pMappedData;
    WEST_CHECK(m_bindlessDescriptorMapped != nullptr, "Failed to map Vulkan bindless descriptor buffer");
    std::memset(m_bindlessDescriptorMapped, 0, static_cast<size_t>(m_bindlessDescriptorBufferSize));

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = m_bindlessDescriptorBuffer;
    m_bindlessDescriptorBufferAddress = vkGetBufferDeviceAddress(m_device, &addressInfo);
    WEST_CHECK(m_bindlessDescriptorBufferAddress != 0, "Failed to get Vulkan descriptor buffer address");

    m_bindlessPool.Initialize(m_bindlessCapacity);

    WEST_LOG_INFO(LogCategory::RHI,
                  "Vulkan bindless descriptor buffer created (capacity={}, size={} bytes).",
                  m_bindlessCapacity, m_bindlessDescriptorBufferSize);
}

void VulkanDevice::DestroyBindlessDescriptors()
{
    if (m_bindlessDescriptorBuffer && m_memoryAllocator)
    {
        vmaDestroyBuffer(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorBuffer,
                         m_bindlessDescriptorAllocation);
        m_bindlessDescriptorBuffer = VK_NULL_HANDLE;
        m_bindlessDescriptorAllocation = VK_NULL_HANDLE;
        m_bindlessDescriptorMapped = nullptr;
        m_bindlessDescriptorBufferAddress = 0;
    }

    if (m_bindlessSetLayout)
    {
        vkDestroyDescriptorSetLayout(m_device, m_bindlessSetLayout, nullptr);
        m_bindlessSetLayout = VK_NULL_HANDLE;
    }
}

// ── IRHIDevice Implementation ─────────────────────────────────────────────

std::unique_ptr<IRHIFence> VulkanDevice::CreateFence(uint64_t initialValue)
{
    auto fence = std::make_unique<VulkanFence>();
    fence->Initialize(m_device, initialValue);
    return fence;
}

std::unique_ptr<IRHISemaphore> VulkanDevice::CreateBinarySemaphore()
{
    auto semaphore = std::make_unique<VulkanSemaphore>();
    semaphore->Initialize(m_device);
    return semaphore;
}

std::unique_ptr<IRHICommandList> VulkanDevice::CreateCommandList(RHIQueueType type)
{
    auto cmdList = std::make_unique<VulkanCommandList>();
    cmdList->Initialize(m_device, m_graphicsQueueFamily, type, m_bindlessDescriptorBufferAddress,
                        m_vkCmdBindDescriptorBuffersEXT, m_vkCmdSetDescriptorBufferOffsetsEXT);
    return cmdList;
}

IRHIQueue* VulkanDevice::GetQueue(RHIQueueType type)
{
    WEST_ASSERT(type == RHIQueueType::Graphics);
    return m_graphicsQueue.get();
}

std::unique_ptr<IRHISwapChain> VulkanDevice::CreateSwapChain(const RHISwapChainDesc& desc)
{
    auto swapChain = std::make_unique<VulkanSwapChain>();
    swapChain->Initialize(this, desc);
    return swapChain;
}

void VulkanDevice::WaitIdle()
{
    if (m_device)
    {
        vkDeviceWaitIdle(m_device);
    }
}

const char* VulkanDevice::GetDeviceName() const
{
    return m_deviceName.c_str();
}

RHIDeviceCaps VulkanDevice::GetCapabilities() const
{
    return m_caps;
}

// ── Stub implementations (Phase 2+) ──────────────────────────────────────

std::unique_ptr<IRHIBuffer> VulkanDevice::CreateBuffer(const RHIBufferDesc& desc)
{
    auto buffer = std::make_unique<VulkanBuffer>();
    buffer->Initialize(this, desc);
    return buffer;
}

std::unique_ptr<IRHITexture> VulkanDevice::CreateTexture(const RHITextureDesc& desc)
{
    auto texture = std::make_unique<VulkanTexture>();
    texture->Initialize(this, desc);
    return texture;
}

std::unique_ptr<IRHISampler> VulkanDevice::CreateSampler(const RHISamplerDesc& desc)
{
    auto sampler = std::make_unique<VulkanSampler>();
    sampler->Initialize(this, desc);
    return sampler;
}

std::unique_ptr<IRHIPipeline> VulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
    // Determine swapchain format for dynamic rendering
    // Default to B8G8R8A8_UNORM if no color formats specified
    VkFormat swapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    auto pipeline = std::make_unique<VulkanPipeline>();
    pipeline->Initialize(m_device, desc, swapChainFormat, m_bindlessSetLayout);
    return pipeline;
}

std::unique_ptr<IRHIPipeline> VulkanDevice::CreateComputePipeline(const RHIComputePipelineDesc& desc)
{
    // TODO(minsu): Phase 4 — Compute pipeline
    WEST_LOG_WARNING(LogCategory::RHI, "VulkanDevice::CreateComputePipeline — stub");
    return nullptr;
}

BindlessIndex VulkanDevice::RegisterBindlessResource(IRHIBuffer* buffer)
{
    WEST_ASSERT(buffer != nullptr);
    WEST_ASSERT(m_bindlessDescriptorMapped != nullptr);
    WEST_ASSERT(m_vkGetDescriptorEXT != nullptr);

    auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
    std::lock_guard lock(m_bindlessMutex);

    BindlessIndex index = m_bindlessPool.Allocate();
    if (index == kInvalidBindlessIndex)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "Vulkan bindless pool exhausted while registering buffer");
        return kInvalidBindlessIndex;
    }

    VkBufferDeviceAddressInfo bufferAddressInfo{};
    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferAddressInfo.buffer = vkBuffer->GetVkBuffer();
    const VkDeviceAddress bufferAddress = vkGetBufferDeviceAddress(m_device, &bufferAddressInfo);
    WEST_CHECK(bufferAddress != 0, "Vulkan bindless buffer registration requires a device address");

    VkDescriptorAddressInfoEXT addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
    addressInfo.address = bufferAddress;
    addressInfo.range = buffer->GetDesc().sizeBytes;
    addressInfo.format = VK_FORMAT_UNDEFINED;

    VkDescriptorGetInfoEXT descriptorInfo{};
    descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorInfo.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorInfo.data.pStorageBuffer = &addressInfo;

    const VkDeviceSize descriptorOffset = m_bufferDescriptorOffset + index * m_storageBufferDescriptorSize;
    void* descriptorDst = static_cast<uint8_t*>(m_bindlessDescriptorMapped) + descriptorOffset;
    m_vkGetDescriptorEXT(m_device, &descriptorInfo, m_storageBufferDescriptorSize, descriptorDst);
    vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation,
                       descriptorOffset, m_storageBufferDescriptorSize);

    vkBuffer->SetBindlessIndex(index);
    return index;
}

BindlessIndex VulkanDevice::RegisterBindlessResource(IRHITexture* texture)
{
    WEST_ASSERT(texture != nullptr);
    WEST_ASSERT(m_bindlessDescriptorMapped != nullptr);
    WEST_ASSERT(m_vkGetDescriptorEXT != nullptr);

    auto* vkTexture = static_cast<VulkanTexture*>(texture);
    std::lock_guard lock(m_bindlessMutex);

    BindlessIndex index = m_bindlessPool.Allocate();
    if (index == kInvalidBindlessIndex)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "Vulkan bindless pool exhausted while registering texture");
        return kInvalidBindlessIndex;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = vkTexture->GetVkImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorGetInfoEXT descriptorInfo{};
    descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptorInfo.data.pSampledImage = &imageInfo;

    const VkDeviceSize descriptorOffset = m_textureDescriptorOffset + index * m_sampledImageDescriptorSize;
    void* descriptorDst = static_cast<uint8_t*>(m_bindlessDescriptorMapped) + descriptorOffset;
    m_vkGetDescriptorEXT(m_device, &descriptorInfo, m_sampledImageDescriptorSize, descriptorDst);
    vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation,
                       descriptorOffset, m_sampledImageDescriptorSize);

    vkTexture->SetBindlessIndex(index);
    return index;
}

BindlessIndex VulkanDevice::RegisterBindlessResource(IRHISampler* sampler)
{
    WEST_ASSERT(sampler != nullptr);
    WEST_ASSERT(m_bindlessDescriptorMapped != nullptr);
    WEST_ASSERT(m_vkGetDescriptorEXT != nullptr);

    auto* vkSampler = static_cast<VulkanSampler*>(sampler);
    std::lock_guard lock(m_bindlessMutex);

    BindlessIndex index = m_bindlessPool.Allocate();
    if (index == kInvalidBindlessIndex)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "Vulkan bindless pool exhausted while registering sampler");
        return kInvalidBindlessIndex;
    }

    VkSampler samplerHandle = vkSampler->GetVkSampler();

    VkDescriptorGetInfoEXT descriptorInfo{};
    descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    descriptorInfo.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorInfo.data.pSampler = &samplerHandle;

    const VkDeviceSize descriptorOffset = m_samplerDescriptorOffset + index * m_samplerDescriptorSize;
    void* descriptorDst = static_cast<uint8_t*>(m_bindlessDescriptorMapped) + descriptorOffset;
    m_vkGetDescriptorEXT(m_device, &descriptorInfo, m_samplerDescriptorSize, descriptorDst);
    vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation,
                       descriptorOffset, m_samplerDescriptorSize);

    vkSampler->SetBindlessIndex(index);
    return index;
}

void VulkanDevice::UnregisterBindlessResource(BindlessIndex index)
{
    std::lock_guard lock(m_bindlessMutex);
    if (!m_bindlessPool.Free(index))
    {
        WEST_LOG_WARNING(LogCategory::RHI, "Vulkan bindless unregister ignored for invalid index {}", index);
        return;
    }

    if (m_bindlessDescriptorMapped)
    {
        const VkDeviceSize textureOffset = m_textureDescriptorOffset + index * m_sampledImageDescriptorSize;
        const VkDeviceSize samplerOffset = m_samplerDescriptorOffset + index * m_samplerDescriptorSize;
        const VkDeviceSize bufferOffset = m_bufferDescriptorOffset + index * m_storageBufferDescriptorSize;

        std::memset(static_cast<uint8_t*>(m_bindlessDescriptorMapped) + textureOffset, 0,
                    m_sampledImageDescriptorSize);
        std::memset(static_cast<uint8_t*>(m_bindlessDescriptorMapped) + samplerOffset, 0,
                    m_samplerDescriptorSize);
        std::memset(static_cast<uint8_t*>(m_bindlessDescriptorMapped) + bufferOffset, 0,
                    m_storageBufferDescriptorSize);

        vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation,
                           textureOffset, m_sampledImageDescriptorSize);
        vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation,
                           samplerOffset, m_samplerDescriptorSize);
        vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation,
                           bufferOffset, m_storageBufferDescriptorSize);
    }
}

} // namespace west::rhi
