// Shared header - a namespaced enum + struct, mirroring an engine's RendererAPI pattern.
namespace EngineAPI {
    enum RendererAPI { DX12 = 0, Vulkan = 1 };
    struct Config { int32 api; };
}

void TestEnumForwardRef();
void TestEnumCrossUnit();
