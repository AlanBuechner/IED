// The Pipeline view's tree (docs/22 "Views"): the state an event sees, by stage - input assembly, the shaders, the
// rasterizer, the output merger, then the root signature with every argument and every descriptor a table reaches
// resolved to its resource and name. Items name a resource (for the resource viewer) or a shader stage.
#include "d3d12replay.h"
#include "d3d12_names.h"
#include "formats.h"

// The separator the UI shows between a value's parts (a middle dot), written as its UTF-8 bytes.
#define DOT "\xC2\xB7"

namespace fb::replay::d3d12
{
	namespace
	{
		// Items written into the tree: {label, value, resource?, shader?, children?}.
		class Tree
		{
		public:
			explicit Tree(JsonWriter& out) : out_(out) {}
			void Group(const char* name, const std::string& summary) {
				out_.BeginObject();
				out_.String("name", name);
				out_.String("summary", summary);
				out_.BeginArray("items");
			}
			void EndGroup() {
				out_.EndArray();
				out_.EndObject();
			}
			void Item(const std::string& label, const std::string& value, u64 resource = 0, const char* shader = nullptr) {
				out_.BeginObject();
				out_.String("label", label);
				out_.String("value", value);
				if (resource) out_.Uint("resource", resource);
				if (shader) out_.String("shader", shader);
				// A view's role, which the UI's "the event's output" looks for.
				const char* role = value.compare(0, 3, "RTV") == 0 ? "rt" : value.compare(0, 3, "DSV") == 0 ? "depth" : value.compare(0, 3, "UAV") == 0 ? "uav" : nullptr;
				if (resource && role) out_.String("role", role);
				out_.EndObject();
			}
			void Parent(const std::string& label, const std::string& value, u64 resource = 0) {
				out_.BeginObject();
				out_.String("label", label);
				out_.String("value", value);
				if (resource) out_.Uint("resource", resource);
				out_.BeginArray("children");
			}
			void EndParent() {
				out_.EndArray();
				out_.EndObject();
			}

		private:
			JsonWriter& out_;
		};

		std::string Bytes(u64 bytes) {
			if (bytes >= 1024 * 1024) return Format("%.1f MB", bytes / (1024.0 * 1024.0));
			if (bytes >= 1024) return Format("%.1f KB", bytes / 1024.0);
			return Format("%llu B", (unsigned long long)bytes);
		}

		const char* Name(const char* text) { return text ? text : "?"; }
	}

	static DXGI_FORMAT ResourceFormat(const Model& model, u64 id) {
		const ObjectEntry* entry = model.Find(id);
		ResourceCreate record = {};
		if (!entry || entry->kind != ObjectKind::Resource || !model.DecodeModel(entry->record, entry->recordSize, record)) return DXGI_FORMAT_UNKNOWN;
		return record.desc.Format;
	}

	static std::string ViewText(const Model& model, const DescriptorWrite& view) {
		const u64 resource = IdOf(view.resource);
		switch (view.kind) {
			case ViewKind::Cbv:
				if (!view.hasDesc) return "CBV (null)";
				return Format("CBV " DOT " %s +%llu " DOT " %s", model.NameOf(AddressId(view.cbv.BufferLocation)).c_str(), (unsigned long long)AddressOffset(view.cbv.BufferLocation),
					Bytes(view.cbv.SizeInBytes).c_str());
			case ViewKind::Srv:
				if (!resource) return "SRV (null)";
				if (view.hasDesc)
					return Format("SRV " DOT " %s " DOT " %s " DOT " %s", model.NameOf(resource).c_str(), Name(EnumName(view.srv.ViewDimension)),
						Name(EnumName(view.srv.Format == DXGI_FORMAT_UNKNOWN ? ResourceFormat(model, resource) : view.srv.Format)));
				return Format("SRV " DOT " %s " DOT " %s", model.NameOf(resource).c_str(), Name(EnumName(ResourceFormat(model, resource))));
			case ViewKind::Uav:
				if (!resource) return "UAV (null)";
				if (view.hasDesc)
					return Format("UAV " DOT " %s " DOT " %s " DOT " %s", model.NameOf(resource).c_str(), Name(EnumName(view.uav.ViewDimension)),
						Name(EnumName(view.uav.Format == DXGI_FORMAT_UNKNOWN ? ResourceFormat(model, resource) : view.uav.Format)));
				return Format("UAV " DOT " %s " DOT " %s", model.NameOf(resource).c_str(), Name(EnumName(ResourceFormat(model, resource))));
			case ViewKind::Rtv: return Format("RTV " DOT " %s " DOT " %s", model.NameOf(resource).c_str(), Name(EnumName(view.hasDesc ? view.rtv.Format : ResourceFormat(model, resource))));
			case ViewKind::Dsv: return Format("DSV " DOT " %s " DOT " %s", model.NameOf(resource).c_str(), Name(EnumName(view.hasDesc ? view.dsv.Format : ResourceFormat(model, resource))));
			case ViewKind::Sampler:
			case ViewKind::Sampler2:
				return Format("Sampler " DOT " %s " DOT " %s/%s/%s", Name(EnumName(view.sampler.Filter)), Name(EnumName(view.sampler.AddressU)), Name(EnumName(view.sampler.AddressV)),
					Name(EnumName(view.sampler.AddressW)));
			default: return "(empty)";
		}
	}

