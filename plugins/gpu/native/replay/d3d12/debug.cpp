// Shader debugging for D3D12 (docs/22 "Shader debugging"): the event's stage - DXBC, Shader Model 5.1 - instrumented for
// the chosen pixel, vertex or compute thread (common/dxbc.h); a root signature that is the list's own plus a root UAV
// for the trace, at register 0 of the first space from 900 the signature leaves free; the instrumented pipeline run
// once in the action's place with every root argument bound again; and the trace read back - every value each chosen
// invocation made, in order, against the stage's listing.
#include "d3d12replay.h"
#include "dxbc.h"

#include <set>

namespace fb::replay::d3d12
{
	namespace
	{
		// What a debug run makes, released with it.
		struct DebugObjects
		{
			~DebugObjects() {
				for (IUnknown* object : {(IUnknown*)pipeline, (IUnknown*)root, (IUnknown*)trace, (IUnknown*)upload, (IUnknown*)readback})
					if (object) object->Release();
			}

			ID3D12RootSignature* root = nullptr;
			ID3D12PipelineState* pipeline = nullptr;
			ID3D12Resource* trace = nullptr;
			ID3D12Resource* upload = nullptr;
			ID3D12Resource* readback = nullptr;
		};

		// The list's root signature with the trace's root UAV after its parameters; `space` gets the trace's register
		// space and `parameters` the signature's own count.
		ID3D12RootSignature* TraceRootSignature(ID3D12Device* device, const RootSignatureCreate& record, D3D12_ROOT_SIGNATURE_FLAGS allow, u32& space, u32& parameters) {
			ID3D12VersionedRootSignatureDeserializer* deserializer = nullptr;
			if (FAILED(D3D12CreateVersionedRootSignatureDeserializer(record.blob, (SIZE_T)record.blobSize, __uuidof(ID3D12VersionedRootSignatureDeserializer), (void**)&deserializer)))
				Fail("the root signature could not be read");
			const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* versioned = nullptr;
			if (FAILED(deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &versioned))) {
				deserializer->Release();
				Fail("the root signature could not be read");
			}
			D3D12_ROOT_SIGNATURE_DESC1 desc = versioned->Desc_1_1;
			parameters = desc.NumParameters;
			u32 cost = 2;
			std::set<u32> spaces;
			for (UINT i = 0; i < desc.NumParameters; ++i) {
				const D3D12_ROOT_PARAMETER1& p = desc.pParameters[i];
				if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
					cost += 1;
					for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r)
						if (p.DescriptorTable.pDescriptorRanges[r].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) spaces.insert(p.DescriptorTable.pDescriptorRanges[r].RegisterSpace);
				}
				else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) cost += p.Constants.Num32BitValues;
				else {
					cost += 2;
					if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV) spaces.insert(p.Descriptor.RegisterSpace);
				}
			}
			if (cost > 64) {
				deserializer->Release();
				Fail("the root signature has no room for the trace's root view (it would take %u of 64 DWORDs)", cost);
			}
			for (space = 900; spaces.count(space);) ++space;
			std::vector<D3D12_ROOT_PARAMETER1> list(desc.pParameters, desc.pParameters + desc.NumParameters);
			D3D12_ROOT_PARAMETER1 trace = {};
			trace.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
			trace.Descriptor = {0, space, D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
			trace.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			list.push_back(trace);
			desc.NumParameters = (UINT)list.size();
			desc.pParameters = list.data();
			desc.Flags &= ~allow;
			D3D12_VERSIONED_ROOT_SIGNATURE_DESC changed = {D3D_ROOT_SIGNATURE_VERSION_1_1};
			changed.Desc_1_1 = desc;
			ID3DBlob* serialized = nullptr;
			ID3DBlob* errors = nullptr;
			const HRESULT hr = D3D12SerializeVersionedRootSignature(&changed, &serialized, &errors);
			deserializer->Release();
			std::string why = errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize()) : "";
			if (errors) errors->Release();
			if (FAILED(hr)) Fail("the trace's root signature could not be made: %s", why.c_str());
			ID3D12RootSignature* made = nullptr;
			const HRESULT created = device->CreateRootSignature(record.nodeMask, serialized->GetBufferPointer(), serialized->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&made);
			serialized->Release();
			if (FAILED(created)) Fail("the trace's root signature could not be made (0x%08lx)", (unsigned long)created);
			return made;
		}

		// Runs the instrumented pipeline in the action's place, with the trace's root signature and every root argument
		// bound again; the list's own come back for the action itself.
		class DebugHooks final : public ReplayHooks
		{
		public:
			DebugHooks(ReplayDevice& device, const Snapshot& s, bool compute, u32 parameters, const DebugObjects& objects)
				: device_(device), s_(s), compute_(compute), parameters_(parameters), objects_(objects) {}

			void Before(ID3D12GraphicsCommandList* list, const CommandRef& command) override {
				if (command.eid != s_.event->eid) return;
				Bind(list, objects_.root, true);
				list->SetPipelineState(objects_.pipeline);
				device_.Repeat(list, command);
				Bind(list, (ID3D12RootSignature*)device_.Object(compute_ ? s_.computeRoot : s_.graphicsRoot), false);
				list->SetPipelineState((ID3D12PipelineState*)device_.Object(s_.pipeline));
				ran = true;
			}

			bool ran = false;

		private:
			void Bind(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root, bool trace) {
				const std::vector<RootArgument>& arguments = compute_ ? s_.compute : s_.graphics;
				if (compute_) list->SetComputeRootSignature(root);
				else list->SetGraphicsRootSignature(root);
				for (u32 i = 0; i < parameters_ && i < arguments.size(); ++i) {
					const RootArgument& a = arguments[i];
					const u64 address = a.kind == RootArgument::Kind::Table ? 0 : device_.Address(AddressId(a.value), AddressOffset(a.value));
					switch (a.kind) {
						case RootArgument::Kind::Table: {
							const D3D12_GPU_DESCRIPTOR_HANDLE table = {device_.GpuDescriptor(DescriptorHeapId(a.value), DescriptorIndex(a.value))};
							compute_ ? list->SetComputeRootDescriptorTable(i, table) : list->SetGraphicsRootDescriptorTable(i, table);
							break;
						}
						case RootArgument::Kind::Cbv: compute_ ? list->SetComputeRootConstantBufferView(i, address) : list->SetGraphicsRootConstantBufferView(i, address); break;
						case RootArgument::Kind::Srv: compute_ ? list->SetComputeRootShaderResourceView(i, address) : list->SetGraphicsRootShaderResourceView(i, address); break;
						case RootArgument::Kind::Uav: compute_ ? list->SetComputeRootUnorderedAccessView(i, address) : list->SetGraphicsRootUnorderedAccessView(i, address); break;
						case RootArgument::Kind::Constants:
							if (a.constants.empty()) break;
							if (compute_) list->SetComputeRoot32BitConstants(i, (UINT)a.constants.size(), a.constants.data(), 0);
							else list->SetGraphicsRoot32BitConstants(i, (UINT)a.constants.size(), a.constants.data(), 0);
							break;
						default: break;
					}
				}
				if (!trace) return;
				const D3D12_GPU_VIRTUAL_ADDRESS at = objects_.trace->GetGPUVirtualAddress();
				compute_ ? list->SetComputeRootUnorderedAccessView(parameters_, at) : list->SetGraphicsRootUnorderedAccessView(parameters_, at);
			}

			ReplayDevice& device_;
			const Snapshot& s_;
			bool compute_;
			u32 parameters_;
			const DebugObjects& objects_;
		};

		ID3D12Resource* MakeBuffer(ID3D12Device* device, D3D12_HEAP_TYPE type, u64 bytes, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
			D3D12_HEAP_PROPERTIES heap = {type};
			D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, bytes, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags};
			ID3D12Resource* buffer = nullptr;
			if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, __uuidof(ID3D12Resource), (void**)&buffer)))
				Fail("the trace buffer could not be made");
			return buffer;
		}
	}

	void D3D12Replay::DebugShader(const JsonValue& params, JsonWriter& out) {
		ReplayDevice& device = Device();
		const u32 eid = (u32)params["eid"].Uint(0);
		std::string stage = params["stage"].Str("ps");
		if (stage == "fs") stage = "ps";
		if (stage != "vs" && stage != "ps" && stage != "cs") Fail("the shader debugger steps vertex (vs), pixel (ps) and compute (cs) shaders");
		const bool compute = stage == "cs";
		const Snapshot s = Simulate(model_, eid);
		if (!s.event) Fail("there is no event %u", eid);
		if (s.event->kind != (compute ? EventKind::Dispatch : EventKind::Draw)) Fail("event %u is not a %s", eid, compute ? "dispatch" : "draw");
		PipelineCreate pso = {};
		const ObjectEntry* entry = model_.Find(s.pipeline);
		if (!entry || !model_.DecodeModel(entry->record, entry->recordSize, pso)) Fail("no pipeline state is bound at event %u", eid);
		const D3D12_SHADER_BYTECODE code = compute ? pso.cs : stage == "vs" ? pso.vs : pso.ps;
		if (!code.pShaderBytecode || !code.BytecodeLength) Fail("the pipeline has no %s stage", stage.c_str());
		if (IsDxil(code)) Fail("the shader is DXIL (Shader Model 6); the debugger instruments DXBC (Shader Model 5.1) so far");
		const u64 rootId = compute ? s.computeRoot : s.graphicsRoot;
		const ObjectEntry* rootEntry = model_.Find(rootId);
		RootSignatureCreate rootRecord = {};
		if (!rootEntry || !model_.DecodeModel(rootEntry->record, rootEntry->recordSize, rootRecord)) Fail("no root signature is bound at event %u", eid);

		DebugObjects objects;
		const D3D12_ROOT_SIGNATURE_FLAGS allow = compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE
			: stage == "vs" ? D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS : D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
		DxbcTarget target;
		u32 parameters = 0;
		objects.root = TraceRootSignature(device.Device(), rootRecord, allow, target.space, parameters);
		target.x = (u32)params["x"].Uint(0);
		target.y = (u32)params["y"].Uint(0);
		target.vertex = (u32)params["vertex"].Uint(0);
		target.instance = (u32)params["instance"].Uint(0);
		for (u32 k = 0; k < 3; ++k) target.thread[k] = (u32)params["thread"].At(k).Uint(0);
		DxbcInstrumented instrumented;
		if (!InstrumentDxbc((const u8*)code.pShaderBytecode, code.BytecodeLength, target, instrumented)) Fail("the shader cannot be instrumented: %s", instrumented.problem.c_str());
		const D3D12_SHADER_BYTECODE changed = {instrumented.bytes.data(), instrumented.bytes.size()};
		objects.pipeline = device.Variant(s.pipeline, [&](PipelineCreate& p) {
			p.rootSignature = objects.root;
			(compute ? p.cs : stage == "vs" ? p.vs : p.ps) = changed;
		});
		if (!objects.pipeline) Fail("the instrumented pipeline could not be made");

		// The trace starts with its count of bytes handed out; the rest is zero, as a new committed buffer is.
		objects.trace = MakeBuffer(device.Device(), D3D12_HEAP_TYPE_DEFAULT, kDxbcTraceBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
		objects.upload = MakeBuffer(device.Device(), D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
		u32* first = nullptr;
		if (FAILED(objects.upload->Map(0, nullptr, (void**)&first))) Fail("the trace buffer could not be made");
		first[0] = kDxbcTraceFirst;
		objects.upload->Unmap(0, nullptr);
		device.BeginUtility()->CopyBufferRegion(objects.trace, 0, objects.upload, 0, 4);
		device.RunUtility();

		DebugHooks hooks(device, s, compute, parameters, objects);
		device.ReplayTo(eid, &hooks);
		Unreplayed();
		if (!hooks.ran) Fail("the replay did not reach event %u", eid);
		objects.readback = MakeReadback(device.Device(), kDxbcTraceBytes);
		device.BeginUtility()->CopyBufferRegion(objects.readback, 0, objects.trace, 0, kDxbcTraceBytes);
		device.RunUtility();
		const u8* trace = nullptr;
		const D3D12_RANGE read = {0, kDxbcTraceBytes};
		if (FAILED(objects.readback->Map(0, &read, (void**)&trace))) Fail("the trace could not be read back");

		std::map<u32, u32> lines;
		const std::string listing = DxbcListing(code, lines);
		out.Uint("eid", eid);
		out.String("stage", stage);
		out.String("pipeline", model_.NameOf(s.pipeline));
		out.String("entry", "");
		out.String("listing", listing);
		out.BeginArray("sites");
		for (const DxbcSite& site : instrumented.sites) {
			auto line = lines.find(site.offset);
			u32 components = 0;
			for (u32 k = 0; k < 4; ++k) components += site.mask >> k & 1;
			out.BeginArray().Uint(line != lines.end() ? line->second : 0).Uint(site.type).Uint(components).Bool(site.store).String(site.name).EndArray();
		}
		out.EndArray();
		u32 used = 0;
		memcpy(&used, trace, 4);
		used = std::min(used, kDxbcTraceBytes);
		const u32 region = kDxbcRecords * kDxbcRecordBytes;
		const u32 regions = used > kDxbcTraceFirst ? std::min((used - kDxbcTraceFirst) / region, kDxbcRegions) : 0;
		out.BeginArray("invocations");
		for (u32 r = 0; r < regions; ++r) {
			const u8* records = trace + kDxbcTraceFirst + (size_t)r * region;
			out.BeginObject();
			out.BeginArray("steps");
			u32 k = 0;
			for (; k < kDxbcRecords; ++k) {
				u32 record[5];
				memcpy(record, records + (size_t)k * kDxbcRecordBytes, sizeof(record));
				if (!record[0] || record[0] > instrumented.sites.size()) break;
				out.BeginArray().Uint(record[0] - 1).String(DxbcValueText(instrumented.sites[record[0] - 1], record + 1)).EndArray();
			}
			out.EndArray();
			out.Bool("truncated", k == kDxbcRecords);
			out.EndObject();
		}
		out.EndArray();
		const D3D12_RANGE none = {0, 0};
		objects.readback->Unmap(0, &none);
	}
}
