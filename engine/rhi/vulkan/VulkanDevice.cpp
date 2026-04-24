// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan device implementation — Instance, PhysicalDevice, LogicalDevice
// =============================================================================
#include "rhi/vulkan/VulkanDevice.h"

#include "rhi/common/FormatConversion.h"
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
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace west::rhi
{

namespace
{

struct QueueSelection
{
    uint32_t familyIndex = UINT32_MAX;
    uint32_t queueIndex = 0;
};

[[nodiscard]] bool SupportsQueueFlags(const VkQueueFamilyProperties& familyProperties, VkQueueFlags requiredFlags)
{
    return (familyProperties.queueFlags & requiredFlags) == requiredFlags;
}

[[nodiscard]] uint32_t FindQueueFamily(std::span<const VkQueueFamilyProperties> queueFamilies,
                                       VkQueueFlags requiredFlags, VkQueueFlags preferredAbsentFlags)
{
    uint32_t fallbackFamily = UINT32_MAX;
    for (uint32_t familyIndex = 0; familyIndex < queueFamilies.size(); ++familyIndex)
    {
        const VkQueueFamilyProperties& familyProperties = queueFamilies[familyIndex];
        if (!SupportsQueueFlags(familyProperties, requiredFlags))
        {
            continue;
        }

        if (fallbackFamily == UINT32_MAX)
        {
            fallbackFamily = familyIndex;
        }

        if ((familyProperties.queueFlags & preferredAbsentFlags) == 0)
        {
            return familyIndex;
        }
    }

    return fallbackFamily;
}

[[nodiscard]] QueueSelection AllocateQueueSelection(std::span<const VkQueueFamilyProperties> queueFamilies,
                                                    uint32_t familyIndex, std::vector<uint32_t>& nextQueueIndices)
{
    WEST_ASSERT(familyIndex != UINT32_MAX);
    WEST_ASSERT(familyIndex < queueFamilies.size());

    QueueSelection selection{};
    selection.familyIndex = familyIndex;

    const uint32_t queueCount = queueFamilies[familyIndex].queueCount;
    WEST_ASSERT(queueCount > 0);

    const uint32_t nextQueueIndex = nextQueueIndices[familyIndex];
    if (nextQueueIndex < queueCount)
    {
        selection.queueIndex = nextQueueIndex;
        nextQueueIndices[familyIndex] = nextQueueIndex + 1;
        return selection;
    }

    selection.queueIndex = queueCount - 1;
    return selection;
}

void AppendUniqueFamily(std::vector<uint32_t>& familyIndices, uint32_t familyIndex)
{
    if (std::find(familyIndices.begin(), familyIndices.end(), familyIndex) == familyIndices.end())
    {
        familyIndices.push_back(familyIndex);
    }
}

[[nodiscard]] VkImageCreateInfo BuildTransientImageCreateInfo(const RHITextureDesc& desc)
{
    VkImageUsageFlags usageFlags = 0;
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource))
        usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess))
        usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))
        usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))
        usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopySource))
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopyDest))
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_ALIAS_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = static_cast<VkFormat>(ToVkFormat(desc.format));
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usageFlags;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return imageInfo;
}

} // namespace

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
    m_computeQueue.reset();
    m_copyQueue.reset();

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

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, m_graphicsQueueIndex, &graphicsQueue);
    m_graphicsQueue = std::make_unique<VulkanQueue>();
    m_graphicsQueue->Initialize(graphicsQueue, m_graphicsQueueFamily, m_graphicsQueueIndex, RHIQueueType::Graphics);

    VkQueue computeQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(m_device, m_computeQueueFamily, m_computeQueueIndex, &computeQueue);
    m_computeQueue = std::make_unique<VulkanQueue>();
    m_computeQueue->Initialize(computeQueue, m_computeQueueFamily, m_computeQueueIndex, RHIQueueType::Compute);

    VkQueue copyQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(m_device, m_copyQueueFamily, m_copyQueueIndex, &copyQueue);
    m_copyQueue = std::make_unique<VulkanQueue>();
    m_copyQueue->Initialize(copyQueue, m_copyQueueFamily, m_copyQueueIndex, RHIQueueType::Copy);

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan Device initialized: {}", m_deviceName);
    WEST_LOG_INFO(LogCategory::RHI, "  VRAM: {} MB", m_caps.dedicatedVideoMemory / (1024 * 1024));
    WEST_LOG_INFO(LogCategory::RHI, "  Ray Tracing: {}", m_caps.supportsRayTracing ? "Yes" : "No");
    WEST_LOG_INFO(LogCategory::RHI, "  Mesh Shaders: {}", m_caps.supportsMeshShaders ? "Yes" : "No");
    WEST_LOG_INFO(LogCategory::RHI, "  Queue topology: G({}:{}) C({}:{}) T({}:{}) families={}, sharing={}",
                  m_graphicsQueueFamily, m_graphicsQueueIndex, m_computeQueueFamily, m_computeQueueIndex,
                  m_copyQueueFamily, m_copyQueueIndex, m_activeQueueFamilies.size(),
                  m_activeQueueFamilies.size() > 1 ? "concurrent" : "exclusive");

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
        WEST_LOG_WARNING(LogCategory::RHI, "VK_LAYER_KHRONOS_validation is not available. Vulkan validation disabled.");
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

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    m_graphicsQueueFamily = FindQueueFamily(queueFamilies, VK_QUEUE_GRAPHICS_BIT, 0);
    m_computeQueueFamily = FindQueueFamily(queueFamilies, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
    if (m_computeQueueFamily == UINT32_MAX)
    {
        m_computeQueueFamily = FindQueueFamily(queueFamilies, VK_QUEUE_COMPUTE_BIT, 0);
    }

    m_copyQueueFamily =
        FindQueueFamily(queueFamilies, VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    if (m_copyQueueFamily == UINT32_MAX)
    {
        m_copyQueueFamily = FindQueueFamily(queueFamilies, VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT);
    }
    if (m_copyQueueFamily == UINT32_MAX)
    {
        m_copyQueueFamily = FindQueueFamily(queueFamilies, VK_QUEUE_TRANSFER_BIT, 0);
    }

    WEST_CHECK(m_graphicsQueueFamily != UINT32_MAX, "No graphics queue family found on selected GPU");
    WEST_CHECK(m_computeQueueFamily != UINT32_MAX, "No compute queue family found on selected GPU");
    WEST_CHECK(m_copyQueueFamily != UINT32_MAX, "No copy queue family found on selected GPU");

    std::vector<uint32_t> nextQueueIndices(queueFamilyCount, 0);
    const QueueSelection graphicsQueue = AllocateQueueSelection(queueFamilies, m_graphicsQueueFamily, nextQueueIndices);
    const QueueSelection computeQueue = AllocateQueueSelection(queueFamilies, m_computeQueueFamily, nextQueueIndices);
    const QueueSelection copyQueue = AllocateQueueSelection(queueFamilies, m_copyQueueFamily, nextQueueIndices);

    m_graphicsQueueIndex = graphicsQueue.queueIndex;
    m_computeQueueIndex = computeQueue.queueIndex;
    m_copyQueueIndex = copyQueue.queueIndex;

    m_activeQueueFamilies.clear();
    AppendUniqueFamily(m_activeQueueFamilies, m_graphicsQueueFamily);
    AppendUniqueFamily(m_activeQueueFamilies, m_computeQueueFamily);
    AppendUniqueFamily(m_activeQueueFamilies, m_copyQueueFamily);
}

// ── Logical Device Creation ───────────────────────────────────────────────

void VulkanDevice::CreateLogicalDevice(bool enableValidation)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    std::vector<uint32_t> requestedQueueCounts(queueFamilyCount, 0);
    auto requestQueueCount = [&](uint32_t familyIndex, uint32_t queueIndex)
    { requestedQueueCounts[familyIndex] = (std::max)(requestedQueueCounts[familyIndex], queueIndex + 1); };

    requestQueueCount(m_graphicsQueueFamily, m_graphicsQueueIndex);
    requestQueueCount(m_computeQueueFamily, m_computeQueueIndex);
    requestQueueCount(m_copyQueueFamily, m_copyQueueIndex);

    std::vector<std::array<float, 3>> queuePriorities(queueFamilyCount);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(m_activeQueueFamilies.size());
    for (uint32_t familyIndex = 0; familyIndex < queueFamilyCount; ++familyIndex)
    {
        const uint32_t requestedQueueCount = requestedQueueCounts[familyIndex];
        if (requestedQueueCount == 0)
        {
            continue;
        }

        WEST_CHECK(requestedQueueCount <= queueFamilies[familyIndex].queueCount,
                   "Requested Vulkan queue count exceeds family capacity");

        auto& priorities = queuePriorities[familyIndex];
        priorities.fill(1.0f);

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = familyIndex;
        queueCreateInfo.queueCount = requestedQueueCount;
        queueCreateInfo.pQueuePriorities = priorities.data();
        queueCreateInfos.push_back(queueCreateInfo);
    }

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

    VkPhysicalDeviceVulkan11Features supported11{};
    supported11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

    VkPhysicalDeviceVulkan12Features supported12{};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    supported11.pNext = &supported12;
    supported12.pNext = &supported13;
    supported13.pNext = &supportedDescriptorBuffer;

    VkPhysicalDeviceFeatures2 supportedFeatures{};
    supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportedFeatures.pNext = &supported11;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supportedFeatures);

    WEST_CHECK(supported11.shaderDrawParameters == VK_TRUE, "Vulkan shader draw parameters are required");
    WEST_CHECK(supported13.dynamicRendering == VK_TRUE, "Vulkan 1.3 dynamic rendering is required");
    WEST_CHECK(supported13.synchronization2 == VK_TRUE, "Vulkan 1.3 synchronization2 is required");
    WEST_CHECK(supported12.timelineSemaphore == VK_TRUE, "Vulkan timeline semaphore is required");
    WEST_CHECK(supported12.bufferDeviceAddress == VK_TRUE, "Vulkan buffer device address is required");
    WEST_CHECK(supported12.drawIndirectCount == VK_TRUE, "Vulkan drawIndirectCount is required");
    WEST_CHECK(supportedFeatures.features.drawIndirectFirstInstance == VK_TRUE,
               "Vulkan drawIndirectFirstInstance is required for GPU-driven draw indexing");
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

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features11.pNext = &features12;
    features12.pNext = &features13;
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.drawIndirectCount = VK_TRUE;
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
    features2.pNext = &features11;
    features2.features.drawIndirectFirstInstance = VK_TRUE;
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
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
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
            descriptorCapacity,
            descriptorLimitFromRange(descriptorBufferProps.maxSamplerDescriptorBufferRange, m_samplerDescriptorSize));
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
    m_vkGetDescriptorEXT =
        reinterpret_cast<PFN_vkGetDescriptorEXT>(vkGetDeviceProcAddr(m_device, "vkGetDescriptorEXT"));
    m_vkCmdBindDescriptorBuffersEXT = reinterpret_cast<PFN_vkCmdBindDescriptorBuffersEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdBindDescriptorBuffersEXT"));
    m_vkCmdSetDescriptorBufferOffsetsEXT = reinterpret_cast<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdSetDescriptorBufferOffsetsEXT"));

    WEST_CHECK(m_vkGetDescriptorSetLayoutSizeEXT != nullptr, "vkGetDescriptorSetLayoutSizeEXT is unavailable");
    WEST_CHECK(m_vkGetDescriptorSetLayoutBindingOffsetEXT != nullptr,
               "vkGetDescriptorSetLayoutBindingOffsetEXT is unavailable");
    WEST_CHECK(m_vkGetDescriptorEXT != nullptr, "vkGetDescriptorEXT is unavailable");
    WEST_CHECK(m_vkCmdBindDescriptorBuffersEXT != nullptr, "vkCmdBindDescriptorBuffersEXT is unavailable");
    WEST_CHECK(m_vkCmdSetDescriptorBufferOffsetsEXT != nullptr, "vkCmdSetDescriptorBufferOffsetsEXT is unavailable");
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
                       VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    ConfigureQueueSharing(bufferInfo);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocationInfo{};
    WEST_VK_CHECK(vmaCreateBuffer(m_memoryAllocator->GetAllocator(), &bufferInfo, &allocInfo,
                                  &m_bindlessDescriptorBuffer, &m_bindlessDescriptorAllocation, &allocationInfo));
    m_bindlessDescriptorMapped = allocationInfo.pMappedData;
    WEST_CHECK(m_bindlessDescriptorMapped != nullptr, "Failed to map Vulkan bindless descriptor buffer");
    std::memset(m_bindlessDescriptorMapped, 0, static_cast<size_t>(m_bindlessDescriptorBufferSize));

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = m_bindlessDescriptorBuffer;
    m_bindlessDescriptorBufferAddress = vkGetBufferDeviceAddress(m_device, &addressInfo);
    WEST_CHECK(m_bindlessDescriptorBufferAddress != 0, "Failed to get Vulkan descriptor buffer address");

    m_bindlessPool.Initialize(m_bindlessCapacity);
    m_bindlessDescriptorKinds.assign(m_bindlessCapacity, BindlessDescriptorKind::None);
    m_bindlessPendingFree.assign(m_bindlessCapacity, 0);

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan bindless descriptor buffer created (capacity={}, size={} bytes).",
                  m_bindlessCapacity, m_bindlessDescriptorBufferSize);
}