	static u64 ViewResource(const DescriptorWrite& view) {
		if (view.kind == ViewKind::Cbv && view.hasDesc) return AddressId(view.cbv.BufferLocation);
		return IdOf(view.resource);
	}

	static const char* RangeLetter(D3D12_DESCRIPTOR_RANGE_TYPE type) {
		switch (type) {
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return "t";
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return "u";
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return "b";
			default: return "s";
		}
	}

	static const char* Visibility(D3D12_SHADER_VISIBILITY visibility) {
		switch (visibility) {
			case D3D12_SHADER_VISIBILITY_VERTEX: return "vertex";
			case D3D12_SHADER_VISIBILITY_HULL: return "hull";
			case D3D12_SHADER_VISIBILITY_DOMAIN: return "domain";
			case D3D12_SHADER_VISIBILITY_GEOMETRY: return "geometry";
			case D3D12_SHADER_VISIBILITY_PIXEL: return "pixel";
			case D3D12_SHADER_VISIBILITY_AMPLIFICATION: return "amplification";
			case D3D12_SHADER_VISIBILITY_MESH: return "mesh";
			default: return "all";
		}
	}

	static void TableItems(const Model& model, const Snapshot& s, const D3D12_ROOT_PARAMETER1& param, const RootArgument& arg, const std::string& label, Tree& tree) {
		if (arg.kind != RootArgument::Kind::Table) {
			tree.Item(label + " table", "not set");
			return;
		}
		const u64 heap = DescriptorHeapId(arg.value);
		const u32 base = DescriptorIndex(arg.value);
		tree.Parent(label + " table", Format("%s[%u]", model.NameOf(heap).c_str(), base));
		u32 offset = 0, shown = 0;
		for (UINT r = 0; r < param.DescriptorTable.NumDescriptorRanges; ++r) {
			const D3D12_DESCRIPTOR_RANGE1& range = param.DescriptorTable.pDescriptorRanges[r];
			if (range.OffsetInDescriptorsFromTableStart != D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) offset = range.OffsetInDescriptorsFromTableStart;
			const bool unbounded = range.NumDescriptors == UINT_MAX;
			const u32 count = unbounded ? 4096 : range.NumDescriptors;
			u32 written = 0;
			for (u32 i = 0; i < count; ++i) {
				DescriptorWrite view;
				const bool present = DescriptorAt(model, s, heap, base + offset + i, view);
				if (unbounded && !present) continue;
				++written;
				if (shown >= 256) continue;
				++shown;
				const std::string reg = Format("%s%u space%u", RangeLetter(range.RangeType), range.BaseShaderRegister + i, range.RegisterSpace);
				if (!present) tree.Item(reg, "(empty)");
				else tree.Item(reg, ViewText(model, view), ViewResource(view));
			}
			if (unbounded) tree.Item(Format("%s%u+ space%u", RangeLetter(range.RangeType), range.BaseShaderRegister, range.RegisterSpace), Format("unbounded: %u written slots shown", written));
			offset += unbounded ? 0 : range.NumDescriptors;
		}
		if (shown >= 256) tree.Item("...", "more descriptors not listed");
		tree.EndParent();
	}

