// d3d12_names.h - names of D3D12 enum values and flags for descriptions (the API Calls window, the Pipeline view).
// Flags join their bits' names with '|'. The returned text lives until the thread's next call of the same function.
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

const char* EnumName(DXGI_FORMAT v);
const char* EnumName(D3D12_RESOURCE_DIMENSION v);
const char* EnumName(D3D12_HEAP_TYPE v);
const char* EnumName(D3D12_CPU_PAGE_PROPERTY v);
const char* EnumName(D3D12_MEMORY_POOL v);
const char* EnumName(D3D12_TEXTURE_LAYOUT v);
const char* EnumName(D3D12_DESCRIPTOR_HEAP_TYPE v);
const char* EnumName(D3D12_COMMAND_LIST_TYPE v);
const char* EnumName(D3D12_PRIMITIVE_TOPOLOGY v);
const char* EnumName(D3D12_PRIMITIVE_TOPOLOGY_TYPE v);
const char* EnumName(D3D12_FILL_MODE v);
const char* EnumName(D3D12_CULL_MODE v);
const char* EnumName(D3D12_COMPARISON_FUNC v);
const char* EnumName(D3D12_BLEND v);
const char* EnumName(D3D12_BLEND_OP v);
const char* EnumName(D3D12_LOGIC_OP v);
const char* EnumName(D3D12_STENCIL_OP v);
const char* EnumName(D3D12_DEPTH_WRITE_MASK v);
const char* EnumName(D3D12_FILTER v);
const char* EnumName(D3D12_TEXTURE_ADDRESS_MODE v);
const char* EnumName(D3D12_SRV_DIMENSION v);
const char* EnumName(D3D12_UAV_DIMENSION v);
const char* EnumName(D3D12_RTV_DIMENSION v);
const char* EnumName(D3D12_DSV_DIMENSION v);
const char* EnumName(D3D12_INPUT_CLASSIFICATION v);
const char* EnumName(D3D12_QUERY_TYPE v);
const char* EnumName(D3D12_QUERY_HEAP_TYPE v);
const char* EnumName(D3D12_RESOURCE_BARRIER_TYPE v);
const char* EnumName(D3D12_TEXTURE_COPY_TYPE v);
const char* EnumName(D3D12_INDIRECT_ARGUMENT_TYPE v);
const char* EnumName(D3D12_BARRIER_TYPE v);
const char* EnumName(D3D12_BARRIER_LAYOUT v);
const char* EnumName(D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE v);
const char* EnumName(D3D12_RENDER_PASS_ENDING_ACCESS_TYPE v);
const char* EnumName(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE v);
const char* EnumName(D3D12_CONSERVATIVE_RASTERIZATION_MODE v);
const char* EnumName(D3D12_LINE_RASTERIZATION_MODE v);
const char* EnumName(D3D12_PREDICATION_OP v);
const char* EnumName(D3D12_RESOLVE_MODE v);
const char* EnumName(D3D12_SHADING_RATE v);
const char* EnumName(D3D_FEATURE_LEVEL v);
const char* EnumName(D3D12_COMMAND_QUEUE_PRIORITY v);

// Flags.
const char* EnumName(D3D12_RESOURCE_STATES v);
const char* EnumName(D3D12_RESOURCE_FLAGS v);
const char* EnumName(D3D12_HEAP_FLAGS v);
const char* EnumName(D3D12_CLEAR_FLAGS v);
const char* EnumName(D3D12_BARRIER_SYNC v);
const char* EnumName(D3D12_BARRIER_ACCESS v);
const char* EnumName(D3D12_DESCRIPTOR_HEAP_FLAGS v);
const char* EnumName(D3D12_COLOR_WRITE_ENABLE v);
const char* EnumName(D3D12_BUFFER_SRV_FLAGS v);
const char* EnumName(D3D12_BUFFER_UAV_FLAGS v);
const char* EnumName(D3D12_DSV_FLAGS v);
const char* EnumName(D3D12_RENDER_PASS_FLAGS v);
const char* EnumName(D3D12_COMMAND_QUEUE_FLAGS v);
const char* EnumName(D3D12_FENCE_FLAGS v);
const char* EnumName(D3D12_PIPELINE_STATE_FLAGS v);
const char* EnumName(D3D12_TEXTURE_BARRIER_FLAGS v);