void VulkanDevice::DestroyBindlessDescriptors()
{
    if (m_bindlessDescriptorBuffer && m_memoryAllocator)
    {
        vmaDestroyBuffer(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorBuffer, m_bindlessDescriptorAllocation);
        m_bindlessDescriptorBuffer = VK_NULL_HANDLE;
        m_bindlessDescriptorAllocation = VK_NULL_HANDLE;
        m_bindlessDescriptorMapped = nullptr;
        m_bindlessDescriptorBufferAddress = 0;
    }
    m_bindlessDescriptorKinds.clear();
    m_bindlessPendingFree.clear();

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
    cmdList->Initialize(m_device, GetQueueFamily(type), type, m_bindlessDescriptorBufferAddress,
                        m_vkCmdBindDescriptorBuffersEXT, m_vkCmdSetDescriptorBufferOffsetsEXT);
    return cmdList;
}

uint32_t VulkanDevice::GetQueueFamily(RHIQueueType type) const
{
    switch (type)
    {
    case RHIQueueType::Graphics:
        return m_graphicsQueueFamily;
    case RHIQueueType::Compute:
        return m_computeQueueFamily;
    case RHIQueueType::Copy:
        return m_copyQueueFamily;
    }

    WEST_ASSERT(false);
    return m_graphicsQueueFamily;
}

