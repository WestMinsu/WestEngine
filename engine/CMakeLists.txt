# =============================================================================
# WestEngine - Engine Library Target
# =============================================================================

set(WEST_ENGINE_SOURCES
    core/Logger.cpp
    core/Memory/LinearAllocator.cpp
    core/Memory/PoolAllocator.cpp
    core/Memory/StackAllocator.cpp
    core/Threading/TaskSystem.cpp
    core/Timer.cpp
    editor/ImGuiPass.cpp
    editor/ImGuiRenderer.cpp
    editor/PostProcessingPresets.cpp
    editor/Profiler/FrameTelemetry.cpp
    editor/Profiler/GPUTimerManager.cpp
    editor/RuntimeControlPanel.cpp
    editor/TelemetryOverlay.cpp
    render/Passes/BokehDOFPass.cpp
    render/Passes/BufferCopyPass.cpp
    render/Passes/DeferredLightingPass.cpp
    render/Passes/ForwardTexturedQuadPass.cpp
    render/Passes/GBufferPass.cpp
    render/Passes/GPUDrivenCullingPass.cpp
    render/Passes/ShadowMapPass.cpp
    render/Passes/SSAOPass.cpp
    render/Passes/ToneMappingPass.cpp
    render/RenderGraph/CommandListPool.cpp
    render/RenderGraph/RenderGraph.cpp
    render/RenderGraph/RenderGraphCompiler.cpp
    render/RenderGraph/TransientResourcePool.cpp
    rhi/RHIFactory.cpp
    rhi/common/BindlessPool.cpp
    rhi/common/DeferredDeletionQueue.cpp
    rhi/common/FormatConversion.cpp
    scene/Camera.cpp
    scene/FreeLookCameraController.cpp
    scene/ImageData.cpp
    scene/MeshLoader.cpp
    scene/SceneAsset.cpp
    scene/SceneAssetLoader.cpp
    scene/SceneGraph.cpp
    scene/TextureAssetData.cpp
    shader/PSOCache.cpp
    shader/ShaderCompiler.cpp
)

if(WIN32)
    list(APPEND WEST_ENGINE_SOURCES
        platform/win32/Win32Application.cpp
        platform/win32/Win32Window.cpp
    )
endif()

if(WIN32 AND WEST_HAS_DX12)
    list(APPEND WEST_ENGINE_SOURCES
        rhi/dx12/DX12Buffer.cpp
        rhi/dx12/DX12CommandList.cpp
        rhi/dx12/DX12Device.cpp
        rhi/dx12/DX12Fence.cpp
        rhi/dx12/DX12MemoryAllocator.cpp
        rhi/dx12/DX12Pipeline.cpp
        rhi/dx12/DX12Queue.cpp
        rhi/dx12/DX12Sampler.cpp
        rhi/dx12/DX12SwapChain.cpp
        rhi/dx12/DX12Texture.cpp
        rhi/dx12/DX12TimestampQueryPool.cpp
    )
endif()

if(WEST_HAS_VULKAN)
    list(APPEND WEST_ENGINE_SOURCES
        rhi/vulkan/VulkanBuffer.cpp
        rhi/vulkan/VulkanCommandList.cpp
        rhi/vulkan/VulkanDevice.cpp
        rhi/vulkan/VulkanFence.cpp
        rhi/vulkan/VulkanMemoryAllocator.cpp
        rhi/vulkan/VulkanPipeline.cpp
        rhi/vulkan/VulkanQueue.cpp
        rhi/vulkan/VulkanSampler.cpp
        rhi/vulkan/VulkanSemaphore.cpp
        rhi/vulkan/VulkanSwapChain.cpp
        rhi/vulkan/VulkanTexture.cpp
        rhi/vulkan/VulkanTimestampQueryPool.cpp
    )
endif()

add_library(Engine STATIC ${WEST_ENGINE_SOURCES})

file(GLOB_RECURSE WEST_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/render/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/render/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/render/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.inl"
)

target_sources(Engine PRIVATE ${WEST_ENGINE_HEADERS})

