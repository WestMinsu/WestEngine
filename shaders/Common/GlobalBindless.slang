// =============================================================================
// WestEngine - Shaders
// Global bindless descriptor access for Slang shaders
// =============================================================================

[vk::binding(0, 0)]
__DynamicResource<__DynamicResourceKind.General> g_textureDescriptors[];

[vk::binding(1, 0)]
__DynamicResource<__DynamicResourceKind.Sampler> g_samplerDescriptors[];

[vk::binding(2, 0)]
__DynamicResource<__DynamicResourceKind.General> g_bufferDescriptors[];

export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle) where T : IOpaqueDescriptor
{
    __target_switch
    {
    case spirv:
        uint index = ((uint2)handle).x;
        if (T.kind == DescriptorKind.Sampler)
        {
            return g_samplerDescriptors[index].asOpaqueDescriptor<T>();
        }
        if (T.kind == DescriptorKind.Buffer)
        {
            return g_bufferDescriptors[index].asOpaqueDescriptor<T>();
        }
        return g_textureDescriptors[index].asOpaqueDescriptor<T>();
    default:
        return defaultGetDescriptorFromHandle(handle);
    }
}