	static void RootSignatureGroup(const Model& model, const Snapshot& s, u64 rootId, const std::vector<RootArgument>& args, Tree& tree) {
		const ObjectEntry* entry = model.Find(rootId);
		RootSignatureCreate record = {};
		if (!entry || !model.DecodeModel(entry->record, entry->recordSize, record)) {
			tree.Group("Root Signature", "none");
			tree.EndGroup();
			return;
		}
		ID3D12VersionedRootSignatureDeserializer* deserializer = nullptr;
		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = nullptr;
		if (FAILED(D3D12CreateVersionedRootSignatureDeserializer(record.blob, (SIZE_T)record.blobSize, __uuidof(ID3D12VersionedRootSignatureDeserializer), (void**)&deserializer)) ||
			FAILED(deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc))) {
			if (deserializer) deserializer->Release();
			tree.Group("Root Signature", model.NameOf(rootId));
			tree.Item("Blob", Bytes(record.blobSize));
			tree.EndGroup();
			return;
		}
		const D3D12_ROOT_SIGNATURE_DESC1& root = desc->Desc_1_1;
		tree.Group("Root Signature", Format("%s " DOT " %u parameters", model.NameOf(rootId).c_str(), root.NumParameters));
		for (UINT p = 0; p < root.NumParameters; ++p) {
			const D3D12_ROOT_PARAMETER1& param = root.pParameters[p];
			const RootArgument arg = p < args.size() ? args[p] : RootArgument();
			const std::string label = Format("[%u] %s", p, Visibility(param.ShaderVisibility));
			switch (param.ParameterType) {
				case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: TableItems(model, s, param, arg, label, tree); break;
				case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: {
					tree.Parent(Format("%s b%u space%u", label.c_str(), param.Constants.ShaderRegister, param.Constants.RegisterSpace),
						arg.kind == RootArgument::Kind::Constants ? Format("%u values", param.Constants.Num32BitValues) : "not set");
					for (size_t i = 0; i < arg.constants.size() && i < 64; ++i) {
						float asFloat;
						memcpy(&asFloat, &arg.constants[i], 4);
						tree.Item(Format("[%zu]", i), Format("%u " DOT " 0x%08X " DOT " %g", arg.constants[i], arg.constants[i], asFloat));
					}
					tree.EndParent();
					break;
				}
				default: {
					const char* kind = param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ? "CBV" : param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ? "SRV" : "UAV";
					const char* letter = param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ? "b" : param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ? "t" : "u";
					const std::string where = Format("%s %s %s%u space%u", label.c_str(), kind, letter, param.Descriptor.ShaderRegister, param.Descriptor.RegisterSpace);
					if (arg.kind == RootArgument::Kind::None) tree.Item(where, "not set");
					else {
						const u64 resource = AddressId(arg.value);
						tree.Item(where, Format("%s +%llu", model.NameOf(resource).c_str(), (unsigned long long)AddressOffset(arg.value)), resource);
					}
				}
			}
		}
		if (root.NumStaticSamplers) {
			tree.Parent("Static samplers", Format("%u", root.NumStaticSamplers));
			for (UINT i = 0; i < root.NumStaticSamplers; ++i) {
				const D3D12_STATIC_SAMPLER_DESC& sampler = root.pStaticSamplers[i];
				tree.Item(Format("s%u space%u", sampler.ShaderRegister, sampler.RegisterSpace),
					Format("%s " DOT " %s/%s/%s", Name(EnumName(sampler.Filter)), Name(EnumName(sampler.AddressU)), Name(EnumName(sampler.AddressV)), Name(EnumName(sampler.AddressW))));
			}
			tree.EndParent();
		}
		tree.EndGroup();
		deserializer->Release();
	}

	static std::string ShaderText(const D3D12_SHADER_BYTECODE& code) {
		if (!code.pShaderBytecode || code.BytecodeLength < 4) return "none";
		const bool dxbc = memcmp(code.pShaderBytecode, "DXBC", 4) == 0;
		// A container holding a DXIL part is DXIL.
		bool dxil = false;
		if (dxbc && code.BytecodeLength > 32) {
			const u8* bytes = (const u8*)code.pShaderBytecode;
			u32 parts = 0;
			memcpy(&parts, bytes + 28, 4);
			for (u32 i = 0; i < parts && 32 + i * 4 + 4 <= code.BytecodeLength; ++i) {
				u32 offset = 0;
				memcpy(&offset, bytes + 32 + i * 4, 4);
				if (offset + 4 <= code.BytecodeLength && memcmp(bytes + offset, "DXIL", 4) == 0) dxil = true;
			}
		}
		return Format("%s " DOT " %s", dxil ? "DXIL" : dxbc ? "DXBC" : "bytecode", Bytes(code.BytecodeLength).c_str());
	}

	static void InputAssembler(const Model& model, const Snapshot& s, const PipelineCreate* pso, Tree& tree) {
		tree.Group("Input Assembler", Name(EnumName(s.topology)));
		tree.Item("Topology", Name(EnumName(s.topology)));
		if (s.indexed) {
			const u64 id = AddressId(s.indexAddress);
			tree.Item("Index buffer", Format("%s +%llu " DOT " %s " DOT " %s", model.NameOf(id).c_str(), (unsigned long long)AddressOffset(s.indexAddress), Name(EnumName(s.indexFormat)),
				Bytes(s.indexSize).c_str()), id);
		}
		std::vector<bool> usedSlots(32, false);
		if (pso)
			for (UINT i = 0; i < pso->inputLayout.NumElements; ++i)
				if (pso->inputLayout.pInputElementDescs[i].InputSlot < 32) usedSlots[pso->inputLayout.pInputElementDescs[i].InputSlot] = true;
		for (u32 slot = 0; slot < 32; ++slot) {
			const VertexBufferBinding& vb = s.vertexBuffers[slot];
			if (!usedSlots[slot] && !vb.address) continue;
			const u64 id = AddressId(vb.address);
			tree.Item(Format("VB %u", slot),
				vb.address ? Format("%s +%llu " DOT " %u B stride " DOT " %s", model.NameOf(id).c_str(), (unsigned long long)AddressOffset(vb.address), vb.stride, Bytes(vb.size).c_str()) : "not set", id);
		}
		if (pso && pso->inputLayout.NumElements) {
			tree.Parent("Input layout", Format("%u elements", pso->inputLayout.NumElements));
			for (UINT i = 0; i < pso->inputLayout.NumElements; ++i) {
				const D3D12_INPUT_ELEMENT_DESC& e = pso->inputLayout.pInputElementDescs[i];
				tree.Item(Format("%s%u", e.SemanticName ? e.SemanticName : "?", e.SemanticIndex),
					Format("%s " DOT " slot %u " DOT " +%u%s", Name(EnumName(e.Format)), e.InputSlot, e.AlignedByteOffset,
						e.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ? " " DOT " per instance" : ""));
			}
			tree.EndParent();
		}
		tree.EndGroup();
	}

	static void Shaders(const PipelineCreate& pso, Tree& tree) {
		const std::pair<const char*, const D3D12_SHADER_BYTECODE*> stages[] = {
			{"as", &pso.as}, {"ms", &pso.ms}, {"vs", &pso.vs}, {"hs", &pso.hs}, {"ds", &pso.ds}, {"gs", &pso.gs}, {"ps", &pso.ps}};
		const char* names[] = {"Amplification Shader", "Mesh Shader", "Vertex Shader", "Hull Shader", "Domain Shader", "Geometry Shader", "Pixel Shader"};
		for (size_t i = 0; i < 7; ++i) {
			if (!stages[i].second->pShaderBytecode || !stages[i].second->BytecodeLength) continue;
			tree.Group(names[i], ShaderText(*stages[i].second));
			tree.Item("Shader", ShaderText(*stages[i].second), 0, stages[i].first);
			tree.EndGroup();
		}
	}

	static void Rasterizer(const Snapshot& s, const PipelineCreate& pso, Tree& tree) {
		const D3D12_RASTERIZER_DESC2& r = pso.rasterizer;
		tree.Group("Rasterizer", Format("%s, cull %s", r.FillMode == D3D12_FILL_MODE_WIREFRAME ? "wireframe" : "solid", Name(EnumName(r.CullMode))));
		tree.Item("Fill mode", Name(EnumName(r.FillMode)));
		tree.Item("Cull mode", Name(EnumName(r.CullMode)));
		tree.Item("Front counter-clockwise", r.FrontCounterClockwise ? "yes" : "no");
		tree.Item("Depth bias", Format("%g (clamp %g, slope %g)", r.DepthBias, r.DepthBiasClamp, r.SlopeScaledDepthBias));
		tree.Item("Depth clip", r.DepthClipEnable ? "on" : "off");
		tree.Item("Conservative", Name(EnumName(r.ConservativeRaster)));
		for (size_t i = 0; i < s.viewports.size(); ++i) {
			const D3D12_VIEWPORT& v = s.viewports[i];
			tree.Item(Format("Viewport %zu", i), Format("%g, %g  %g x %g  depth %g..%g", v.TopLeftX, v.TopLeftY, v.Width, v.Height, v.MinDepth, v.MaxDepth));
		}
		for (size_t i = 0; i < s.scissors.size(); ++i) {
			const D3D12_RECT& rect = s.scissors[i];
			tree.Item(Format("Scissor %zu", i), Format("%ld, %ld  %ld x %ld", rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top));
		}
		tree.EndGroup();
	}

	static void OutputMerger(const Model& model, const Snapshot& s, const PipelineCreate* pso, Tree& tree) {
		tree.Group("Output Merger", Format("%zu target(s)%s", s.renderTargets.size(), s.depthStencil ? " + depth" : ""));
		for (size_t i = 0; i < s.renderTargets.size(); ++i) {
			DescriptorWrite view;
			if (DescriptorAt(model, s, DescriptorHeapId(s.renderTargets[i]), DescriptorIndex(s.renderTargets[i]), view)) tree.Item(Format("RTV %zu", i), ViewText(model, view), ViewResource(view));
			else tree.Item(Format("RTV %zu", i), "(empty)");
		}
		if (s.depthStencil) {
			DescriptorWrite view;
			if (DescriptorAt(model, s, DescriptorHeapId(s.depthStencil), DescriptorIndex(s.depthStencil), view)) tree.Item("DSV", ViewText(model, view), ViewResource(view));
		}
		if (pso) {
			const D3D12_DEPTH_STENCIL_DESC2& d = pso->depthStencil;
			tree.Item("Depth", d.DepthEnable ? Format("%s, write %s", Name(EnumName(d.DepthFunc)), d.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL ? "on" : "off") : "off");
			tree.Item("Stencil", d.StencilEnable ? Format("ref %u, front %s", s.stencilRef, Name(EnumName(d.FrontFace.StencilFunc))) : "off");
			for (UINT i = 0; i < std::max<UINT>(1, pso->renderTargets.NumRenderTargets) && i < 8; ++i) {
				const D3D12_RENDER_TARGET_BLEND_DESC& b = pso->blend.RenderTarget[pso->blend.IndependentBlendEnable ? i : 0];
				const std::string mask = EnumName((D3D12_COLOR_WRITE_ENABLE)b.RenderTargetWriteMask);
				tree.Item(Format("Blend %u", i), b.BlendEnable ? Format("%s %s %s, alpha %s %s %s, mask %s", Name(EnumName(b.SrcBlend)), Name(EnumName(b.BlendOp)), Name(EnumName(b.DestBlend)),
					Name(EnumName(b.SrcBlendAlpha)), Name(EnumName(b.BlendOpAlpha)), Name(EnumName(b.DestBlendAlpha)), mask.c_str()) : Format("off, mask %s", mask.c_str()));
			}
			tree.Item("Blend factor", Format("%g, %g, %g, %g", s.blendFactor[0], s.blendFactor[1], s.blendFactor[2], s.blendFactor[3]));
		}
		tree.EndGroup();
	}

	void D3D12Replay::State(u32 eid, JsonWriter& out) {
		const Snapshot s = Simulate(model_, eid);
		if (!s.event) Fail("no event %u", eid);
		const bool compute = s.command != Cmd::Count && ClassOf(s.command) == CmdClass::Dispatch;
		out.Uint("eid", eid);
		out.String("command", s.command == Cmd::Count ? s.event->name : CmdName(s.command));
		out.String("kind", EventKindName(s.event->kind));
		out.String("pipeline", model_.NameOf(s.pipeline));
		out.Uint("pipelineId", s.pipeline);
		out.BeginArray("groups");
		Tree tree(out);
		PipelineCreate pso = {};
		const ObjectEntry* psoEntry = model_.Find(s.pipeline);
		const bool havePso = psoEntry && model_.DecodeModel(psoEntry->record, psoEntry->recordSize, pso);
		if (compute) {
			tree.Group("Compute Shader", havePso ? ShaderText(pso.cs) : "no pipeline");
			if (havePso) tree.Item("Shader", ShaderText(pso.cs), 0, "cs");
			tree.EndGroup();
			RootSignatureGroup(model_, s, s.computeRoot, s.compute, tree);
		}
		else {
			InputAssembler(model_, s, havePso ? &pso : nullptr, tree);
			if (havePso) {
				Shaders(pso, tree);
				Rasterizer(s, pso, tree);
			}
			OutputMerger(model_, s, havePso ? &pso : nullptr, tree);
			RootSignatureGroup(model_, s, s.graphicsRoot, s.graphics, tree);
		}
		tree.Group("Descriptor Heaps", Format("%s%s%s", model_.NameOf(s.heaps[0]).c_str(), s.heaps[1] ? ", " : "", s.heaps[1] ? model_.NameOf(s.heaps[1]).c_str() : ""));
		for (u64 heap : s.heaps) {
			if (!heap) continue;
			const ObjectEntry* entry = model_.Find(heap);
			DescriptorHeapCreate record = {};
			if (entry && model_.DecodeModel(entry->record, entry->recordSize, record))
				tree.Item(model_.NameOf(heap), Format("%s " DOT " %u descriptors", Name(EnumName(record.desc.Type)), record.desc.NumDescriptors));
		}
		tree.EndGroup();
		out.EndArray();
	}
}
