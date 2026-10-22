// A recorded command issued again on a replay list: its arguments decoded with the replay device as the map (objects,
// addresses and descriptor handles are the replay's), then the same method called.
#include "device.h"

namespace fb::replay::d3d12
{
	template <class I> static I* As(ID3D12GraphicsCommandList* list) {
		I* versioned = nullptr;
		if (FAILED(list->QueryInterface(__uuidof(I), (void**)&versioned))) return nullptr;
		versioned->Release();   // the list keeps it alive
		return versioned;
	}

	void ReplayDevice::RunCommand(ID3D12GraphicsCommandList* list, const CommandRef& command, std::vector<u8>& args) {
		args.assign(CommandArgsSize(command.command) + 16, 0);
		scratch_.Reset();
		if (!model_.DecodeCommandIn(scratch_, command, args.data(), this)) {
			warnings.push_back(Format("a damaged %s command was skipped", CmdName(command.command)));
			return;
		}
		void* raw = args.data();
		switch (command.command) {
			case Cmd::Close:
			case Cmd::Reset: return;
			case Cmd::ClearState: list->ClearState(((PipelineArgs*)raw)->pipelineState); return;
			case Cmd::SetPipelineState: {
				ID3D12PipelineState* state = ((PipelineArgs*)raw)->pipelineState;
				if (state) list->SetPipelineState(state);
				return;
			}
			case Cmd::DrawInstanced: {
				auto& a = *(DrawArgs*)raw;
				list->DrawInstanced(a.vertexCount, a.instanceCount, a.startVertex, a.startInstance);
				return;
			}
			case Cmd::DrawIndexedInstanced: {
				auto& a = *(DrawIndexedArgs*)raw;
				list->DrawIndexedInstanced(a.indexCount, a.instanceCount, a.startIndex, a.baseVertex, a.startInstance);
				return;
			}
			case Cmd::Dispatch: {
				auto& a = *(DispatchArgs*)raw;
				list->Dispatch(a.x, a.y, a.z);
				return;
			}
			case Cmd::DispatchMesh: {
				auto& a = *(DispatchArgs*)raw;
				if (auto* list6 = As<ID3D12GraphicsCommandList6>(list)) list6->DispatchMesh(a.x, a.y, a.z);
				return;
			}
			case Cmd::CopyBufferRegion: {
				auto& a = *(CopyBufferRegionArgs*)raw;
				if (a.dst && a.src) list->CopyBufferRegion(a.dst, a.dstOffset, a.src, a.srcOffset, a.numBytes);
				return;
			}
			case Cmd::CopyTextureRegion: {
				auto& a = *(CopyTextureRegionArgs*)raw;
				if (a.dst.pResource && a.src.pResource) list->CopyTextureRegion(&a.dst, a.dstX, a.dstY, a.dstZ, &a.src, a.srcBox);
				return;
			}
			case Cmd::CopyResource: {
				auto& a = *(CopyResourceArgs*)raw;
				if (a.dst && a.src) list->CopyResource(a.dst, a.src);
				return;
			}
			case Cmd::CopyTiles: {
				auto& a = *(CopyTilesArgs*)raw;
				if (a.tiled && a.buffer) list->CopyTiles(a.tiled, &a.start, &a.size, a.buffer, a.bufferOffset, a.flags);
				return;
			}
			case Cmd::ResolveSubresource: {
				auto& a = *(ResolveSubresourceArgs*)raw;
				if (a.dst && a.src) list->ResolveSubresource(a.dst, a.dstSubresource, a.src, a.srcSubresource, a.format);
				return;
			}
			case Cmd::IASetPrimitiveTopology: list->IASetPrimitiveTopology(((TopologyArgs*)raw)->topology); return;
			case Cmd::RSSetViewports: {
				auto& a = *(ViewportsArgs*)raw;
				list->RSSetViewports(a.count, a.viewports);
				return;
			}
			case Cmd::RSSetScissorRects: {
				auto& a = *(ScissorsArgs*)raw;
				list->RSSetScissorRects(a.count, a.rects);
				return;
			}
			case Cmd::OMSetBlendFactor: list->OMSetBlendFactor(((BlendFactorArgs*)raw)->factor); return;
			case Cmd::OMSetStencilRef: list->OMSetStencilRef(((StencilRefArgs*)raw)->ref); return;
			case Cmd::ResourceBarrier: {
				auto& a = *(BarrierArgs*)raw;
				// Barriers on resources the replay could not make are left out.
				std::vector<D3D12_RESOURCE_BARRIER> kept;
				for (UINT i = 0; i < a.count; ++i) {
					const D3D12_RESOURCE_BARRIER& b = a.barriers[i];
					if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && !b.Transition.pResource) continue;
					kept.push_back(b);
				}
				if (!kept.empty()) list->ResourceBarrier((UINT)kept.size(), kept.data());
				return;
			}
			case Cmd::ExecuteBundle: return;
			case Cmd::SetDescriptorHeaps: {
				auto& a = *(DescriptorHeapsArgs*)raw;
				ID3D12DescriptorHeap* heaps[2] = {};
				UINT count = 0;
				for (UINT i = 0; i < a.count; ++i)
					if (a.heaps[i]) heaps[count++] = a.heaps[i];
				if (count) list->SetDescriptorHeaps(count, heaps);
				return;
			}
			case Cmd::SetComputeRootSignature: list->SetComputeRootSignature(((RootSignatureArgs*)raw)->rootSignature); return;
			case Cmd::SetGraphicsRootSignature: list->SetGraphicsRootSignature(((RootSignatureArgs*)raw)->rootSignature); return;
			case Cmd::SetComputeRootDescriptorTable: {
				auto& a = *(RootTableArgs*)raw;
				list->SetComputeRootDescriptorTable(a.index, a.base);
				return;
			}
			case Cmd::SetGraphicsRootDescriptorTable: {
				auto& a = *(RootTableArgs*)raw;
				list->SetGraphicsRootDescriptorTable(a.index, a.base);
				return;
			}
			case Cmd::SetComputeRoot32BitConstant: {
				auto& a = *(RootConstantArgs*)raw;
				list->SetComputeRoot32BitConstant(a.index, a.value, a.offset);
				return;
			}
			case Cmd::SetGraphicsRoot32BitConstant: {
				auto& a = *(RootConstantArgs*)raw;
				list->SetGraphicsRoot32BitConstant(a.index, a.value, a.offset);
				return;
			}
			case Cmd::SetComputeRoot32BitConstants: {
				auto& a = *(RootConstantsArgs*)raw;
				list->SetComputeRoot32BitConstants(a.index, a.count, a.values, a.offset);
				return;
			}
			case Cmd::SetGraphicsRoot32BitConstants: {
				auto& a = *(RootConstantsArgs*)raw;
				list->SetGraphicsRoot32BitConstants(a.index, a.count, a.values, a.offset);
				return;
			}
			case Cmd::SetComputeRootConstantBufferView: list->SetComputeRootConstantBufferView(((RootViewArgs*)raw)->index, ((RootViewArgs*)raw)->address); return;
			case Cmd::SetGraphicsRootConstantBufferView: list->SetGraphicsRootConstantBufferView(((RootViewArgs*)raw)->index, ((RootViewArgs*)raw)->address); return;
			case Cmd::SetComputeRootShaderResourceView: list->SetComputeRootShaderResourceView(((RootViewArgs*)raw)->index, ((RootViewArgs*)raw)->address); return;
			case Cmd::SetGraphicsRootShaderResourceView: list->SetGraphicsRootShaderResourceView(((RootViewArgs*)raw)->index, ((RootViewArgs*)raw)->address); return;
			case Cmd::SetComputeRootUnorderedAccessView: list->SetComputeRootUnorderedAccessView(((RootViewArgs*)raw)->index, ((RootViewArgs*)raw)->address); return;
			case Cmd::SetGraphicsRootUnorderedAccessView: list->SetGraphicsRootUnorderedAccessView(((RootViewArgs*)raw)->index, ((RootViewArgs*)raw)->address); return;
			case Cmd::IASetIndexBuffer: list->IASetIndexBuffer(((IndexBufferArgs*)raw)->view); return;
			case Cmd::IASetVertexBuffers: {
				auto& a = *(VertexBuffersArgs*)raw;
				list->IASetVertexBuffers(a.startSlot, a.count, a.views);
				return;
			}
			case Cmd::SOSetTargets: {
				auto& a = *(SOTargetsArgs*)raw;
				list->SOSetTargets(a.startSlot, a.count, a.views);
				return;
			}
			case Cmd::OMSetRenderTargets: {
				auto& a = *(RenderTargetsArgs*)raw;
				list->OMSetRenderTargets(a.count, a.count ? a.targets : nullptr, FALSE, a.depthStencil);
				return;
			}
			case Cmd::ClearDepthStencilView: {
				auto& a = *(ClearDepthStencilArgs*)raw;
				if (a.view.ptr) list->ClearDepthStencilView(a.view, a.flags, a.depth, a.stencil, a.rectCount, a.rects);
				return;
			}
			case Cmd::ClearRenderTargetView: {
				auto& a = *(ClearRenderTargetArgs*)raw;
				if (a.view.ptr) list->ClearRenderTargetView(a.view, a.color, a.rectCount, a.rects);
				return;
			}
			case Cmd::ClearUnorderedAccessViewUint: {
				auto& a = *(ClearUavUintArgs*)raw;
				if (a.resource && a.gpuView.ptr && a.cpuView.ptr) list->ClearUnorderedAccessViewUint(a.gpuView, a.cpuView, a.resource, a.values, a.rectCount, a.rects);
				return;
			}
			case Cmd::ClearUnorderedAccessViewFloat: {
				auto& a = *(ClearUavFloatArgs*)raw;
				if (a.resource && a.gpuView.ptr && a.cpuView.ptr) list->ClearUnorderedAccessViewFloat(a.gpuView, a.cpuView, a.resource, a.values, a.rectCount, a.rects);
				return;
			}
			case Cmd::DiscardResource: {
				auto& a = *(DiscardArgs*)raw;
				if (a.resource) list->DiscardResource(a.resource, a.region);
				return;
			}
			case Cmd::BeginQuery: {
				auto& a = *(QueryArgs*)raw;
				if (a.heap) list->BeginQuery(a.heap, a.type, a.index);
				return;
			}
			case Cmd::EndQuery: {
				auto& a = *(QueryArgs*)raw;
				if (a.heap) list->EndQuery(a.heap, a.type, a.index);
				return;
			}
			case Cmd::ResolveQueryData: {
				auto& a = *(ResolveQueryArgs*)raw;
				if (a.heap && a.destination) list->ResolveQueryData(a.heap, a.type, a.start, a.count, a.destination, a.offset);
				return;
			}
			case Cmd::SetPredication: {
				auto& a = *(PredicationArgs*)raw;
				list->SetPredication(a.buffer, a.offset, a.op);
				return;
			}
			case Cmd::SetMarker: {
				auto& a = *(MarkerArgs*)raw;
				list->SetMarker(a.metadata, a.data, a.size);
				return;
			}
			case Cmd::BeginEvent: {
				auto& a = *(MarkerArgs*)raw;
				list->BeginEvent(a.metadata, a.data, a.size);
				return;
			}
			case Cmd::EndEvent: list->EndEvent(); return;
			case Cmd::ExecuteIndirect: {
				auto& a = *(ExecuteIndirectArgs*)raw;
				if (a.signature && a.arguments) list->ExecuteIndirect(a.signature, a.maxCount, a.arguments, a.argumentsOffset, a.countBuffer, a.countOffset);
				return;
			}
			case Cmd::AtomicCopyBufferUINT:
			case Cmd::AtomicCopyBufferUINT64: {
				auto& a = *(AtomicCopyArgs*)raw;
				auto* list1 = As<ID3D12GraphicsCommandList1>(list);
				if (!list1 || !a.dst || !a.src) return;
				if (command.command == Cmd::AtomicCopyBufferUINT) list1->AtomicCopyBufferUINT(a.dst, a.dstOffset, a.src, a.srcOffset, a.dependencies, a.dependentResources, a.dependentRanges);
				else list1->AtomicCopyBufferUINT64(a.dst, a.dstOffset, a.src, a.srcOffset, a.dependencies, a.dependentResources, a.dependentRanges);
				return;
			}
			case Cmd::OMSetDepthBounds: {
				auto& a = *(DepthBoundsArgs*)raw;
				if (auto* list1 = As<ID3D12GraphicsCommandList1>(list)) list1->OMSetDepthBounds(a.min, a.max);
				return;
			}
			case Cmd::SetSamplePositions: {
				auto& a = *(SamplePositionsArgs*)raw;
				if (auto* list1 = As<ID3D12GraphicsCommandList1>(list)) list1->SetSamplePositions(a.samplesPerPixel, a.pixels, a.positions);
				return;
			}
			case Cmd::ResolveSubresourceRegion: {
				auto& a = *(ResolveRegionArgs*)raw;
				auto* list1 = As<ID3D12GraphicsCommandList1>(list);
				if (list1 && a.dst && a.src) list1->ResolveSubresourceRegion(a.dst, a.dstSubresource, a.dstX, a.dstY, a.src, a.srcSubresource, a.srcRect, a.format, a.mode);
				return;
			}
			case Cmd::SetViewInstanceMask: {
				if (auto* list1 = As<ID3D12GraphicsCommandList1>(list)) list1->SetViewInstanceMask(((ViewInstanceMaskArgs*)raw)->mask);
				return;
			}
			case Cmd::WriteBufferImmediate: {
				auto& a = *(WriteImmediateArgs*)raw;
				if (auto* list2 = As<ID3D12GraphicsCommandList2>(list)) list2->WriteBufferImmediate(a.count, a.params, a.modes);
				return;
			}
			case Cmd::BeginRenderPass: {
				auto& a = *(BeginRenderPassArgs*)raw;
				if (auto* list4 = As<ID3D12GraphicsCommandList4>(list)) list4->BeginRenderPass(a.count, a.targets, a.depthStencil, a.flags);
				return;
			}
			case Cmd::EndRenderPass:
				if (auto* list4 = As<ID3D12GraphicsCommandList4>(list)) list4->EndRenderPass();
				return;
			case Cmd::RSSetShadingRate: {
				auto& a = *(ShadingRateArgs*)raw;
				if (auto* list5 = As<ID3D12GraphicsCommandList5>(list)) list5->RSSetShadingRate(a.rate, a.hasCombiners ? a.combiners : nullptr);
				return;
			}
			case Cmd::RSSetShadingRateImage:
				if (auto* list5 = As<ID3D12GraphicsCommandList5>(list)) list5->RSSetShadingRateImage(((ShadingRateImageArgs*)raw)->image);
				return;
			case Cmd::Barrier: {
				auto& a = *(EnhancedBarrierArgs*)raw;
				if (auto* list7 = As<ID3D12GraphicsCommandList7>(list)) list7->Barrier(a.count, a.groups);
				return;
			}
			case Cmd::OMSetFrontAndBackStencilRef: {
				auto& a = *(StencilRefsArgs*)raw;
				if (auto* list8 = As<ID3D12GraphicsCommandList8>(list)) list8->OMSetFrontAndBackStencilRef(a.front, a.back);
				return;
			}
			case Cmd::RSSetDepthBias: {
				auto& a = *(DepthBiasArgs*)raw;
				if (auto* list9 = As<ID3D12GraphicsCommandList9>(list)) list9->RSSetDepthBias(a.bias, a.clamp, a.slopeScaled);
				return;
			}
			case Cmd::IASetIndexBufferStripCutValue:
				if (auto* list9 = As<ID3D12GraphicsCommandList9>(list)) list9->IASetIndexBufferStripCutValue(((StripCutArgs*)raw)->value);
				return;
			case Cmd::Unsupported: return;
			default: return;
		}
	}
}
