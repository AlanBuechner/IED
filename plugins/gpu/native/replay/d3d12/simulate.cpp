#include "simulate.h"

namespace fb::replay::d3d12
{
	static void ApplyWrites(const Model& model, u32 untilOp, Snapshot& snapshot) {
		ModelMap map(model);
		for (u32 i = 0; i < untilOp && i < model.frame.size(); ++i) {
			const FrameOp& op = model.frame[i];
			if (op.kind == OpKind::Descriptor) {
				ByteReader peek(op.data, (size_t)op.size);
				const u64 heap = peek.Get<u64>();
				const u32 index = peek.Get<u32>();
				snapshot.written[heap][index] = {op.data, op.size};
			}
			else if (op.kind == OpKind::CopyDescriptors) {
				DescriptorCopy copy = {};
				if (!model.Decode(op.data, op.size, copy, &map) || !copy.srcHeap) continue;
				for (u32 k = 0; k < copy.count; ++k) {
					const u32 from = copy.srcIndex + k;
					auto source = snapshot.written.find(copy.srcHeap);
					const std::pair<const u8*, u64>* slot = nullptr;
					if (source != snapshot.written.end()) {
						auto found = source->second.find(from);
						if (found != source->second.end()) slot = &found->second;
					}
					if (!slot) {
						auto heap = model.descriptors.find(copy.srcHeap);
						if (heap != model.descriptors.end()) {
							auto found = heap->second.find(from);
							if (found != heap->second.end()) slot = &found->second;
						}
					}
					if (slot) snapshot.written[copy.dstHeap][copy.dstIndex + k] = *slot;
				}
			}
		}
	}

	bool DescriptorAt(const Model& model, const Snapshot& snapshot, u64 heap, u32 index, DescriptorWrite& out) {
		const std::pair<const u8*, u64>* slot = nullptr;
		auto written = snapshot.written.find(heap);
		if (written != snapshot.written.end()) {
			auto found = written->second.find(index);
			if (found != written->second.end()) slot = &found->second;
		}
		if (!slot) {
			auto initial = model.descriptors.find(heap);
			if (initial == model.descriptors.end()) return false;
			auto found = initial->second.find(index);
			if (found == initial->second.end()) return false;
			slot = &found->second;
		}
		out = {};
		if (!model.DecodeModel(slot->first, slot->second, out)) return false;
		// A slot copied from another heap keeps its own place.
		out.heap = heap;
		out.index = index;
		return true;
	}

