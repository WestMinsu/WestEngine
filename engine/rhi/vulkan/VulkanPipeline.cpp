// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan pipeline implementation — Phase 2 minimal graphics pipeline
// =============================================================================
#include "rhi/vulkan/VulkanPipeline.h"

#include "rhi/common/FormatConversion.h"
#include "rhi/interface/RHIDescriptors.h"
#include "rhi/vulkan/VulkanDevice.h"

#include <utility>
#include <vector>

namespace west::rhi
{

static VkPrimitiveTopology ToVkPrimitiveTopology(RHIPrimitiveTopology topology)
{
    switch (topology)
    {
    case RHIPrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case RHIPrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case RHIPrimitiveTopology::PointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case RHIPrimitiveTopology::TriangleList:
    default:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static VkCompareOp ToVkCompareOp(RHICompareOp op)
{
    switch (op)
    {
    case RHICompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case RHICompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case RHICompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case RHICompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHICompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case RHICompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case RHICompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHICompareOp::Always:
    default:
        return VK_COMPARE_OP_ALWAYS;
    }
}

static VkBlendFactor ToVkBlendFactor(RHIBlendFactor factor)
{
    switch (factor)
    {
    case RHIBlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case RHIBlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case RHIBlendFactor::SrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case RHIBlendFactor::OneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case RHIBlendFactor::DstAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case RHIBlendFactor::OneMinusDstAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case RHIBlendFactor::SrcColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case RHIBlendFactor::OneMinusSrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case RHIBlendFactor::DstColor:
        return VK_BLEND_FACTOR_DST_COLOR;
    case RHIBlendFactor::OneMinusDstColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp ToVkBlendOp(RHIBlendOp op)
{
    switch (op)
    {
    case RHIBlendOp::Add:
        return VK_BLEND_OP_ADD;
    case RHIBlendOp::Subtract:
        return VK_BLEND_OP_SUBTRACT;
    case RHIBlendOp::RevSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case RHIBlendOp::Min:
        return VK_BLEND_OP_MIN;
    case RHIBlendOp::Max:
        return VK_BLEND_OP_MAX;
    default:
        return VK_BLEND_OP_ADD;
    }
}

static VkPipelineLayout CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout bindlessSetLayout,
                                             uint32_t pushConstantSizeBytes)
{
    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_ALL;
    pushConstants.offset = 0;
    pushConstants.size = pushConstantSizeBytes;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = bindlessSetLayout != VK_NULL_HANDLE ? 1 : 0;
    layoutInfo.pSetLayouts = bindlessSetLayout != VK_NULL_HANDLE ? &bindlessSetLayout : nullptr;
    layoutInfo.pushConstantRangeCount = pushConstantSizeBytes > 0 ? 1 : 0;
    layoutInfo.pPushConstantRanges = pushConstantSizeBytes > 0 ? &pushConstants : nullptr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    WEST_VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout));
    return layout;
}

VulkanPipeline::~VulkanPipeline()
{
    if (m_device == VK_NULL_HANDLE)
    {
        return;
    }

    VkPipeline retiredPipeline = std::exchange(m_pipeline, VK_NULL_HANDLE);
    VkPipelineLayout retiredPipelineLayout = std::exchange(m_pipelineLayout, VK_NULL_HANDLE);
    if (retiredPipeline == VK_NULL_HANDLE && retiredPipelineLayout == VK_NULL_HANDLE)
    {
        return;
    }

    if (m_ownerDevice && m_ownerDevice->GetVkDevice() != VK_NULL_HANDLE)
    {
        const VkDevice device = m_device;
        m_ownerDevice->EnqueueDeferredDeletion(
            [device, retiredPipeline, retiredPipelineLayout]()
            {
                if (retiredPipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(device, retiredPipeline, nullptr);
                }
                if (retiredPipelineLayout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(device, retiredPipelineLayout, nullptr);
                }
            },
            m_ownerDevice->GetCurrentFrameFenceValue());
        return;
    }

    if (retiredPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_device, retiredPipeline, nullptr);
    }
    if (retiredPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(m_device, retiredPipelineLayout, nullptr);
    }
}

void VulkanPipeline::Initialize(VkDevice device, const RHIGraphicsPipelineDesc& desc, VkFormat swapChainFormat,
                                VkDescriptorSetLayout bindlessSetLayout)
{
    WEST_ASSERT(device != VK_NULL_HANDLE);
    WEST_ASSERT(desc.pushConstantSizeBytes <= kMaxPushConstantSizeBytes);
    WEST_ASSERT((desc.pushConstantSizeBytes % sizeof(uint32_t)) == 0);
    (void)swapChainFormat;
    m_device = device;
    m_type = RHIPipelineType::Graphics;
    m_pipelineLayout = CreatePipelineLayout(device, bindlessSetLayout, desc.pushConstantSizeBytes);

    // ── Shader Modules ──
    VkShaderModuleCreateInfo vsModuleInfo{};
    vsModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsModuleInfo.codeSize = desc.vertexShader.size();
    vsModuleInfo.pCode = reinterpret_cast<const uint32_t*>(desc.vertexShader.data());

    VkShaderModule vsModule;
    WEST_VK_CHECK(vkCreateShaderModule(device, &vsModuleInfo, nullptr, &vsModule));

    VkShaderModuleCreateInfo fsModuleInfo{};
    fsModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsModuleInfo.codeSize = desc.fragmentShader.size();
    fsModuleInfo.pCode = reinterpret_cast<const uint32_t*>(desc.fragmentShader.data());

    VkShaderModule fsModule;
    WEST_VK_CHECK(vkCreateShaderModule(device, &fsModuleInfo, nullptr, &fsModule));

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vsModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fsModule;
    shaderStages[1].pName = "main";

    // ── Vertex Input ──
    std::vector<VkVertexInputAttributeDescription> vertexAttribs;
    for (uint32_t i = 0; i < desc.vertexAttributes.size(); ++i)
    {
        const auto& attr = desc.vertexAttributes[i];
        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = i;
        vkAttr.binding = 0;
        vkAttr.format = static_cast<VkFormat>(ToVkFormat(attr.format));
        vkAttr.offset = attr.offset;
        vertexAttribs.push_back(vkAttr);
    }

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = desc.vertexStride;
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = vertexAttribs.empty() ? 0u : 1u;
    vertexInputInfo.pVertexBindingDescriptions = vertexAttribs.empty() ? nullptr : &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttribs.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttribs.empty() ? nullptr : vertexAttribs.data();

    // ── Input Assembly ──
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = ToVkPrimitiveTopology(desc.topology);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ── Dynamic State (Viewport + Scissor) ──
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // ── Rasterizer ──
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode =
        (desc.fillMode == RHIFillMode::Wireframe) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.cullMode =
        (desc.cullMode == RHICullMode::None)  ? VK_CULL_MODE_NONE :
        (desc.cullMode == RHICullMode::Front) ? VK_CULL_MODE_FRONT_BIT :
                                                 VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    // ── Multisampling ──
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── Depth Stencil ──
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = ToVkCompareOp(desc.depthCompare);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // ── Color Blend ──
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(static_cast<uint32_t>(desc.colorFormats.size()));
    for (VkPipelineColorBlendAttachmentState& colorBlendAttachment : colorBlendAttachments)
    {
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    for (uint32_t i = 0; i < desc.blendAttachments.size() && i < colorBlendAttachments.size(); ++i)
    {
        const RHIBlendAttachment& attachment = desc.blendAttachments[i];
        VkPipelineColorBlendAttachmentState& colorBlendAttachment = colorBlendAttachments[i];
        colorBlendAttachment.blendEnable = attachment.blendEnable ? VK_TRUE : VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = ToVkBlendFactor(attachment.srcColor);
        colorBlendAttachment.dstColorBlendFactor = ToVkBlendFactor(attachment.dstColor);
        colorBlendAttachment.colorBlendOp = ToVkBlendOp(attachment.colorOp);
        colorBlendAttachment.srcAlphaBlendFactor = ToVkBlendFactor(attachment.srcAlpha);
        colorBlendAttachment.dstAlphaBlendFactor = ToVkBlendFactor(attachment.dstAlpha);
        colorBlendAttachment.alphaBlendOp = ToVkBlendOp(attachment.alphaOp);
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.logicOpEnable = VK_FALSE;
    colorBlend.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlend.pAttachments = colorBlendAttachments.data();

    // ── Dynamic Rendering (Vulkan 1.3) ──
    std::vector<VkFormat> colorFormats;
    if (!desc.colorFormats.empty())
    {
        colorFormats.reserve(desc.colorFormats.size());
        for (const RHIFormat colorFormat : desc.colorFormats)
        {
            colorFormats.push_back(static_cast<VkFormat>(ToVkFormat(colorFormat)));
        }
    }

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat =
        desc.depthFormat != RHIFormat::Unknown ? static_cast<VkFormat>(ToVkFormat(desc.depthFormat))
                                               : VK_FORMAT_UNDEFINED;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // ── Create Pipeline ──
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // Dynamic Rendering
    pipelineInfo.subpass = 0;

    WEST_VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));

    // Cleanup shader modules (no longer needed after pipeline creation)
    vkDestroyShaderModule(device, vsModule, nullptr);
    vkDestroyShaderModule(device, fsModule, nullptr);

    m_psoHash = desc.psoHash != 0 ? desc.psoHash : desc.vertexShader.size() ^ (desc.fragmentShader.size() << 16);

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan Graphics Pipeline created: {}",
                  desc.debugName ? desc.debugName : "unnamed");
}

