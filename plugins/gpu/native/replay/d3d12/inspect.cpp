// Reading resources after an event (docs/22 "Replay"): a subresource or a buffer range copied into readback memory,
// textures decoded to RGBA32F for the browser's viewer, the exact texel of a pixel pick, and a draw's vertex inputs
// for the mesh viewer.
#include "d3d12replay.h"
#include "texels.h"
#include "d3d12_names.h"
#include "formats.h"

#include <cmath>

namespace fb::replay::d3d12
{
	static ID3D12Resource* MakeReadback(ID3D12Device* device, u64 bytes) {
		D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_READBACK};
		D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, std::max<u64>(bytes, 256), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
		ID3D12Resource* buffer = nullptr;
		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&buffer)))
			Fail("not enough memory to read %llu bytes back", (unsigned long long)bytes);
		return buffer;
	}

	static bool Readable(D3D12_RESOURCE_STATES state) { return (state & D3D12_RESOURCE_STATE_COPY_SOURCE) != 0; }

	const Readback& D3D12Replay::Read(u32 eid, u64 id, u32 subresource) {
		if (last_.valid && last_.eid == eid && last_.id == id && last_.subresource == subresource) return last_;
		ReplayDevice& device = Device();
		ID3D12Resource* resource = device.Resource(id);
		if (!resource) Fail("resource %s is not in the replay", model_.NameOf(id).c_str());
		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		if (desc.SampleDesc.Count > 1) Fail("multisampled textures cannot be read yet");
		if (replayedTo_ != eid) {
			device.ReplayTo(eid);
			replayedTo_ = eid;
		}
		last_ = Readback();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT rows = 0;
		UINT64 rowBytes = 0, total = 0;
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
			footprint.Footprint = {DXGI_FORMAT_UNKNOWN, (UINT)desc.Width, 1, 1, (UINT)desc.Width};
			rows = 1;
			total = desc.Width;
		}
		else device.Device()->GetCopyableFootprints(&desc, subresource, 1, 0, &footprint, &rows, &rowBytes, &total);
		ID3D12Resource* readback = MakeReadback(device.Device(), total);
		ID3D12GraphicsCommandList* list = device.BeginUtility();
		const D3D12_RESOURCE_STATES state = device.StateOf(id, subresource);
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.Subresource = desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : subresource;
		barrier.Transition.StateBefore = state;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		const bool transition = !Readable(state);
		if (transition) list->ResourceBarrier(1, &barrier);
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) list->CopyBufferRegion(readback, 0, resource, 0, desc.Width);
		else {
			D3D12_TEXTURE_COPY_LOCATION dst = {};
			dst.pResource = readback;
			dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dst.PlacedFootprint = footprint;
			D3D12_TEXTURE_COPY_LOCATION src = {};
			src.pResource = resource;
			src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			src.SubresourceIndex = subresource;
			list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}
		if (transition) {
			std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
			list->ResourceBarrier(1, &barrier);
		}
		device.RunUtility();
		u8* mapped = nullptr;
		const D3D12_RANGE read = {0, (SIZE_T)total};
		if (FAILED(readback->Map(0, &read, (void**)&mapped))) {
			readback->Release();
			Fail("the readback could not be mapped");
		}
		last_.bytes.assign(mapped, mapped + total);
		const D3D12_RANGE none = {0, 0};
		readback->Unmap(0, &none);
		readback->Release();
		last_.eid = eid;
		last_.id = id;
		last_.subresource = subresource;
		last_.footprint = footprint.Footprint;
		last_.rows = rows;
		last_.valid = true;
		return last_;
	}

	void D3D12Replay::ReadBuffer(u32 eid, u64 id, u64 offset, u64 size, std::vector<u8>& out) {
		const Readback& whole = Read(eid, id, 0);
		if (offset >= whole.bytes.size()) {
			out.clear();
			return;
		}
		size = std::min<u64>(size, whole.bytes.size() - offset);
		out.assign(whole.bytes.begin() + (ptrdiff_t)offset, whole.bytes.begin() + (ptrdiff_t)(offset + size));
	}

	u32 D3D12Replay::Subresource(u64 id, const JsonValue& params, D3D12_RESOURCE_DESC& desc) {
		ID3D12Resource* resource = Device().Resource(id);
		if (!resource) Fail("resource %s is not in the replay", model_.NameOf(id).c_str());
		desc = resource->GetDesc();
		const u32 mip = (u32)params["mip"].Uint(0);
		const u32 slice = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 0 : (u32)params["slice"].Uint(0);
		const u32 plane = (u32)params["plane"].Uint(0);
		if (mip >= desc.MipLevels) Fail("the texture has %u mips", desc.MipLevels);
		const u32 slices = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
		if (slice >= slices) Fail("the texture has %u slices", slices);
		return mip + slice * desc.MipLevels + plane * desc.MipLevels * slices;
	}

	// The format to decode a subresource with: the asked view format, else the resource's (a plane's for depth-stencil).
	static DXGI_FORMAT ViewFormat(const JsonValue& params, DXGI_FORMAT resource, u32 plane) {
		const std::string asked = params["format"].Str();
		if (!asked.empty()) {
			for (u32 f = 1; f < 200; ++f) {
				const char* name = DxgiFormatName((DXGI_FORMAT)f);
				if (name && asked == name) return (DXGI_FORMAT)f;
			}
		}
		const DXGI_FORMAT planar = DxgiPlaneFormat(resource, plane);
		return planar != resource ? planar : DxgiDefaultView(resource);
	}

	void D3D12Replay::Texture(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const u64 id = params["id"].Uint(0);
		D3D12_RESOURCE_DESC desc;
		const u32 subresource = Subresource(id, params, desc);
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) Fail("%s is a buffer", model_.NameOf(id).c_str());
		const Readback& r = Read(eid, id, subresource);
		const DXGI_FORMAT format = ViewFormat(params, desc.Format, (u32)params["plane"].Uint(0));
		const FormatInfo* info = DxgiFormat(format);
		if (!info || info->codec == Codec::Unknown) Fail("format %s cannot be shown", DxgiFormatName(format) ? DxgiFormatName(format) : "?");
		if (info->codec == Codec::BC6H || info->codec == Codec::BC7) Fail("%s is decoded on the GPU, which lands later", info->name);
		const u32 width = r.footprint.Width, height = r.footprint.Height;
		const u32 z = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? std::min<u32>((u32)params["slice"].Uint(0), r.footprint.Depth - 1) : 0;
		const u32 maxSize = (u32)params["maxSize"].Uint(2048);
		ShowTexels(*info, r.bytes.data() + (size_t)z * r.footprint.RowPitch * r.rows, r.footprint.RowPitch, width, height, maxSize, out, binary);
		out.Uint("eid", eid);
		out.Uint("id", id);
		out.String("name", model_.NameOf(id));
		out.Uint("mips", desc.MipLevels);
		out.Uint("slices", desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? r.footprint.Depth : desc.DepthOrArraySize);
		out.String("format", DxgiFormatName(desc.Format) ? DxgiFormatName(desc.Format) : "?");
		out.String("view", info->name);
		out.Bool("srgb", info->component == Component::Srgb);
		// Linear values (float and sRGB formats, decoded) are encoded for display; UNORM ones are shown as stored.
		out.Bool("linear", info->component == Component::Float || info->component == Component::Srgb);
		out.Bool("depth", info->depth);
		out.Uint("channels", info->channels);
	}

	TexelValue D3D12Replay::PixelAt(u32 eid, const JsonValue& params, std::string& format) {
		const u64 id = params["id"].Uint(0);
		D3D12_RESOURCE_DESC desc;
		const u32 subresource = Subresource(id, params, desc);
		const Readback& r = Read(eid, id, subresource);
		const u32 x = (u32)params["x"].Uint(0), y = (u32)params["y"].Uint(0);
		if (x >= r.footprint.Width || y >= r.footprint.Height) Fail("(%u, %u) is outside the %u x %u texture", x, y, r.footprint.Width, r.footprint.Height);
		const DXGI_FORMAT view = ViewFormat(params, desc.Format, (u32)params["plane"].Uint(0));
		const FormatInfo* info = DxgiFormat(view);
		if (!info) Fail("format %s cannot be read", DxgiFormatName(view) ? DxgiFormatName(view) : "?");
		const u32 z = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? std::min<u32>((u32)params["slice"].Uint(0), r.footprint.Depth - 1) : 0;
		const u8* slice = r.bytes.data() + (size_t)z * r.footprint.RowPitch * r.rows;
		format = info->name;
		return ReadTexel(*info, slice, r.footprint.RowPitch, x, y);
	}

	void D3D12Replay::Pixel(const JsonValue& params, JsonWriter& out) {
		std::string format;
		const TexelValue texel = PixelAt((u32)params["eid"].Uint(0), params, format);
		out.Uint("x", params["x"].Uint(0));
		out.Uint("y", params["y"].Uint(0));
		out.String("format", format);
		WriteTexel(texel, out);
	}

	void D3D12Replay::Buffer(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const u64 id = params["id"].Uint(0);
		const u64 offset = params["offset"].Uint(0);
		const u64 size = std::min<u64>(params["size"].Uint(65536), 16ull * 1024 * 1024);
		ReadBuffer(eid, id, offset, size, binary);
		out.Uint("eid", eid);
		out.Uint("id", id);
		out.Uint("offset", offset);
		out.Uint("size", binary.size());
	}

	void D3D12Replay::Mesh(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const Snapshot s = Simulate(model_, eid);
		if (!s.event || (s.command != Cmd::DrawInstanced && s.command != Cmd::DrawIndexedInstanced)) Fail("event %u is not a draw with vertex input", eid);
		const CommandRef* command = model_.CommandOf(*s.event);
		ModelMap map(model_);
		DrawIndexedArgs indexedArgs = {};
		DrawArgs drawArgs = {};
		if (s.command == Cmd::DrawIndexedInstanced) model_.DecodeCommand(*command, &indexedArgs, &map);
		else model_.DecodeCommand(*command, &drawArgs, &map);
		PipelineCreate pso = {};
		const ObjectEntry* entry = model_.Find(s.pipeline);
		if (!entry || !model_.DecodeModel(entry->record, entry->recordSize, pso)) Fail("the draw has no pipeline state");

		std::vector<u32> indices;
		const bool indexed = s.command == Cmd::DrawIndexedInstanced;
		const u32 count = std::min<u32>(indexed ? indexedArgs.indexCount : drawArgs.vertexCount, 1u << 20);
		if (indexed) {
			const u32 indexBytes = s.indexFormat == DXGI_FORMAT_R32_UINT ? 4 : 2;
			std::vector<u8> raw;
			ReadBuffer(eid, AddressId(s.indexAddress), AddressOffset(s.indexAddress) + (u64)indexedArgs.startIndex * indexBytes, (u64)count * indexBytes, raw);
			for (size_t i = 0; i + indexBytes <= raw.size(); i += indexBytes) {
				u32 index = 0;
				memcpy(&index, raw.data() + i, indexBytes);
				indices.push_back((u32)((i64)index + indexedArgs.baseVertex));
			}
		}
		else
			for (u32 i = 0; i < count; ++i) indices.push_back(drawArgs.startVertex + i);
		u32 low = UINT32_MAX, high = 0;
		for (u32 index : indices) {
			low = std::min(low, index);
			high = std::max(high, index);
		}
		if (indices.empty()) low = high = 0;
		const u32 vertices = std::min<u32>(high - low + 1, 1u << 16);
		const u32 instance = indexed ? indexedArgs.startInstance : drawArgs.startInstance;

		out.Uint("eid", eid);
		out.String("topology", EnumName(s.topology) ? EnumName(s.topology) : "?");
		out.Uint("first", low);
		out.Uint("count", vertices);
		out.BeginArray("attributes");
		std::vector<float> data;
		for (UINT e = 0; e < pso.inputLayout.NumElements; ++e) {
			const D3D12_INPUT_ELEMENT_DESC& element = pso.inputLayout.pInputElementDescs[e];
			const FormatInfo* info = DxgiFormat(element.Format);
			const VertexBufferBinding& vb = element.InputSlot < s.vertexBuffers.size() ? s.vertexBuffers[element.InputSlot] : VertexBufferBinding();
			out.BeginObject();
			out.String("name", element.SemanticName ? element.SemanticName : "?");
			out.Uint("index", element.SemanticIndex);
			out.String("format", info ? info->name : "?");
			out.Uint("components", info ? info->channels : 0);
			out.Uint("slot", element.InputSlot);
			out.EndObject();
			std::vector<u8> raw;
			const bool perInstance = element.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
			if (vb.address && info && !IsBlock(info->codec)) {
				const u64 first = perInstance ? instance : low;
				ReadBuffer(eid, AddressId(vb.address), AddressOffset(vb.address) + first * vb.stride, (u64)(perInstance ? 1 : vertices) * vb.stride + info->bytes, raw);
			}
			for (u32 v = 0; v < vertices; ++v) {
				float value[4] = {0, 0, 0, 1};
				const size_t at = (size_t)(perInstance ? 0 : v) * vb.stride + element.AlignedByteOffset;
				if (info && at + info->bytes <= raw.size()) DecodeRows(*info, raw.data() + at, 0, 1, 1, value);
				data.insert(data.end(), value, value + 4);
			}
		}
		out.EndArray();
		out.BeginArray("indices");
		for (u32 index : indices) out.Uint(index - low);
		out.EndArray();
		binary.resize(data.size() * sizeof(float));
		memcpy(binary.data(), data.data(), binary.size());
	}
}
