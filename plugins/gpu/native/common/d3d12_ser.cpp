// The D3D12 structures and commands, each serialized once for writing, reading and describing (ser.h).
#include "d3d12_records.h"
#include "d3d12_names.h"
#include "formats.h"

using fb::Ser;
using fb::u8;
using fb::u32;
using fb::u64;

// --- API structures --------------------------------------------------------------------------------------------------

void Serialize(Ser& s, D3D12_HEAP_PROPERTIES& v) {
	s.Field("Type", v.Type);
	s.Field("CPUPageProperty", v.CPUPageProperty);
	s.Field("MemoryPoolPreference", v.MemoryPoolPreference);
	s.Field("CreationNodeMask", v.CreationNodeMask);
	s.Field("VisibleNodeMask", v.VisibleNodeMask);
}

void Serialize(Ser& s, D3D12_HEAP_DESC& v) {
	s.Field("SizeInBytes", v.SizeInBytes);
	s.Field("Properties", v.Properties);
	s.Field("Alignment", v.Alignment);
	s.Field("Flags", v.Flags);
}

void Serialize(Ser& s, DXGI_SAMPLE_DESC& v) {
	s.Field("Count", v.Count);
	s.Field("Quality", v.Quality);
}

void Serialize(Ser& s, D3D12_RESOURCE_DESC1& v) {
	s.Field("Dimension", v.Dimension);
	s.Field("Alignment", v.Alignment);
	s.Field("Width", v.Width);
	s.Field("Height", v.Height);
	s.Field("DepthOrArraySize", v.DepthOrArraySize);
	s.Field("MipLevels", v.MipLevels);
	s.Field("Format", v.Format);
	s.Field("SampleDesc", v.SampleDesc);
	s.Field("Layout", v.Layout);
	s.Field("Flags", v.Flags);
	s.Field("SamplerFeedbackMipRegionWidth", v.SamplerFeedbackMipRegion.Width);
	s.Field("SamplerFeedbackMipRegionHeight", v.SamplerFeedbackMipRegion.Height);
	s.Field("SamplerFeedbackMipRegionDepth", v.SamplerFeedbackMipRegion.Depth);
}

void Serialize(Ser& s, D3D12_CLEAR_VALUE& v) {
	s.Field("Format", v.Format);
	const fb::FormatInfo* info = fb::DxgiFormat(v.Format);
	if (info && info->depth) {
		s.Field("Depth", v.DepthStencil.Depth);
		s.Field("Stencil", v.DepthStencil.Stencil);
	}
	else s.Fixed("Color", v.Color);
}

void Serialize(Ser& s, D3D12_DESCRIPTOR_HEAP_DESC& v) {
	s.Field("Type", v.Type);
	s.Field("NumDescriptors", v.NumDescriptors);
	s.Field("Flags", v.Flags);
	s.Field("NodeMask", v.NodeMask);
}

void Serialize(Ser& s, D3D12_SHADER_BYTECODE& v) { s.Blob("Bytecode", v.pShaderBytecode, v.BytecodeLength); }

void Serialize(Ser& s, D3D12_SO_DECLARATION_ENTRY& v) {
	s.Field("Stream", v.Stream);
	s.Str("SemanticName", v.SemanticName);
	s.Field("SemanticIndex", v.SemanticIndex);
	s.Field("StartComponent", v.StartComponent);
	s.Field("ComponentCount", v.ComponentCount);
	s.Field("OutputSlot", v.OutputSlot);
}

void Serialize(Ser& s, D3D12_STREAM_OUTPUT_DESC& v) {
	s.Array("Entries", v.pSODeclaration, v.NumEntries);
	s.Array("BufferStrides", v.pBufferStrides, v.NumStrides);
	s.Field("RasterizedStream", v.RasterizedStream);
}

void Serialize(Ser& s, D3D12_RENDER_TARGET_BLEND_DESC& v) {
	s.Field("BlendEnable", v.BlendEnable);
	s.Field("LogicOpEnable", v.LogicOpEnable);
	s.Field("SrcBlend", v.SrcBlend);
	s.Field("DestBlend", v.DestBlend);
	s.Field("BlendOp", v.BlendOp);
	s.Field("SrcBlendAlpha", v.SrcBlendAlpha);
	s.Field("DestBlendAlpha", v.DestBlendAlpha);
	s.Field("BlendOpAlpha", v.BlendOpAlpha);
	s.Field("LogicOp", v.LogicOp);
	D3D12_COLOR_WRITE_ENABLE mask = (D3D12_COLOR_WRITE_ENABLE)v.RenderTargetWriteMask;
	s.Field("RenderTargetWriteMask", mask);
	v.RenderTargetWriteMask = (UINT8)mask;
}

void Serialize(Ser& s, D3D12_BLEND_DESC& v) {
	s.Field("AlphaToCoverageEnable", v.AlphaToCoverageEnable);
	s.Field("IndependentBlendEnable", v.IndependentBlendEnable);
	s.Fixed("RenderTarget", v.RenderTarget);
}

void Serialize(Ser& s, D3D12_RASTERIZER_DESC2& v) {
	s.Field("FillMode", v.FillMode);
	s.Field("CullMode", v.CullMode);
	s.Field("FrontCounterClockwise", v.FrontCounterClockwise);
	s.Field("DepthBias", v.DepthBias);
	s.Field("DepthBiasClamp", v.DepthBiasClamp);
	s.Field("SlopeScaledDepthBias", v.SlopeScaledDepthBias);
	s.Field("DepthClipEnable", v.DepthClipEnable);
	s.Field("LineRasterizationMode", v.LineRasterizationMode);
	s.Field("ForcedSampleCount", v.ForcedSampleCount);
	s.Field("ConservativeRaster", v.ConservativeRaster);
}

void Serialize(Ser& s, D3D12_DEPTH_STENCILOP_DESC1& v) {
	s.Field("StencilFailOp", v.StencilFailOp);
	s.Field("StencilDepthFailOp", v.StencilDepthFailOp);
	s.Field("StencilPassOp", v.StencilPassOp);
	s.Field("StencilFunc", v.StencilFunc);
	s.Field("StencilReadMask", v.StencilReadMask);
	s.Field("StencilWriteMask", v.StencilWriteMask);
}

void Serialize(Ser& s, D3D12_DEPTH_STENCIL_DESC2& v) {
	s.Field("DepthEnable", v.DepthEnable);
	s.Field("DepthWriteMask", v.DepthWriteMask);
	s.Field("DepthFunc", v.DepthFunc);
	s.Field("StencilEnable", v.StencilEnable);
	s.Field("FrontFace", v.FrontFace);
	s.Field("BackFace", v.BackFace);
	s.Field("DepthBoundsTestEnable", v.DepthBoundsTestEnable);
}

void Serialize(Ser& s, D3D12_INPUT_ELEMENT_DESC& v) {
	s.Str("SemanticName", v.SemanticName);
	s.Field("SemanticIndex", v.SemanticIndex);
	s.Field("Format", v.Format);
	s.Field("InputSlot", v.InputSlot);
	s.Field("AlignedByteOffset", v.AlignedByteOffset);
	s.Field("InputSlotClass", v.InputSlotClass);
	s.Field("InstanceDataStepRate", v.InstanceDataStepRate);
}

void Serialize(Ser& s, D3D12_INPUT_LAYOUT_DESC& v) { s.Array("Elements", v.pInputElementDescs, v.NumElements); }

void Serialize(Ser& s, D3D12_RT_FORMAT_ARRAY& v) {
	s.Field("NumRenderTargets", v.NumRenderTargets);
	s.Fixed("RTFormats", v.RTFormats);
}

void Serialize(Ser& s, D3D12_VIEW_INSTANCE_LOCATION& v) {
	s.Field("ViewportArrayIndex", v.ViewportArrayIndex);
	s.Field("RenderTargetArrayIndex", v.RenderTargetArrayIndex);
}