void VulkanDevice::ConfigureQueueSharing(VkBufferCreateInfo& bufferInfo) const
{
    if (m_activeQueueFamilies.size() > 1)
    {
        bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferInfo.queueFamilyIndexCount = static_cast<uint32_t>(m_activeQueueFamilies.size());
        bufferInfo.pQueueFamilyIndices = m_activeQueueFamilies.data();
        return;
    }

    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0;
    bufferInfo.pQueueFamilyIndices = nullptr;
}

void VulkanDevice::ConfigureQueueSharing(VkImageCreateInfo& imageInfo) const
{
    if (m_activeQueueFamilies.size() > 1)
    {
        imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(m_activeQueueFamilies.size());
        imageInfo.pQueueFamilyIndices = m_activeQueueFamilies.data();
        return;
    }

    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices = nullptr;
}

IRHIQueue* VulkanDevice::GetQueue(RHIQueueType type)
{
    switch (type)
    {
    case RHIQueueType::Graphics:
        return m_graphicsQueue.get();
    case RHIQueueType::Compute:
        return m_computeQueue.get();
    case RHIQueueType::Copy:
        return m_copyQueue.get();
    }

    WEST_ASSERT(false);
    return nullptr;
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

std::unique_ptr<IRHIBuffer> VulkanDevice::CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t /*aliasSlot*/)
{
    return CreateBuffer(desc);
}