	static void Apply(Snapshot& s, Cmd command, void* raw) {
		auto setRoot = [](std::vector<RootArgument>& args, UINT index, RootArgument::Kind kind, u64 value) {
			if (index >= args.size()) return;
			args[index].kind = kind;
			args[index].value = value;
			args[index].constants.clear();
		};
		auto setConstants = [](std::vector<RootArgument>& args, UINT index, UINT offset, UINT count, const UINT* values) {
			if (index >= args.size()) return;
			RootArgument& arg = args[index];
			if (arg.kind != RootArgument::Kind::Constants) arg.constants.clear();
			arg.kind = RootArgument::Kind::Constants;
			if (arg.constants.size() < offset + count) arg.constants.resize(offset + count);
			for (UINT i = 0; i < count; ++i) arg.constants[offset + i] = values[i];
		};
		switch (command) {
			case Cmd::Reset: s.pipeline = IdOf(((ResetArgs*)raw)->pipelineState); break;
			case Cmd::ClearState: {
				const u64 pipeline = IdOf(((PipelineArgs*)raw)->pipelineState);
				Snapshot cleared;
				cleared.event = s.event;
				cleared.command = s.command;
				cleared.queue = s.queue;
				cleared.list = s.list;
				cleared.written = std::move(s.written);
				s = std::move(cleared);
				s.pipeline = pipeline;
				break;
			}
			case Cmd::SetPipelineState: s.pipeline = IdOf(((PipelineArgs*)raw)->pipelineState); break;
			case Cmd::SetDescriptorHeaps: {
				auto& a = *(DescriptorHeapsArgs*)raw;
				s.heaps[0] = s.heaps[1] = 0;
				for (UINT i = 0; i < a.count && i < 2; ++i) s.heaps[i] = IdOf(a.heaps[i]);
				break;
			}
			case Cmd::SetGraphicsRootSignature:
				s.graphicsRoot = IdOf(((RootSignatureArgs*)raw)->rootSignature);
				for (RootArgument& arg : s.graphics) arg = RootArgument();
				break;
			case Cmd::SetComputeRootSignature:
				s.computeRoot = IdOf(((RootSignatureArgs*)raw)->rootSignature);
				for (RootArgument& arg : s.compute) arg = RootArgument();
				break;
			case Cmd::SetGraphicsRootDescriptorTable: setRoot(s.graphics, ((RootTableArgs*)raw)->index, RootArgument::Kind::Table, ((RootTableArgs*)raw)->base.ptr); break;
			case Cmd::SetComputeRootDescriptorTable: setRoot(s.compute, ((RootTableArgs*)raw)->index, RootArgument::Kind::Table, ((RootTableArgs*)raw)->base.ptr); break;
			case Cmd::SetGraphicsRootConstantBufferView: setRoot(s.graphics, ((RootViewArgs*)raw)->index, RootArgument::Kind::Cbv, ((RootViewArgs*)raw)->address); break;
			case Cmd::SetComputeRootConstantBufferView: setRoot(s.compute, ((RootViewArgs*)raw)->index, RootArgument::Kind::Cbv, ((RootViewArgs*)raw)->address); break;
			case Cmd::SetGraphicsRootShaderResourceView: setRoot(s.graphics, ((RootViewArgs*)raw)->index, RootArgument::Kind::Srv, ((RootViewArgs*)raw)->address); break;
			case Cmd::SetComputeRootShaderResourceView: setRoot(s.compute, ((RootViewArgs*)raw)->index, RootArgument::Kind::Srv, ((RootViewArgs*)raw)->address); break;
			case Cmd::SetGraphicsRootUnorderedAccessView: setRoot(s.graphics, ((RootViewArgs*)raw)->index, RootArgument::Kind::Uav, ((RootViewArgs*)raw)->address); break;
			case Cmd::SetComputeRootUnorderedAccessView: setRoot(s.compute, ((RootViewArgs*)raw)->index, RootArgument::Kind::Uav, ((RootViewArgs*)raw)->address); break;
			case Cmd::SetGraphicsRoot32BitConstant: {
				auto& a = *(RootConstantArgs*)raw;
				setConstants(s.graphics, a.index, a.offset, 1, &a.value);
				break;
			}
			case Cmd::SetComputeRoot32BitConstant: {
				auto& a = *(RootConstantArgs*)raw;
				setConstants(s.compute, a.index, a.offset, 1, &a.value);
				break;
			}
			case Cmd::SetGraphicsRoot32BitConstants: {
				auto& a = *(RootConstantsArgs*)raw;
				setConstants(s.graphics, a.index, a.offset, a.count, a.values);
				break;
			}
			case Cmd::SetComputeRoot32BitConstants: {
				auto& a = *(RootConstantsArgs*)raw;
				setConstants(s.compute, a.index, a.offset, a.count, a.values);
				break;
			}
			case Cmd::IASetPrimitiveTopology: s.topology = ((TopologyArgs*)raw)->topology; break;
			case Cmd::IASetIndexBuffer: {
				const D3D12_INDEX_BUFFER_VIEW* view = ((IndexBufferArgs*)raw)->view;
				s.indexed = view != nullptr;
				s.indexAddress = view ? view->BufferLocation : 0;
				s.indexSize = view ? view->SizeInBytes : 0;
				s.indexFormat = view ? view->Format : DXGI_FORMAT_UNKNOWN;
				break;
			}
			case Cmd::IASetVertexBuffers: {
				auto& a = *(VertexBuffersArgs*)raw;
				for (UINT i = 0; i < a.count && a.startSlot + i < s.vertexBuffers.size(); ++i)
					s.vertexBuffers[a.startSlot + i] = {a.views[i].BufferLocation, a.views[i].SizeInBytes, a.views[i].StrideInBytes};
				break;
			}
			case Cmd::RSSetViewports: {
				auto& a = *(ViewportsArgs*)raw;
				s.viewports.assign(a.viewports, a.viewports + a.count);
				break;
			}
			case Cmd::RSSetScissorRects: {
				auto& a = *(ScissorsArgs*)raw;
				s.scissors.assign(a.rects, a.rects + a.count);
				break;
			}
			case Cmd::OMSetBlendFactor: memcpy(s.blendFactor, ((BlendFactorArgs*)raw)->factor, sizeof(s.blendFactor)); break;
			case Cmd::OMSetStencilRef: s.stencilRef = ((StencilRefArgs*)raw)->ref; break;
			case Cmd::OMSetRenderTargets: {
				auto& a = *(RenderTargetsArgs*)raw;
				s.renderTargets.clear();
				for (UINT i = 0; i < a.count; ++i) s.renderTargets.push_back(a.targets[i].ptr);
				s.depthStencil = a.depthStencil ? a.depthStencil->ptr : 0;
				break;
			}
			case Cmd::BeginRenderPass: {
				auto& a = *(BeginRenderPassArgs*)raw;
				s.renderTargets.clear();
				for (UINT i = 0; i < a.count; ++i) s.renderTargets.push_back(a.targets[i].cpuDescriptor.ptr);
				s.depthStencil = a.depthStencil ? a.depthStencil->cpuDescriptor.ptr : 0;
				break;
			}
			default: break;
		}
	}

	Snapshot Simulate(const Model& model, u32 eid) {
		Snapshot snapshot;
		const Event* event = model.EventAt(eid);
		if (!event) return snapshot;
		snapshot.event = event;
		snapshot.queue = event->queue;
		ApplyWrites(model, event->op, snapshot);
		const CommandRef* target = model.CommandOf(*event);
		if (!target) return snapshot;
		snapshot.command = target->command;
		const ListRef& list = model.frame[event->op].lists[(size_t)event->list];
		snapshot.list = list.list;
		ModelMap map(model);
		std::vector<u8> args;
		for (size_t i = 0; i <= (size_t)event->command && i < list.commands.size(); ++i) {
			const CommandRef& command = list.commands[i];
			// The event's own command counts only when it sets state.
			if (i == (size_t)event->command && IsAction(command.command)) break;
			args.assign(CommandArgsSize(command.command) + 16, 0);
			if (command.command == Cmd::Unsupported || !model.DecodeCommand(command, args.data(), &map)) continue;
			Apply(snapshot, command.command, args.data());
		}
		return snapshot;
	}
}