void Serialize(Ser& s, D3D12_VIEW_INSTANCING_DESC& v) {
	s.Array("Locations", v.pViewInstanceLocations, v.ViewInstanceCount);
	s.Field("Flags", v.Flags);
}

void Serialize(Ser& s, D3D12_INDIRECT_ARGUMENT_DESC& v) {
	s.Field("Type", v.Type);
	switch (v.Type) {
		case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW: s.Field("Slot", v.VertexBuffer.Slot); break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
			s.Field("RootParameterIndex", v.Constant.RootParameterIndex);
			s.Field("DestOffsetIn32BitValues", v.Constant.DestOffsetIn32BitValues);
			s.Field("Num32BitValuesToSet", v.Constant.Num32BitValuesToSet);
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
		case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
		case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
			s.Field("RootParameterIndex", v.ConstantBufferView.RootParameterIndex);
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT:
			s.Field("RootParameterIndex", v.IncrementingConstant.RootParameterIndex);
			s.Field("DestOffsetIn32BitValues", v.IncrementingConstant.DestOffsetIn32BitValues);
			break;
		default: break;
	}
}

void Serialize(Ser& s, D3D12_COMMAND_SIGNATURE_DESC& v) {
	s.Field("ByteStride", v.ByteStride);
	s.Array("Arguments", v.pArgumentDescs, v.NumArgumentDescs);
	s.Field("NodeMask", v.NodeMask);
}

void Serialize(Ser& s, D3D12_QUERY_HEAP_DESC& v) {
	s.Field("Type", v.Type);
	s.Field("Count", v.Count);
	s.Field("NodeMask", v.NodeMask);
}

void Serialize(Ser& s, D3D12_COMMAND_QUEUE_DESC& v) {
	s.Field("Type", v.Type);
	s.Field("Priority", v.Priority);
	s.Field("Flags", v.Flags);
	s.Field("NodeMask", v.NodeMask);
}

void Serialize(Ser& s, D3D12_CONSTANT_BUFFER_VIEW_DESC& v) {
	s.Address("BufferLocation", v.BufferLocation);
	s.Field("SizeInBytes", v.SizeInBytes);
}

void Serialize(Ser& s, D3D12_SHADER_RESOURCE_VIEW_DESC& v) {
	s.Field("Format", v.Format);
	s.Field("ViewDimension", v.ViewDimension);
	s.Field("Shader4ComponentMapping", v.Shader4ComponentMapping);
	switch (v.ViewDimension) {
		case D3D12_SRV_DIMENSION_BUFFER:
			s.Field("FirstElement", v.Buffer.FirstElement);
			s.Field("NumElements", v.Buffer.NumElements);
			s.Field("StructureByteStride", v.Buffer.StructureByteStride);
			s.Field("Flags", v.Buffer.Flags);
			break;
		case D3D12_SRV_DIMENSION_TEXTURE1D:
			s.Field("MostDetailedMip", v.Texture1D.MostDetailedMip);
			s.Field("MipLevels", v.Texture1D.MipLevels);
			s.Field("ResourceMinLODClamp", v.Texture1D.ResourceMinLODClamp);
			break;
		case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
			s.Field("MostDetailedMip", v.Texture1DArray.MostDetailedMip);
			s.Field("MipLevels", v.Texture1DArray.MipLevels);
			s.Field("FirstArraySlice", v.Texture1DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture1DArray.ArraySize);
			s.Field("ResourceMinLODClamp", v.Texture1DArray.ResourceMinLODClamp);
			break;
		case D3D12_SRV_DIMENSION_TEXTURE2D:
			s.Field("MostDetailedMip", v.Texture2D.MostDetailedMip);
			s.Field("MipLevels", v.Texture2D.MipLevels);
			s.Field("PlaneSlice", v.Texture2D.PlaneSlice);
			s.Field("ResourceMinLODClamp", v.Texture2D.ResourceMinLODClamp);
			break;
		case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
			s.Field("MostDetailedMip", v.Texture2DArray.MostDetailedMip);
			s.Field("MipLevels", v.Texture2DArray.MipLevels);
			s.Field("FirstArraySlice", v.Texture2DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DArray.ArraySize);
			s.Field("PlaneSlice", v.Texture2DArray.PlaneSlice);
			s.Field("ResourceMinLODClamp", v.Texture2DArray.ResourceMinLODClamp);
			break;
		case D3D12_SRV_DIMENSION_TEXTURE2DMS: break;
		case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
			s.Field("FirstArraySlice", v.Texture2DMSArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DMSArray.ArraySize);
			break;
		case D3D12_SRV_DIMENSION_TEXTURE3D:
			s.Field("MostDetailedMip", v.Texture3D.MostDetailedMip);
			s.Field("MipLevels", v.Texture3D.MipLevels);
			s.Field("ResourceMinLODClamp", v.Texture3D.ResourceMinLODClamp);
			break;
		case D3D12_SRV_DIMENSION_TEXTURECUBE:
			s.Field("MostDetailedMip", v.TextureCube.MostDetailedMip);
			s.Field("MipLevels", v.TextureCube.MipLevels);
			s.Field("ResourceMinLODClamp", v.TextureCube.ResourceMinLODClamp);
			break;
		case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
			s.Field("MostDetailedMip", v.TextureCubeArray.MostDetailedMip);
			s.Field("MipLevels", v.TextureCubeArray.MipLevels);
			s.Field("First2DArrayFace", v.TextureCubeArray.First2DArrayFace);
			s.Field("NumCubes", v.TextureCubeArray.NumCubes);
			s.Field("ResourceMinLODClamp", v.TextureCubeArray.ResourceMinLODClamp);
			break;
		case D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE:
			s.Address("Location", v.RaytracingAccelerationStructure.Location);
			break;
		default: break;
	}
}

void Serialize(Ser& s, D3D12_UNORDERED_ACCESS_VIEW_DESC& v) {
	s.Field("Format", v.Format);
	s.Field("ViewDimension", v.ViewDimension);
	switch (v.ViewDimension) {
		case D3D12_UAV_DIMENSION_BUFFER:
			s.Field("FirstElement", v.Buffer.FirstElement);
			s.Field("NumElements", v.Buffer.NumElements);
			s.Field("StructureByteStride", v.Buffer.StructureByteStride);
			s.Field("CounterOffsetInBytes", v.Buffer.CounterOffsetInBytes);
			s.Field("Flags", v.Buffer.Flags);
			break;
		case D3D12_UAV_DIMENSION_TEXTURE1D: s.Field("MipSlice", v.Texture1D.MipSlice); break;
		case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
			s.Field("MipSlice", v.Texture1DArray.MipSlice);
			s.Field("FirstArraySlice", v.Texture1DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture1DArray.ArraySize);
			break;
		case D3D12_UAV_DIMENSION_TEXTURE2D:
			s.Field("MipSlice", v.Texture2D.MipSlice);
			s.Field("PlaneSlice", v.Texture2D.PlaneSlice);
			break;
		case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
			s.Field("MipSlice", v.Texture2DArray.MipSlice);
			s.Field("FirstArraySlice", v.Texture2DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DArray.ArraySize);
			s.Field("PlaneSlice", v.Texture2DArray.PlaneSlice);
			break;
		case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
			s.Field("FirstArraySlice", v.Texture2DMSArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DMSArray.ArraySize);
			break;
		case D3D12_UAV_DIMENSION_TEXTURE3D:
			s.Field("MipSlice", v.Texture3D.MipSlice);
			s.Field("FirstWSlice", v.Texture3D.FirstWSlice);
			s.Field("WSize", v.Texture3D.WSize);
			break;
		default: break;
	}
}