std::unique_ptr<IRHITexture> VulkanDevice::CreateTransientTexture(const RHITextureDesc& desc, uint32_t aliasSlot)
{
    if (aliasSlot == UINT32_MAX)
    {
        return CreateTexture(desc);
    }

    std::shared_ptr<VmaAllocation_T> aliasingAllocation;
    {
        std::lock_guard<std::mutex> lock(m_transientTextureMutex);

        if (aliasSlot >= m_transientTextureAliases.size())
        {
            m_transientTextureAliases.resize(aliasSlot + 1);
        }

        TransientTextureAliasEntry& entry = m_transientTextureAliases[aliasSlot];
        aliasingAllocation = entry.allocation.lock();

        if (!aliasingAllocation || !entry.valid || entry.desc != desc)
        {
            VkImageCreateInfo imageInfo = BuildTransientImageCreateInfo(desc);
            ConfigureQueueSharing(imageInfo);

            VkImage probeImage = VK_NULL_HANDLE;
            WEST_VK_CHECK(vkCreateImage(m_device, &imageInfo, nullptr, &probeImage));

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            allocInfo.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;

            VmaAllocation allocation = VK_NULL_HANDLE;
            const VmaAllocator allocatorHandle = m_memoryAllocator->GetAllocator();
            const VkDevice deviceHandle = m_device;
            WEST_VK_CHECK(vmaAllocateMemoryForImage(allocatorHandle, probeImage, &allocInfo, &allocation, nullptr));
            vkDestroyImage(deviceHandle, probeImage, nullptr);

            aliasingAllocation = std::shared_ptr<VmaAllocation_T>(
                allocation,
                [device = this, allocatorHandle](VmaAllocation_T* ptr)
                {
                    const VmaAllocation allocationHandle = ptr;
                    if (!allocationHandle)
                    {
                        return;
                    }

                    if (device && device->GetVkDevice() != VK_NULL_HANDLE)
                    {
                        device->EnqueueDeferredDeletion([allocatorHandle, allocationHandle]()
                                                        { vmaFreeMemory(allocatorHandle, allocationHandle); },
                                                        device->GetCurrentFrameFenceValue());
                    }
                    else
                    {
                        vmaFreeMemory(allocatorHandle, allocationHandle);
                    }
                });

            entry.desc = desc;
            entry.valid = true;
            entry.allocation = aliasingAllocation;

            WEST_LOG_INFO(LogCategory::RHI, "Vulkan transient texture alias slot {} allocated ({}x{}, format={})",
                          aliasSlot, desc.width, desc.height, static_cast<uint32_t>(desc.format));
        }
    }

    auto texture = std::make_unique<VulkanTexture>();
    texture->InitializeAliased(this, desc, std::move(aliasingAllocation));
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
    pipeline->SetOwnerDevice(this);
    pipeline->Initialize(m_device, desc, swapChainFormat, m_bindlessSetLayout);
    return pipeline;
}