file(GLOB_RECURSE WEST_ALL_SLANG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shaders/*.slang"
)

if(WEST_SHADER_SOURCES OR WEST_ALL_SLANG_SOURCES)
    set(WEST_VISIBLE_SHADER_SOURCES ${WEST_SHADER_SOURCES} ${WEST_ALL_SLANG_SOURCES})
    list(REMOVE_DUPLICATES WEST_VISIBLE_SHADER_SOURCES)
    target_sources(Engine PRIVATE ${WEST_VISIBLE_SHADER_SOURCES})
    set_source_files_properties(${WEST_VISIBLE_SHADER_SOURCES}
        PROPERTIES
            HEADER_FILE_ONLY TRUE
    )
    source_group(TREE "${CMAKE_SOURCE_DIR}/shaders" PREFIX "Shaders" FILES ${WEST_VISIBLE_SHADER_SOURCES})
endif()

if(WEST_SHADER_BUILD_OUTPUTS)
    set(WEST_SHADER_METADATA_DEPENDENT_SOURCES
        editor/ImGuiRenderer.cpp
        render/Passes/BokehDOFPass.cpp
        render/Passes/DeferredLightingPass.cpp
        render/Passes/ForwardTexturedQuadPass.cpp
        render/Passes/GBufferPass.cpp
        render/Passes/GPUDrivenCullingPass.cpp
        render/Passes/ShadowMapPass.cpp
        render/Passes/SSAOPass.cpp
        render/Passes/ToneMappingPass.cpp
    )
    set_source_files_properties(${WEST_SHADER_METADATA_DEPENDENT_SOURCES}
        PROPERTIES
            OBJECT_DEPENDS "${WEST_SHADER_BUILD_OUTPUTS}"
    )
endif()

file(TO_CMAKE_PATH "${WEST_SHADER_OUTPUT_DIR}" WEST_SHADER_OUTPUT_DIR_CPP)

target_compile_definitions(Engine
    PUBLIC
        WEST_SHADER_OUTPUT_DIR="${WEST_SHADER_OUTPUT_DIR_CPP}"
        $<$<AND:$<BOOL:${WEST_HAS_TRACY}>,$<CONFIG:Debug>>:TRACY_ENABLE>
        $<$<AND:$<BOOL:${WEST_HAS_TRACY}>,$<CONFIG:RelWithDebInfo>>:TRACY_ENABLE>
)

if(WIN32 AND WEST_HAS_DX12)
    target_compile_definitions(Engine
        PRIVATE
            WEST_HAS_DX12_BACKEND
    )
endif()

if(WEST_HAS_VULKAN)
    target_compile_definitions(Engine
        PRIVATE
            VK_API_VERSION_1_3
            WEST_HAS_VULKAN_BACKEND
    )
endif()

target_include_directories(Engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CGLTF_INCLUDE_DIRS}
        ${Stb_INCLUDE_DIR}
)

target_link_libraries(Engine
    PUBLIC
        ${WEST_IMGUI_TARGET}
    PRIVATE
        assimp::assimp
)

if(WEST_HAS_TRACY)
    target_link_libraries(Engine
        PUBLIC
            Tracy::TracyClient
    )
endif()

if(WIN32 AND WEST_HAS_DX12)
    target_link_libraries(Engine
        PRIVATE
            d3d12.lib
            dxgi.lib
            dxguid.lib
    )

    if(WEST_HAS_D3D12MA)
        target_link_libraries(Engine PRIVATE GPUOpen::D3D12MemoryAllocator)
    endif()
endif()

if(WEST_HAS_VULKAN)
    target_link_libraries(Engine
        PUBLIC
            Vulkan::Vulkan
    )

    if(WEST_HAS_VMA)
        target_link_libraries(Engine PRIVATE GPUOpen::VulkanMemoryAllocator)
    endif()
endif()

west_set_compiler_options(Engine)

# Backward-compatible aliases keep tests and local CMake references stable while
# Visual Studio shows a single engine project.
add_library(west_core ALIAS Engine)
add_library(west_editor ALIAS Engine)
add_library(west_engine_runtime ALIAS Engine)
add_library(west_platform_win32 ALIAS Engine)
add_library(west_render ALIAS Engine)
add_library(west_rhi_common ALIAS Engine)
add_library(west_rhi_dx12 ALIAS Engine)
add_library(west_rhi_interface ALIAS Engine)
add_library(west_rhi_vulkan ALIAS Engine)
add_library(west_scene ALIAS Engine)
add_library(west_shader ALIAS Engine)