void Serialize(Ser& s, D3D12_RENDER_TARGET_VIEW_DESC& v) {
	s.Field("Format", v.Format);
	s.Field("ViewDimension", v.ViewDimension);
	switch (v.ViewDimension) {
		case D3D12_RTV_DIMENSION_BUFFER:
			s.Field("FirstElement", v.Buffer.FirstElement);
			s.Field("NumElements", v.Buffer.NumElements);
			break;
		case D3D12_RTV_DIMENSION_TEXTURE1D: s.Field("MipSlice", v.Texture1D.MipSlice); break;
		case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
			s.Field("MipSlice", v.Texture1DArray.MipSlice);
			s.Field("FirstArraySlice", v.Texture1DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture1DArray.ArraySize);
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2D:
			s.Field("MipSlice", v.Texture2D.MipSlice);
			s.Field("PlaneSlice", v.Texture2D.PlaneSlice);
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
			s.Field("MipSlice", v.Texture2DArray.MipSlice);
			s.Field("FirstArraySlice", v.Texture2DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DArray.ArraySize);
			s.Field("PlaneSlice", v.Texture2DArray.PlaneSlice);
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
			s.Field("FirstArraySlice", v.Texture2DMSArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DMSArray.ArraySize);
			break;
		case D3D12_RTV_DIMENSION_TEXTURE3D:
			s.Field("MipSlice", v.Texture3D.MipSlice);
			s.Field("FirstWSlice", v.Texture3D.FirstWSlice);
			s.Field("WSize", v.Texture3D.WSize);
			break;
		default: break;
	}
}

void Serialize(Ser& s, D3D12_DEPTH_STENCIL_VIEW_DESC& v) {
	s.Field("Format", v.Format);
	s.Field("ViewDimension", v.ViewDimension);
	s.Field("Flags", v.Flags);
	switch (v.ViewDimension) {
		case D3D12_DSV_DIMENSION_TEXTURE1D: s.Field("MipSlice", v.Texture1D.MipSlice); break;
		case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
			s.Field("MipSlice", v.Texture1DArray.MipSlice);
			s.Field("FirstArraySlice", v.Texture1DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture1DArray.ArraySize);
			break;
		case D3D12_DSV_DIMENSION_TEXTURE2D: s.Field("MipSlice", v.Texture2D.MipSlice); break;
		case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
			s.Field("MipSlice", v.Texture2DArray.MipSlice);
			s.Field("FirstArraySlice", v.Texture2DArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DArray.ArraySize);
			break;
		case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
			s.Field("FirstArraySlice", v.Texture2DMSArray.FirstArraySlice);
			s.Field("ArraySize", v.Texture2DMSArray.ArraySize);
			break;
		default: break;
	}
}

void Serialize(Ser& s, D3D12_SAMPLER_DESC2& v) {
	s.Field("Filter", v.Filter);
	s.Field("AddressU", v.AddressU);
	s.Field("AddressV", v.AddressV);
	s.Field("AddressW", v.AddressW);
	s.Field("MipLODBias", v.MipLODBias);
	s.Field("MaxAnisotropy", v.MaxAnisotropy);
	s.Field("ComparisonFunc", v.ComparisonFunc);
	if (v.Flags & D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR) s.Fixed("UintBorderColor", v.UintBorderColor);
	else s.Fixed("BorderColor", v.FloatBorderColor);
	s.Field("MinLOD", v.MinLOD);
	s.Field("MaxLOD", v.MaxLOD);
	s.Field("Flags", v.Flags);
}

void Serialize(Ser& s, D3D12_SUBRESOURCE_FOOTPRINT& v) {
	s.Field("Format", v.Format);
	s.Field("Width", v.Width);
	s.Field("Height", v.Height);
	s.Field("Depth", v.Depth);
	s.Field("RowPitch", v.RowPitch);
}

void Serialize(Ser& s, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& v) {
	s.Field("Offset", v.Offset);
	s.Field("Footprint", v.Footprint);
}

void Serialize(Ser& s, D3D12_TEXTURE_COPY_LOCATION& v) {
	s.Object("pResource", v.pResource);
	s.Field("Type", v.Type);
	if (v.Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT) s.Field("PlacedFootprint", v.PlacedFootprint);
	else s.Field("SubresourceIndex", v.SubresourceIndex);
}

void Serialize(Ser& s, D3D12_BOX& v) {
	s.Field("left", v.left);
	s.Field("top", v.top);
	s.Field("front", v.front);
	s.Field("right", v.right);
	s.Field("bottom", v.bottom);
	s.Field("back", v.back);
}

void Serialize(Ser& s, D3D12_TILED_RESOURCE_COORDINATE& v) {
	s.Field("X", v.X);
	s.Field("Y", v.Y);
	s.Field("Z", v.Z);
	s.Field("Subresource", v.Subresource);
}

void Serialize(Ser& s, D3D12_TILE_REGION_SIZE& v) {
	s.Field("NumTiles", v.NumTiles);
	s.Field("UseBox", v.UseBox);
	s.Field("Width", v.Width);
	s.Field("Height", v.Height);
	s.Field("Depth", v.Depth);
}

void Serialize(Ser& s, D3D12_VIEWPORT& v) {
	s.Field("TopLeftX", v.TopLeftX);
	s.Field("TopLeftY", v.TopLeftY);
	s.Field("Width", v.Width);
	s.Field("Height", v.Height);
	s.Field("MinDepth", v.MinDepth);
	s.Field("MaxDepth", v.MaxDepth);
}

void Serialize(Ser& s, D3D12_RECT& v) {
	s.Field("left", v.left);
	s.Field("top", v.top);
	s.Field("right", v.right);
	s.Field("bottom", v.bottom);
}

void Serialize(Ser& s, D3D12_RESOURCE_BARRIER& v) {
	s.Field("Type", v.Type);
	s.Field("Flags", v.Flags);
	switch (v.Type) {
		case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
			s.Object("pResource", v.Transition.pResource);
			s.Field("Subresource", v.Transition.Subresource);
			s.Field("StateBefore", v.Transition.StateBefore);
			s.Field("StateAfter", v.Transition.StateAfter);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
			s.Object("pResourceBefore", v.Aliasing.pResourceBefore);
			s.Object("pResourceAfter", v.Aliasing.pResourceAfter);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_UAV: s.Object("pResource", v.UAV.pResource); break;
	}
}

void Serialize(Ser& s, D3D12_INDEX_BUFFER_VIEW& v) {
	s.Address("BufferLocation", v.BufferLocation);
	s.Field("SizeInBytes", v.SizeInBytes);
	s.Field("Format", v.Format);
}

void Serialize(Ser& s, D3D12_VERTEX_BUFFER_VIEW& v) {
	s.Address("BufferLocation", v.BufferLocation);
	s.Field("SizeInBytes", v.SizeInBytes);
	s.Field("StrideInBytes", v.StrideInBytes);
}

void Serialize(Ser& s, D3D12_STREAM_OUTPUT_BUFFER_VIEW& v) {
	s.Address("BufferLocation", v.BufferLocation);
	s.Field("SizeInBytes", v.SizeInBytes);
	s.Address("BufferFilledSizeLocation", v.BufferFilledSizeLocation);
}

void Serialize(Ser& s, D3D12_DISCARD_REGION& v) {
	s.Array("Rects", v.pRects, v.NumRects);
	s.Field("FirstSubresource", v.FirstSubresource);
	s.Field("NumSubresources", v.NumSubresources);
}

void Serialize(Ser& s, D3D12_SUBRESOURCE_RANGE_UINT64& v) {
	s.Field("Subresource", v.Subresource);
	s.Field("Begin", v.Range.Begin);
	s.Field("End", v.Range.End);
}

void Serialize(Ser& s, D3D12_SAMPLE_POSITION& v) {
	s.Field("X", v.X);
	s.Field("Y", v.Y);
}

void Serialize(Ser& s, D3D12_WRITEBUFFERIMMEDIATE_PARAMETER& v) {
	s.Address("Dest", v.Dest);
	s.Field("Value", v.Value);
}