std::unique_ptr<IRHIPipeline> VulkanDevice::CreateComputePipeline(const RHIComputePipelineDesc& desc)
{
    auto pipeline = std::make_unique<VulkanPipeline>();
    pipeline->SetOwnerDevice(this);
    pipeline->Initialize(m_device, desc, m_bindlessSetLayout);
    return pipeline;
}

BindlessIndex VulkanDevice::RegisterBindlessResource(IRHIBuffer* buffer, bool /*writable*/)
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
    vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation, descriptorOffset,
                       m_storageBufferDescriptorSize);

    WEST_ASSERT(index < m_bindlessDescriptorKinds.size());
    m_bindlessDescriptorKinds[index] = BindlessDescriptorKind::Buffer;
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
    vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation, descriptorOffset,
                       m_sampledImageDescriptorSize);

    WEST_ASSERT(index < m_bindlessDescriptorKinds.size());
    m_bindlessDescriptorKinds[index] = BindlessDescriptorKind::Texture;
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
    vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation, descriptorOffset,
                       m_samplerDescriptorSize);

    WEST_ASSERT(index < m_bindlessDescriptorKinds.size());
    m_bindlessDescriptorKinds[index] = BindlessDescriptorKind::Sampler;
    vkSampler->SetBindlessIndex(index);
    return index;
}