void VulkanPipeline::Initialize(VkDevice device, const RHIComputePipelineDesc& desc,
                                VkDescriptorSetLayout bindlessSetLayout)
{
    WEST_ASSERT(device != VK_NULL_HANDLE);
    WEST_ASSERT(desc.pushConstantSizeBytes <= kMaxPushConstantSizeBytes);
    WEST_ASSERT((desc.pushConstantSizeBytes % sizeof(uint32_t)) == 0);
    m_device = device;
    m_type = RHIPipelineType::Compute;
    m_pipelineLayout = CreatePipelineLayout(device, bindlessSetLayout, desc.pushConstantSizeBytes);

    VkShaderModuleCreateInfo csModuleInfo{};
    csModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    csModuleInfo.codeSize = desc.computeShader.size();
    csModuleInfo.pCode = reinterpret_cast<const uint32_t*>(desc.computeShader.data());

    VkShaderModule csModule = VK_NULL_HANDLE;
    WEST_VK_CHECK(vkCreateShaderModule(device, &csModuleInfo, nullptr, &csModule));

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = csModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_pipelineLayout;

    WEST_VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));

    vkDestroyShaderModule(device, csModule, nullptr);

    m_psoHash = desc.psoHash != 0 ? desc.psoHash : desc.computeShader.size();

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan Compute Pipeline created: {}",
                  desc.debugName ? desc.debugName : "unnamed");
}

} // namespace west::rhi