void Serialize(Ser& s, D3D12_RENDER_PASS_BEGINNING_ACCESS& v) {
	s.Field("Type", v.Type);
	if (v.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR) s.Field("ClearValue", v.Clear.ClearValue);
	else if (v.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER || v.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV ||
		v.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_UAV) {
		s.Field("AdditionalWidth", v.PreserveLocal.AdditionalWidth);
		s.Field("AdditionalHeight", v.PreserveLocal.AdditionalHeight);
	}
}

void Serialize(Ser& s, D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS& v) {
	s.Field("SrcSubresource", v.SrcSubresource);
	s.Field("DstSubresource", v.DstSubresource);
	s.Field("DstX", v.DstX);
	s.Field("DstY", v.DstY);
	s.Field("SrcRect", v.SrcRect);
}

void Serialize(Ser& s, D3D12_RENDER_PASS_ENDING_ACCESS& v) {
	s.Field("Type", v.Type);
	if (v.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE) {
		s.Object("pSrcResource", v.Resolve.pSrcResource);
		s.Object("pDstResource", v.Resolve.pDstResource);
		s.Array("SubresourceParameters", v.Resolve.pSubresourceParameters, v.Resolve.SubresourceCount);
		s.Field("Format", v.Resolve.Format);
		s.Field("ResolveMode", v.Resolve.ResolveMode);
		s.Field("PreserveResolveSource", v.Resolve.PreserveResolveSource);
	}
	else if (v.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER || v.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_SRV ||
		v.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_UAV) {
		s.Field("AdditionalWidth", v.PreserveLocal.AdditionalWidth);
		s.Field("AdditionalHeight", v.PreserveLocal.AdditionalHeight);
	}
}

void Serialize(Ser& s, D3D12_RENDER_PASS_RENDER_TARGET_DESC& v) {
	s.Field("cpuDescriptor", v.cpuDescriptor);
	s.Field("BeginningAccess", v.BeginningAccess);
	s.Field("EndingAccess", v.EndingAccess);
}

void Serialize(Ser& s, D3D12_RENDER_PASS_DEPTH_STENCIL_DESC& v) {
	s.Field("cpuDescriptor", v.cpuDescriptor);
	s.Field("DepthBeginningAccess", v.DepthBeginningAccess);
	s.Field("StencilBeginningAccess", v.StencilBeginningAccess);
	s.Field("DepthEndingAccess", v.DepthEndingAccess);
	s.Field("StencilEndingAccess", v.StencilEndingAccess);
}

void Serialize(Ser& s, D3D12_GLOBAL_BARRIER& v) {
	s.Field("SyncBefore", v.SyncBefore);
	s.Field("SyncAfter", v.SyncAfter);
	s.Field("AccessBefore", v.AccessBefore);
	s.Field("AccessAfter", v.AccessAfter);
}

void Serialize(Ser& s, D3D12_BARRIER_SUBRESOURCE_RANGE& v) {
	s.Field("IndexOrFirstMipLevel", v.IndexOrFirstMipLevel);
	s.Field("NumMipLevels", v.NumMipLevels);
	s.Field("FirstArraySlice", v.FirstArraySlice);
	s.Field("NumArraySlices", v.NumArraySlices);
	s.Field("FirstPlane", v.FirstPlane);
	s.Field("NumPlanes", v.NumPlanes);
}

void Serialize(Ser& s, D3D12_TEXTURE_BARRIER& v) {
	s.Field("SyncBefore", v.SyncBefore);
	s.Field("SyncAfter", v.SyncAfter);
	s.Field("AccessBefore", v.AccessBefore);
	s.Field("AccessAfter", v.AccessAfter);
	s.Field("LayoutBefore", v.LayoutBefore);
	s.Field("LayoutAfter", v.LayoutAfter);
	s.Object("pResource", v.pResource);
	s.Field("Subresources", v.Subresources);
	s.Field("Flags", v.Flags);
}

void Serialize(Ser& s, D3D12_BUFFER_BARRIER& v) {
	s.Field("SyncBefore", v.SyncBefore);
	s.Field("SyncAfter", v.SyncAfter);
	s.Field("AccessBefore", v.AccessBefore);
	s.Field("AccessAfter", v.AccessAfter);
	s.Object("pResource", v.pResource);
	s.Field("Offset", v.Offset);
	s.Field("Size", v.Size);
}

void Serialize(Ser& s, D3D12_BARRIER_GROUP& v) {
	s.Field("Type", v.Type);
	switch (v.Type) {
		case D3D12_BARRIER_TYPE_GLOBAL: s.Array("GlobalBarriers", v.pGlobalBarriers, v.NumBarriers); break;
		case D3D12_BARRIER_TYPE_TEXTURE: s.Array("TextureBarriers", v.pTextureBarriers, v.NumBarriers); break;
		case D3D12_BARRIER_TYPE_BUFFER: s.Array("BufferBarriers", v.pBufferBarriers, v.NumBarriers); break;
	}
}

void SerField(Ser& s, const char* name, D3D12_CPU_DESCRIPTOR_HANDLE& v) {
	u64 ptr = v.ptr;
	s.CpuDescriptor(name, ptr);
	v.ptr = (SIZE_T)ptr;
}

void SerField(Ser& s, const char* name, D3D12_GPU_DESCRIPTOR_HANDLE& v) { s.GpuDescriptor(name, v.ptr); }

// --- Records ---------------------------------------------------------------------------------------------------------

namespace fb::d3d12
{
	const char* ObjectKindName(ObjectKind kind) {
		static const char* names[] = {"Unknown", "Device", "Heap", "Resource", "DescriptorHeap", "RootSignature", "PipelineState",
			"CommandSignature", "QueryHeap", "Fence", "CommandQueue", "CommandAllocator", "CommandList", "SwapChain"};
		return (u32)kind < sizeof(names) / sizeof(names[0]) ? names[(u32)kind] : "Unknown";
	}

	const char* ViewKindName(ViewKind kind) {
		static const char* names[] = {"None", "CBV", "SRV", "UAV", "RTV", "DSV", "Sampler", "Sampler"};
		return (u32)kind < 8 ? names[(u32)kind] : "None";
	}

	void Serialize(Ser& s, DeviceInfo& v) {
		s.Value("id", v.id);
		s.Text("adapter", v.adapter);
		s.Value("vendorId", v.vendorId);
		s.Value("deviceId", v.deviceId);
		s.Value("luid", v.luid);
		s.Value("dedicatedVideoMemory", v.dedicatedVideoMemory);
		s.Text("driver", v.driver);
		s.Value("featureLevel", v.featureLevel);
		s.Value("rtvIncrement", v.rtvIncrement);
		s.Value("dsvIncrement", v.dsvIncrement);
		s.Value("viewIncrement", v.viewIncrement);
		s.Value("samplerIncrement", v.samplerIncrement);
		s.Value("debugLayer", v.debugLayer);
	}

	void Serialize(Ser& s, HeapCreate& v) {
		s.Value("id", v.id);
		s.Field("desc", v.desc);
	}

	void Serialize(Ser& s, ResourceCreate& v) {
		s.Value("id", v.id);
		u32 kind = (u32)v.kind;
		s.Value("kind", kind);
		v.kind = (ResourceKind)kind;
		s.Field("heapProperties", v.heapProperties);
		s.Field("heapFlags", v.heapFlags);
		s.Field("desc", v.desc);
		s.Field("initialState", v.initialState);
		s.Value("useLayout", v.useLayout);
		s.Field("initialLayout", v.initialLayout);
		s.Value("hasClearValue", v.hasClearValue);
		if (v.hasClearValue) s.Field("clearValue", v.clearValue);
		s.Value("heap", v.heap);
		s.Value("heapOffset", v.heapOffset);
		s.Value("gpuAddress", v.gpuAddress);
		s.Array("castableFormats", v.castableFormats, v.castableFormatCount);
	}

	void Serialize(Ser& s, DescriptorHeapCreate& v) {
		s.Value("id", v.id);
		s.Field("desc", v.desc);
		s.Value("cpuStart", v.cpuStart);
		s.Value("gpuStart", v.gpuStart);
		s.Value("increment", v.increment);
	}