void VulkanDevice::UnregisterBindlessResource(BindlessIndex index)
{
    const uint64_t fenceValue = GetCurrentFrameFenceValue();
    BindlessDescriptorKind descriptorKind = BindlessDescriptorKind::None;

    {
        std::lock_guard lock(m_bindlessMutex);
        if (!m_bindlessPool.IsAllocated(index) || index >= m_bindlessDescriptorKinds.size() ||
            index >= m_bindlessPendingFree.size())
        {
            WEST_LOG_WARNING(LogCategory::RHI, "Vulkan bindless unregister ignored for invalid index {}", index);
            return;
        }

        if (m_bindlessPendingFree[index] != 0)
        {
            WEST_LOG_WARNING(LogCategory::RHI, "Vulkan bindless unregister ignored for pending index {}", index);
            return;
        }

        descriptorKind = m_bindlessDescriptorKinds[index];
        m_bindlessPendingFree[index] = 1;
    }

    EnqueueDeferredDeletion([this, index, descriptorKind]()
    {
        std::lock_guard lock(m_bindlessMutex);
        if (!m_bindlessPool.Free(index))
        {
            WEST_LOG_WARNING(LogCategory::RHI, "Vulkan deferred bindless free ignored for invalid index {}", index);
        }

        if (index < m_bindlessDescriptorKinds.size())
        {
            m_bindlessDescriptorKinds[index] = BindlessDescriptorKind::None;
        }
        if (index < m_bindlessPendingFree.size())
        {
            m_bindlessPendingFree[index] = 0;
        }

        if (!m_bindlessDescriptorMapped || descriptorKind == BindlessDescriptorKind::None)
        {
            return;
        }

        VkDeviceSize descriptorOffset = 0;
        VkDeviceSize descriptorSize = 0;
        switch (descriptorKind)
        {
        case BindlessDescriptorKind::Texture:
            descriptorOffset = m_textureDescriptorOffset + index * m_sampledImageDescriptorSize;
            descriptorSize = m_sampledImageDescriptorSize;
            break;
        case BindlessDescriptorKind::Sampler:
            descriptorOffset = m_samplerDescriptorOffset + index * m_samplerDescriptorSize;
            descriptorSize = m_samplerDescriptorSize;
            break;
        case BindlessDescriptorKind::Buffer:
            descriptorOffset = m_bufferDescriptorOffset + index * m_storageBufferDescriptorSize;
            descriptorSize = m_storageBufferDescriptorSize;
            break;
        case BindlessDescriptorKind::None:
        default:
            return;
        }

        std::memset(static_cast<uint8_t*>(m_bindlessDescriptorMapped) + descriptorOffset, 0,
                    static_cast<size_t>(descriptorSize));
        vmaFlushAllocation(m_memoryAllocator->GetAllocator(), m_bindlessDescriptorAllocation, descriptorOffset,
                           descriptorSize);
    }, fenceValue);
}

} // namespace west::rhi
