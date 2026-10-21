// simulate.h - the state a D3D12 event sees, worked out on the CPU from the recorded commands (docs/22 "Replay"):
// the bound pipeline and root signatures, every root argument, input assembly, viewports, render targets, and the
// descriptor heaps' contents at that point of the frame. The Pipeline view and the mesh, shader and readback requests
// start from it. Values are the model's tagged ids (model.h).
#pragma once

#include "model.h"

namespace fb::replay::d3d12
{
	struct RootArgument
	{
		enum class Kind : u8 { None, Table, Cbv, Srv, Uav, Constants } kind = Kind::None;
		u64 value = 0;                  // a table's tagged descriptor, a root view's tagged address
		std::vector<u32> constants;
	};

	struct VertexBufferBinding
	{
		u64 address = 0;                // tagged
		u32 size = 0;
		u32 stride = 0;
	};

	struct Snapshot
	{
		const Event* event = nullptr;
		Cmd command = Cmd::Count;
		u64 queue = 0;
		u64 list = 0;
		u64 pipeline = 0;
		u64 graphicsRoot = 0;
		u64 computeRoot = 0;
		u64 heaps[2] = {};
		std::vector<RootArgument> graphics = std::vector<RootArgument>(64);
		std::vector<RootArgument> compute = std::vector<RootArgument>(64);
		D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		std::vector<D3D12_VIEWPORT> viewports;
		std::vector<D3D12_RECT> scissors;
		float blendFactor[4] = {1, 1, 1, 1};
		u32 stencilRef = 0;
		bool indexed = false;
		u64 indexAddress = 0;           // tagged
		u32 indexSize = 0;
		DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
		std::vector<VertexBufferBinding> vertexBuffers = std::vector<VertexBufferBinding>(32);
		std::vector<u64> renderTargets;   // tagged descriptors
		u64 depthStencil = 0;
		// Heap slots written during the frame before this event's submission: heap -> index -> DescriptorWrite record.
		std::map<u64, std::map<u32, std::pair<const u8*, u64>>> written;
	};

	// The state at `eid` (before its own command acts, but including it when the event itself sets state).
	Snapshot Simulate(const Model& model, u32 eid);

	// The view in a heap slot at the snapshot's point, decoded with the model's ids; false when nothing was written there.
	bool DescriptorAt(const Model& model, const Snapshot& snapshot, u64 heap, u32 index, DescriptorWrite& out);
}