	void Serialize(Ser& s, RootSignatureCreate& v) {
		s.Value("id", v.id);
		s.Value("nodeMask", v.nodeMask);
		s.Blob("blob", v.blob, v.blobSize);
	}

	void Serialize(Ser& s, PipelineCreate& v) {
		s.Value("id", v.id);
		u32 kind = (u32)v.kind;
		s.Value("kind", kind);
		v.kind = (PipelineKind)kind;
		s.Value("fromStream", v.fromStream);
		s.Object("rootSignature", v.rootSignature);
		s.Field("VS", v.vs);
		s.Field("PS", v.ps);
		s.Field("DS", v.ds);
		s.Field("HS", v.hs);
		s.Field("GS", v.gs);
		s.Field("CS", v.cs);
		s.Field("AS", v.as);
		s.Field("MS", v.ms);
		s.Field("StreamOutput", v.streamOutput);
		s.Field("BlendState", v.blend);
		s.Value("SampleMask", v.sampleMask);
		s.Value("rasterizerVersion", v.rasterizerVersion);
		s.Field("RasterizerState", v.rasterizer);
		s.Value("depthStencilVersion", v.depthStencilVersion);
		s.Field("DepthStencilState", v.depthStencil);
		s.Field("InputLayout", v.inputLayout);
		s.Field("IBStripCutValue", v.stripCut);
		s.Field("PrimitiveTopologyType", v.topologyType);
		s.Field("RenderTargets", v.renderTargets);
		s.Field("DSVFormat", v.depthStencilFormat);
		s.Field("SampleDesc", v.sampleDesc);
		s.Value("NodeMask", v.nodeMask);
		s.Field("Flags", v.flags);
		s.Field("ViewInstancing", v.viewInstancing);
	}

	void Serialize(Ser& s, CommandSignatureCreate& v) {
		s.Value("id", v.id);
		s.Field("desc", v.desc);
		s.Object("rootSignature", v.rootSignature);
	}

	void Serialize(Ser& s, QueryHeapCreate& v) {
		s.Value("id", v.id);
		s.Field("desc", v.desc);
	}

	void Serialize(Ser& s, FenceCreate& v) {
		s.Value("id", v.id);
		s.Value("value", v.value);
		s.Field("flags", v.flags);
	}

	void Serialize(Ser& s, QueueCreate& v) {
		s.Value("id", v.id);
		s.Field("desc", v.desc);
	}

	void Serialize(Ser& s, ListCreate& v) {
		s.Value("id", v.id);
		s.Field("type", v.type);
	}

	void Serialize(Ser& s, SwapChainCreate& v) {
		s.Value("id", v.id);
		s.Value("queue", v.queue);
		s.Value("width", v.width);
		s.Value("height", v.height);
		s.Field("format", v.format);
		s.Array("buffers", v.buffers, v.bufferCount);
	}

	void Serialize(Ser& s, ObjectName& v) {
		s.Value("id", v.id);
		s.Text("name", v.name);
	}

	void Serialize(Ser& s, ObjectRef& v) { s.Value("id", v.id); }

	void Serialize(Ser& s, DescriptorWrite& v) {
		s.Value("heap", v.heap);
		s.Value("index", v.index);
		u32 kind = (u32)v.kind;
		s.Value("kind", kind);
		v.kind = (ViewKind)kind;
		s.Object("resource", v.resource);
		s.Object("counter", v.counter);
		s.Value("hasDesc", v.hasDesc);
		if (!v.hasDesc) return;
		switch (v.kind) {
			case ViewKind::Cbv: s.Field("desc", v.cbv); break;
			case ViewKind::Srv: s.Field("desc", v.srv); break;
			case ViewKind::Uav: s.Field("desc", v.uav); break;
			case ViewKind::Rtv: s.Field("desc", v.rtv); break;
			case ViewKind::Dsv: s.Field("desc", v.dsv); break;
			case ViewKind::Sampler:
			case ViewKind::Sampler2: s.Field("desc", v.sampler); break;
			default: break;
		}
	}

	void Serialize(Ser& s, DescriptorCopy& v) {
		s.Value("dstHeap", v.dstHeap);
		s.Value("dstIndex", v.dstIndex);
		s.Value("srcHeap", v.srcHeap);
		s.Value("srcIndex", v.srcIndex);
		s.Value("count", v.count);
	}

	void Serialize(Ser& s, ResourceState& v) {
		s.Value("id", v.id);
		s.Array("states", v.states, v.count);
		s.Array("layouts", v.layouts, v.layoutCount);
	}

	void Serialize(Ser& s, InitialContents& v) {
		s.Value("id", v.id);
		s.Value("subresource", v.subresource);
		s.Field("footprint", v.footprint);
		s.Blob("data", v.data, v.size);
	}

	void Serialize(Ser& s, ListContents& v) {
		s.Value("list", v.list);
		s.Value("commandCount", v.commandCount);
		s.Blob("commands", v.commands, v.size);
	}

	void Serialize(Ser& s, Submit& v) {
		s.Value("queue", v.queue);
		s.Array("lists", v.lists, v.listCount);
	}

	void Serialize(Ser& s, MapWrite& v) {
		s.Value("resource", v.resource);
		s.Value("offset", v.offset);
		s.Blob("data", v.data, v.size);
	}

	void Serialize(Ser& s, QueueFence& v) {
		s.Value("queue", v.queue);
		s.Value("fence", v.fence);
		s.Value("value", v.value);
	}

	void Serialize(Ser& s, FenceSignal& v) {
		s.Value("fence", v.fence);
		s.Value("value", v.value);
	}

	void Serialize(Ser& s, QueueMarker& v) {
		s.Value("queue", v.queue);
		u32 op = (u32)v.op;
		s.Value("op", op);
		v.op = (MarkerOp)op;
		s.Value("metadata", v.metadata);
		s.Blob("data", v.data, v.size);
	}

	void Serialize(Ser& s, Present& v) {
		s.Value("swapChain", v.swapChain);
		s.Value("backBuffer", v.backBuffer);
		s.Value("syncInterval", v.syncInterval);
		s.Value("flags", v.flags);
	}

	// --- Commands -------------------------------------------------------------------------------------------------

	const char* CmdName(Cmd command) {
		static const char* names[] = {
#define FB_NAME_ITEM(name) #name,
			FB_D3D12_COMMANDS(FB_NAME_ITEM)
#undef FB_NAME_ITEM
		};
		return (u32)command < (u32)Cmd::Count ? names[(u32)command] : "Unknown";
	}

	CmdClass ClassOf(Cmd command) {
		switch (command) {
			case Cmd::DrawInstanced:
			case Cmd::DrawIndexedInstanced:
			case Cmd::ExecuteIndirect:
			case Cmd::DispatchMesh:
				return CmdClass::Draw;
			case Cmd::Dispatch: return CmdClass::Dispatch;
			case Cmd::CopyBufferRegion:
			case Cmd::CopyTextureRegion:
			case Cmd::CopyResource:
			case Cmd::CopyTiles:
			case Cmd::AtomicCopyBufferUINT:
			case Cmd::AtomicCopyBufferUINT64:
			case Cmd::WriteBufferImmediate:
				return CmdClass::Copy;
			case Cmd::ClearDepthStencilView:
			case Cmd::ClearRenderTargetView:
			case Cmd::ClearUnorderedAccessViewUint:
			case Cmd::ClearUnorderedAccessViewFloat:
			case Cmd::DiscardResource:
				return CmdClass::Clear;
			case Cmd::ResolveSubresource:
			case Cmd::ResolveSubresourceRegion:
			case Cmd::ResolveQueryData:
				return CmdClass::Resolve;
			case Cmd::ResourceBarrier:
			case Cmd::Barrier:
				return CmdClass::Barrier;
			case Cmd::SetMarker:
			case Cmd::BeginEvent:
			case Cmd::EndEvent:
				return CmdClass::Marker;
			case Cmd::BeginQuery:
			case Cmd::EndQuery:
				return CmdClass::Query;
			case Cmd::Unsupported: return CmdClass::Other;
			default: return CmdClass::State;
		}
	}

