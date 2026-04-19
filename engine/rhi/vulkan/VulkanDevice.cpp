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
#include "rhi/vulkan/VulkanSemaphore.h"
#include "rhi/vulkan/VulkanSwapChain.h"

// Win32 surface extension name — defined directly to avoid pulling in Windows headers.
// VulkanSwapChain.cpp includes Win32Headers.h and vulkan_win32.h for actual surface creation.
#if defined(_WIN32) && !defined(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#endif

#include <cstring>
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

    // Check for VK_EXT_device_fault
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
    }

    // Vulkan 1.3 features — Timeline Semaphore, Dynamic Rendering, Synchronization2
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;
    features12.timelineSemaphore = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features12;

    // Device fault feature
    VkPhysicalDeviceFaultFeaturesEXT faultFeatures{};
    if (m_deviceFaultSupported)
    {
        faultFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        faultFeatures.deviceFault = VK_TRUE;
        features13.pNext = &faultFeatures;
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

    // Bindless: descriptor indexing is enabled in 1.2 features
    VkPhysicalDeviceDescriptorIndexingProperties indexingProps{};
    indexingProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &indexingProps;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);
    m_caps.maxBindlessResources = indexingProps.maxDescriptorSetUpdateAfterBindSampledImages;

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
    cmdList->Initialize(m_device, m_graphicsQueueFamily, type);
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
    // TODO(minsu): Phase 2 — VMA texture allocation
    WEST_LOG_WARNING(LogCategory::RHI, "VulkanDevice::CreateTexture — stub");
    return nullptr;
}

std::unique_ptr<IRHISampler> VulkanDevice::CreateSampler(const RHISamplerDesc& desc)
{
    // TODO(minsu): Phase 3 — Sampler creation
    WEST_LOG_WARNING(LogCategory::RHI, "VulkanDevice::CreateSampler — stub");
    return nullptr;
}

std::unique_ptr<IRHIPipeline> VulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
    // Determine swapchain format for dynamic rendering
    // Default to B8G8R8A8_UNORM if no color formats specified
    VkFormat swapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    auto pipeline = std::make_unique<VulkanPipeline>();
    pipeline->Initialize(m_device, desc, swapChainFormat);
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
    // TODO(minsu): Phase 3 — Descriptor indexing registration
    return kInvalidBindlessIndex;
}

BindlessIndex VulkanDevice::RegisterBindlessResource(IRHITexture* texture)
{
    // TODO(minsu): Phase 3 — Descriptor indexing registration
    return kInvalidBindlessIndex;
}

void VulkanDevice::UnregisterBindlessResource(BindlessIndex index)
{
    // TODO(minsu): Phase 3 — Descriptor indexing unregistration
}

} // namespace west::rhi