	bool IsAction(Cmd command) {
		const CmdClass c = ClassOf(command);
		return c == CmdClass::Draw || c == CmdClass::Dispatch || c == CmdClass::Copy || c == CmdClass::Clear || c == CmdClass::Resolve;
	}

	template <class T> static void Ser1(Ser& s, void* args, void (*body)(Ser&, T&)) { body(s, *(T*)args); }

	size_t CommandArgsSize(Cmd command) {
		switch (command) {
			case Cmd::Close:
			case Cmd::EndEvent:
			case Cmd::EndRenderPass:
				return 1;
			case Cmd::Reset: return sizeof(ResetArgs);
			case Cmd::ClearState:
			case Cmd::SetPipelineState:
				return sizeof(PipelineArgs);
			case Cmd::DrawInstanced: return sizeof(DrawArgs);
			case Cmd::DrawIndexedInstanced: return sizeof(DrawIndexedArgs);
			case Cmd::Dispatch: return sizeof(DispatchArgs);
			case Cmd::CopyBufferRegion: return sizeof(CopyBufferRegionArgs);
			case Cmd::CopyTextureRegion: return sizeof(CopyTextureRegionArgs);
			case Cmd::CopyResource: return sizeof(CopyResourceArgs);
			case Cmd::CopyTiles: return sizeof(CopyTilesArgs);
			case Cmd::ResolveSubresource: return sizeof(ResolveSubresourceArgs);
			case Cmd::IASetPrimitiveTopology: return sizeof(TopologyArgs);
			case Cmd::RSSetViewports: return sizeof(ViewportsArgs);
			case Cmd::RSSetScissorRects: return sizeof(ScissorsArgs);
			case Cmd::OMSetBlendFactor: return sizeof(BlendFactorArgs);
			case Cmd::OMSetStencilRef: return sizeof(StencilRefArgs);
			case Cmd::ResourceBarrier: return sizeof(BarrierArgs);
			case Cmd::ExecuteBundle: return sizeof(BundleArgs);
			case Cmd::SetDescriptorHeaps: return sizeof(DescriptorHeapsArgs);
			case Cmd::SetComputeRootSignature:
			case Cmd::SetGraphicsRootSignature:
				return sizeof(RootSignatureArgs);
			case Cmd::SetComputeRootDescriptorTable:
			case Cmd::SetGraphicsRootDescriptorTable:
				return sizeof(RootTableArgs);
			case Cmd::SetComputeRoot32BitConstant:
			case Cmd::SetGraphicsRoot32BitConstant:
				return sizeof(RootConstantArgs);
			case Cmd::SetComputeRoot32BitConstants:
			case Cmd::SetGraphicsRoot32BitConstants:
				return sizeof(RootConstantsArgs);
			case Cmd::SetComputeRootConstantBufferView:
			case Cmd::SetGraphicsRootConstantBufferView:
			case Cmd::SetComputeRootShaderResourceView:
			case Cmd::SetGraphicsRootShaderResourceView:
			case Cmd::SetComputeRootUnorderedAccessView:
			case Cmd::SetGraphicsRootUnorderedAccessView:
				return sizeof(RootViewArgs);
			case Cmd::IASetIndexBuffer: return sizeof(IndexBufferArgs);
			case Cmd::IASetVertexBuffers: return sizeof(VertexBuffersArgs);
			case Cmd::SOSetTargets: return sizeof(SOTargetsArgs);
			case Cmd::OMSetRenderTargets: return sizeof(RenderTargetsArgs);
			case Cmd::ClearDepthStencilView: return sizeof(ClearDepthStencilArgs);
			case Cmd::ClearRenderTargetView: return sizeof(ClearRenderTargetArgs);
			case Cmd::ClearUnorderedAccessViewUint: return sizeof(ClearUavUintArgs);
			case Cmd::ClearUnorderedAccessViewFloat: return sizeof(ClearUavFloatArgs);
			case Cmd::DiscardResource: return sizeof(DiscardArgs);
			case Cmd::BeginQuery:
			case Cmd::EndQuery:
				return sizeof(QueryArgs);
			case Cmd::ResolveQueryData: return sizeof(ResolveQueryArgs);
			case Cmd::SetPredication: return sizeof(PredicationArgs);
			case Cmd::SetMarker:
			case Cmd::BeginEvent:
				return sizeof(MarkerArgs);
			case Cmd::ExecuteIndirect: return sizeof(ExecuteIndirectArgs);
			case Cmd::AtomicCopyBufferUINT:
			case Cmd::AtomicCopyBufferUINT64:
				return sizeof(AtomicCopyArgs);
			case Cmd::OMSetDepthBounds: return sizeof(DepthBoundsArgs);
			case Cmd::SetSamplePositions: return sizeof(SamplePositionsArgs);
			case Cmd::ResolveSubresourceRegion: return sizeof(ResolveRegionArgs);
			case Cmd::SetViewInstanceMask: return sizeof(ViewInstanceMaskArgs);
			case Cmd::WriteBufferImmediate: return sizeof(WriteImmediateArgs);
			case Cmd::BeginRenderPass: return sizeof(BeginRenderPassArgs);
			case Cmd::RSSetShadingRate: return sizeof(ShadingRateArgs);
			case Cmd::RSSetShadingRateImage: return sizeof(ShadingRateImageArgs);
			case Cmd::DispatchMesh: return sizeof(DispatchMeshArgs);
			case Cmd::Barrier: return sizeof(EnhancedBarrierArgs);
			case Cmd::OMSetFrontAndBackStencilRef: return sizeof(StencilRefsArgs);
			case Cmd::RSSetDepthBias: return sizeof(DepthBiasArgs);
			case Cmd::IASetIndexBufferStripCutValue: return sizeof(StripCutArgs);
			case Cmd::Unsupported: return sizeof(UnsupportedArgs);
			default: return 1;
		}
	}

	static void Rects(Ser& s, const char* name, const D3D12_RECT*& rects, UINT& count) { s.Array(name, rects, count); }

	void SerializeCommand(Ser& s, Cmd command, void* raw) {
		switch (command) {
			case Cmd::Close:
			case Cmd::EndEvent:
			case Cmd::EndRenderPass:
				return;
			case Cmd::Reset: {
				auto& a = *(ResetArgs*)raw;
				s.Object("pAllocator", a.allocator);
				s.Object("pInitialState", a.pipelineState);
				return;
			}
			case Cmd::ClearState:
			case Cmd::SetPipelineState: {
				auto& a = *(PipelineArgs*)raw;
				s.Object("pPipelineState", a.pipelineState);
				return;
			}
			case Cmd::DrawInstanced: {
				auto& a = *(DrawArgs*)raw;
				s.Field("VertexCountPerInstance", a.vertexCount);
				s.Field("InstanceCount", a.instanceCount);
				s.Field("StartVertexLocation", a.startVertex);
				s.Field("StartInstanceLocation", a.startInstance);
				return;
			}
			case Cmd::DrawIndexedInstanced: {
				auto& a = *(DrawIndexedArgs*)raw;
				s.Field("IndexCountPerInstance", a.indexCount);
				s.Field("InstanceCount", a.instanceCount);
				s.Field("StartIndexLocation", a.startIndex);
				s.Field("BaseVertexLocation", a.baseVertex);
				s.Field("StartInstanceLocation", a.startInstance);
				return;
			}
			case Cmd::Dispatch:
			case Cmd::DispatchMesh: {
				auto& a = *(DispatchArgs*)raw;
				s.Field("ThreadGroupCountX", a.x);
				s.Field("ThreadGroupCountY", a.y);
				s.Field("ThreadGroupCountZ", a.z);
				return;
			}
			case Cmd::CopyBufferRegion: {
				auto& a = *(CopyBufferRegionArgs*)raw;
				s.Object("pDstBuffer", a.dst);
				s.Field("DstOffset", a.dstOffset);
				s.Object("pSrcBuffer", a.src);
				s.Field("SrcOffset", a.srcOffset);
				s.Field("NumBytes", a.numBytes);
				return;
			}
			case Cmd::CopyTextureRegion: {
				auto& a = *(CopyTextureRegionArgs*)raw;
				s.Field("Dst", a.dst);
				s.Field("DstX", a.dstX);
				s.Field("DstY", a.dstY);
				s.Field("DstZ", a.dstZ);
				s.Field("Src", a.src);
				s.Optional("SrcBox", a.srcBox);
				return;
			}
			case Cmd::CopyResource: {
				auto& a = *(CopyResourceArgs*)raw;
				s.Object("pDstResource", a.dst);
				s.Object("pSrcResource", a.src);
				return;
			}
			case Cmd::CopyTiles: {
				auto& a = *(CopyTilesArgs*)raw;
				s.Object("pTiledResource", a.tiled);
				s.Field("TileRegionStartCoordinate", a.start);
				s.Field("TileRegionSize", a.size);
				s.Object("pBuffer", a.buffer);
				s.Field("BufferStartOffsetInBytes", a.bufferOffset);
				s.Field("Flags", a.flags);
				return;
			}
			case Cmd::ResolveSubresource: {
				auto& a = *(ResolveSubresourceArgs*)raw;
				s.Object("pDstResource", a.dst);
				s.Field("DstSubresource", a.dstSubresource);
				s.Object("pSrcResource", a.src);
				s.Field("SrcSubresource", a.srcSubresource);
				s.Field("Format", a.format);
				return;
			}
			case Cmd::IASetPrimitiveTopology: s.Field("PrimitiveTopology", ((TopologyArgs*)raw)->topology); return;
			case Cmd::RSSetViewports: {
				auto& a = *(ViewportsArgs*)raw;
				s.Array("Viewports", a.viewports, a.count);
				return;
			}
			case Cmd::RSSetScissorRects: {
				auto& a = *(ScissorsArgs*)raw;
				Rects(s, "Rects", a.rects, a.count);
				return;
			}
			case Cmd::OMSetBlendFactor: s.Fixed("BlendFactor", ((BlendFactorArgs*)raw)->factor); return;
			case Cmd::OMSetStencilRef: s.Field("StencilRef", ((StencilRefArgs*)raw)->ref); return;
			case Cmd::ResourceBarrier: {
				auto& a = *(BarrierArgs*)raw;
				s.Array("Barriers", a.barriers, a.count);
				return;
			}
			case Cmd::ExecuteBundle: s.Object("pCommandList", ((BundleArgs*)raw)->bundle); return;
			case Cmd::SetDescriptorHeaps: {
				auto& a = *(DescriptorHeapsArgs*)raw;
				// Heaps are object references: their ids. A list binds at most one view and one sampler heap.
				if (s.Writing() && a.count > 2) a.count = 2;
				s.Value("NumDescriptorHeaps", a.count);
				if (a.count > 2) a.count = 2;
				ID3D12DescriptorHeap** heaps = s.Writing() ? const_cast<ID3D12DescriptorHeap**>(a.heaps) : s.Memory()->Array<ID3D12DescriptorHeap*>(a.count);
				if (s.Json()) s.Json()->BeginArray("ppDescriptorHeaps");
				for (u32 i = 0; i < a.count && !s.Failed(); ++i) s.Object(nullptr, heaps[i]);
				if (s.Json()) s.Json()->EndArray();
				if (s.Reading()) a.heaps = heaps;
				return;
			}
			case Cmd::SetComputeRootSignature:
			case Cmd::SetGraphicsRootSignature:
				s.Object("pRootSignature", ((RootSignatureArgs*)raw)->rootSignature);
				return;
			case Cmd::SetComputeRootDescriptorTable:
			case Cmd::SetGraphicsRootDescriptorTable: {
				auto& a = *(RootTableArgs*)raw;
				s.Field("RootParameterIndex", a.index);
				s.Field("BaseDescriptor", a.base);
				return;
			}
			case Cmd::SetComputeRoot32BitConstant:
			case Cmd::SetGraphicsRoot32BitConstant: {
				auto& a = *(RootConstantArgs*)raw;
				s.Field("RootParameterIndex", a.index);
				s.Field("SrcData", a.value);
				s.Field("DestOffsetIn32BitValues", a.offset);
				return;
			}
			case Cmd::SetComputeRoot32BitConstants:
			case Cmd::SetGraphicsRoot32BitConstants: {
				auto& a = *(RootConstantsArgs*)raw;
				s.Field("RootParameterIndex", a.index);
				s.Array("SrcData", a.values, a.count);
				s.Field("DestOffsetIn32BitValues", a.offset);
				return;
			}
			case Cmd::SetComputeRootConstantBufferView:
			case Cmd::SetGraphicsRootConstantBufferView:
			case Cmd::SetComputeRootShaderResourceView:
			case Cmd::SetGraphicsRootShaderResourceView:
			case Cmd::SetComputeRootUnorderedAccessView:
			case Cmd::SetGraphicsRootUnorderedAccessView: {
				auto& a = *(RootViewArgs*)raw;
				s.Field("RootParameterIndex", a.index);
				s.Address("BufferLocation", a.address);
				return;
			}
			case Cmd::IASetIndexBuffer: s.Optional("pView", ((IndexBufferArgs*)raw)->view); return;
			case Cmd::IASetVertexBuffers: {
				auto& a = *(VertexBuffersArgs*)raw;
				s.Field("StartSlot", a.startSlot);
				s.Array("Views", a.views, a.count);
				return;
			}
			case Cmd::SOSetTargets: {
				auto& a = *(SOTargetsArgs*)raw;
				s.Field("StartSlot", a.startSlot);
				s.Array("Views", a.views, a.count);
				return;
			}
			case Cmd::OMSetRenderTargets: {
				auto& a = *(RenderTargetsArgs*)raw;
				s.Array("RenderTargetDescriptors", a.targets, a.count);
				s.Optional("DepthStencilDescriptor", a.depthStencil);
				return;
			}
			case Cmd::ClearDepthStencilView: {
				auto& a = *(ClearDepthStencilArgs*)raw;
				s.Field("DepthStencilView", a.view);
				s.Field("ClearFlags", a.flags);
				s.Field("Depth", a.depth);
				s.Field("Stencil", a.stencil);
				Rects(s, "Rects", a.rects, a.rectCount);
				return;
			}
			case Cmd::ClearRenderTargetView: {
				auto& a = *(ClearRenderTargetArgs*)raw;
				s.Field("RenderTargetView", a.view);
				s.Fixed("ColorRGBA", a.color);
				Rects(s, "Rects", a.rects, a.rectCount);
				return;
			}
			case Cmd::ClearUnorderedAccessViewUint: {
				auto& a = *(ClearUavUintArgs*)raw;
				s.Field("ViewGPUHandleInCurrentHeap", a.gpuView);
				s.Field("ViewCPUHandle", a.cpuView);
				s.Object("pResource", a.resource);
				s.Fixed("Values", a.values);
				Rects(s, "Rects", a.rects, a.rectCount);
				return;
			}
			case Cmd::ClearUnorderedAccessViewFloat: {
				auto& a = *(ClearUavFloatArgs*)raw;
				s.Field("ViewGPUHandleInCurrentHeap", a.gpuView);
				s.Field("ViewCPUHandle", a.cpuView);
				s.Object("pResource", a.resource);
				s.Fixed("Values", a.values);
				Rects(s, "Rects", a.rects, a.rectCount);
				return;
			}
			case Cmd::DiscardResource: {
				auto& a = *(DiscardArgs*)raw;
				s.Object("pResource", a.resource);
				s.Optional("pRegion", a.region);
				return;
			}
			case Cmd::BeginQuery:
			case Cmd::EndQuery: {
				auto& a = *(QueryArgs*)raw;
				s.Object("pQueryHeap", a.heap);
				s.Field("Type", a.type);
				s.Field("Index", a.index);
				return;
			}
			case Cmd::ResolveQueryData: {
				auto& a = *(ResolveQueryArgs*)raw;
				s.Object("pQueryHeap", a.heap);
				s.Field("Type", a.type);
				s.Field("StartIndex", a.start);
				s.Field("NumQueries", a.count);
				s.Object("pDestinationBuffer", a.destination);
				s.Field("AlignedDestinationBufferOffset", a.offset);
				return;
			}
			case Cmd::SetPredication: {
				auto& a = *(PredicationArgs*)raw;
				s.Object("pBuffer", a.buffer);
				s.Field("AlignedBufferOffset", a.offset);
				s.Field("Operation", a.op);
				return;
			}
			case Cmd::SetMarker:
			case Cmd::BeginEvent: {
				auto& a = *(MarkerArgs*)raw;
				s.Value("Metadata", a.metadata);
				s.Blob("pData", a.data, a.size);
				if (s.Json() && s.Reading()) s.Json()->String("Text", MarkerText(a.metadata, a.data, a.size));
				return;
			}
			case Cmd::ExecuteIndirect: {
				auto& a = *(ExecuteIndirectArgs*)raw;
				s.Object("pCommandSignature", a.signature);
				s.Field("MaxCommandCount", a.maxCount);
				s.Object("pArgumentBuffer", a.arguments);
				s.Field("ArgumentBufferOffset", a.argumentsOffset);
				s.Object("pCountBuffer", a.countBuffer);
				s.Field("CountBufferOffset", a.countOffset);
				return;
			}
			case Cmd::AtomicCopyBufferUINT:
			case Cmd::AtomicCopyBufferUINT64: {
				auto& a = *(AtomicCopyArgs*)raw;
				s.Object("pDstBuffer", a.dst);
				s.Field("DstOffset", a.dstOffset);
				s.Object("pSrcBuffer", a.src);
				s.Field("SrcOffset", a.srcOffset);
				s.Value("Dependencies", a.dependencies);
				ID3D12Resource** deps = s.Writing() ? const_cast<ID3D12Resource**>(a.dependentResources) : s.Memory()->Array<ID3D12Resource*>(a.dependencies);
				if (s.Json()) s.Json()->BeginArray("ppDependentResources");
				for (u32 i = 0; i < a.dependencies && !s.Failed(); ++i) s.Object(nullptr, deps[i]);
				if (s.Json()) s.Json()->EndArray();
				if (s.Reading()) a.dependentResources = deps;
				s.Items("pDependentSubresourceRanges", a.dependentRanges, a.dependencies);
				return;
			}
			case Cmd::OMSetDepthBounds: {
				auto& a = *(DepthBoundsArgs*)raw;
				s.Field("Min", a.min);
				s.Field("Max", a.max);
				return;
			}
			case Cmd::SetSamplePositions: {
				auto& a = *(SamplePositionsArgs*)raw;
				s.Field("NumSamplesPerPixel", a.samplesPerPixel);
				s.Field("NumPixels", a.pixels);
				u32 count = a.samplesPerPixel * a.pixels;
				s.Items("pSamplePositions", a.positions, count);
				return;
			}
			case Cmd::ResolveSubresourceRegion: {
				auto& a = *(ResolveRegionArgs*)raw;
				s.Object("pDstResource", a.dst);
				s.Field("DstSubresource", a.dstSubresource);
				s.Field("DstX", a.dstX);
				s.Field("DstY", a.dstY);
				s.Object("pSrcResource", a.src);
				s.Field("SrcSubresource", a.srcSubresource);
				s.Optional("pSrcRect", a.srcRect);
				s.Field("Format", a.format);
				s.Field("ResolveMode", a.mode);
				return;
			}
			case Cmd::SetViewInstanceMask: s.Field("Mask", ((ViewInstanceMaskArgs*)raw)->mask); return;
			case Cmd::WriteBufferImmediate: {
				auto& a = *(WriteImmediateArgs*)raw;
				s.Array("Params", a.params, a.count);
				u8 hasModes = a.modes ? 1 : 0;
				s.Value("hasModes", hasModes);
				if (hasModes) s.Items("Modes", a.modes, a.count);
				else if (s.Reading()) a.modes = nullptr;
				return;
			}
			case Cmd::BeginRenderPass: {
				auto& a = *(BeginRenderPassArgs*)raw;
				s.Array("RenderTargets", a.targets, a.count);
				s.Optional("DepthStencil", a.depthStencil);
				s.Field("Flags", a.flags);
				return;
			}
			case Cmd::RSSetShadingRate: {
				auto& a = *(ShadingRateArgs*)raw;
				s.Field("BaseShadingRate", a.rate);
				s.Value("hasCombiners", a.hasCombiners);
				if (a.hasCombiners) s.Fixed("Combiners", a.combiners);
				return;
			}
			case Cmd::RSSetShadingRateImage: s.Object("shadingRateImage", ((ShadingRateImageArgs*)raw)->image); return;
			case Cmd::Barrier: {
				auto& a = *(EnhancedBarrierArgs*)raw;
				s.Array("BarrierGroups", a.groups, a.count);
				return;
			}
			case Cmd::OMSetFrontAndBackStencilRef: {
				auto& a = *(StencilRefsArgs*)raw;
				s.Field("FrontStencilRef", a.front);
				s.Field("BackStencilRef", a.back);
				return;
			}
			case Cmd::RSSetDepthBias: {
				auto& a = *(DepthBiasArgs*)raw;
				s.Field("DepthBias", a.bias);
				s.Field("DepthBiasClamp", a.clamp);
				s.Field("SlopeScaledDepthBias", a.slopeScaled);
				return;
			}
			case Cmd::IASetIndexBufferStripCutValue: s.Field("IBStripCutValue", ((StripCutArgs*)raw)->value); return;
			case Cmd::Unsupported: s.Text("Method", ((UnsupportedArgs*)raw)->name); return;
			default: return;
		}
	}

	static std::string Utf16Text(const void* data, u32 size) {
		std::wstring wide;
		const wchar_t* chars = (const wchar_t*)data;
		for (u32 i = 0; i < size / 2 && chars[i]; ++i) wide += chars[i];
		return Narrow(wide);
	}

	std::string MarkerText(u32 metadata, const void* data, u32 size) {
		if (!data || !size) return std::string();
		if (metadata == 0) return Utf16Text(data, size);
		if (metadata == 1) {
			const char* chars = (const char*)data;
			size_t length = 0;
			while (length < size && chars[length]) ++length;
			return std::string(chars, length);
		}
		if (metadata == 2 && size >= 24) {
			// PIX3 blob: u64 header (event type), u64 color, then the format string as a string header (u64: flags
			// with bit 63 ANSI... ) followed by its characters packed in u64s. Only the format string is shown.
			const u64* words = (const u64*)data;
			const size_t count = size / 8;
			const u64 stringHeader = words[2];
			const bool ansi = (stringHeader >> 54) & 1;
			std::string text;
			for (size_t w = 3; w < count; ++w) {
				const u64 word = words[w];
				if (ansi) {
					for (int b = 0; b < 8; ++b) {
						const char c = (char)(word >> (8 * b));
						if (!c) return text;
						text += c;
					}
				}
				else {
					std::wstring wide;
					for (int b = 0; b < 4; ++b) {
						const wchar_t c = (wchar_t)(word >> (16 * b));
						if (!c) return text + Narrow(wide);
						wide += c;
					}
					text += Narrow(wide);
				}
			}
			return text;
		}
		// Unknown metadata: printable bytes, or the size.
		std::string text;
		const u8* bytes = (const u8*)data;
		for (u32 i = 0; i < size && bytes[i]; ++i)
			if (bytes[i] >= 32 && bytes[i] < 127) text += (char)bytes[i];
		return text.empty() ? Format("marker (%u bytes)", size) : text;
	}
}
