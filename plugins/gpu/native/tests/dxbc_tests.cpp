// DXBC containers and the shader debugger's instrumentation (common/dxbc.cpp), against shaders Windows' own compiler
// (d3dcompiler_47.dll) builds: the checksum is the compiler's, a container reads and writes back unchanged, and an
// instrumented shader still disassembles, with the trace's UAV, the selection's inputs and a record per value.
#include "testing.h"

#include "dxbc.h"

#include <d3dcompiler.h>
#include <windows.h>

using namespace fb;

namespace
{
	const char* kSource = R"(
		RWByteAddressBuffer other : register(u2);
		cbuffer C : register(b0) { float4 tint; uint count; };
		Texture2D<float4> image : register(t0);
		SamplerState bilinear : register(s0);
		struct V { float4 position : SV_Position; float2 uv : TEXCOORD; };
		V VS(float3 p : POSITION, float2 uv : TEXCOORD) {
			V v;
			v.position = float4(p * 0.5, 1);
			v.uv = uv;
			return v;
		}
		float4 PS(V v) : SV_Target {
			float4 c = image.Sample(bilinear, v.uv) * tint;
			[loop] for (uint i = 0; i < count; ++i) c.rgb *= 0.5;
			other.Store(0, asuint(c.r));
			return c;
		}
		float4 Unpositioned(float2 uv : TEXCOORD) : SV_Target { return float4(uv, 0, 1); }
		float Depth(V v, out float4 color : SV_Target) : SV_Depth { color = v.uv.xyxy; return v.position.z * 0.5; }
		[numthreads(8, 8, 1)]
		void CS(uint3 id : SV_DispatchThreadID) { if (id.y == 1) other.Store(id.x * 4, id.x + id.z); }
	)";

	struct Compiled
	{
		std::vector<u8> bytes;
		std::string errors;
	};

	HMODULE Compiler() {
		static HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
		return module;
	}

	Compiled Compile(const char* entry, const char* profile) {
		Compiled out;
		auto compile = Compiler() ? (pD3DCompile)GetProcAddress(Compiler(), "D3DCompile") : nullptr;
		if (!compile) return out;
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		if (SUCCEEDED(compile(kSource, strlen(kSource), "dxbc_tests.hlsl", nullptr, nullptr, entry, profile, 0, 0, &code, &errors)))
			out.bytes.assign((const u8*)code->GetBufferPointer(), (const u8*)code->GetBufferPointer() + code->GetBufferSize());
		if (errors) out.errors.assign((const char*)errors->GetBufferPointer(), errors->GetBufferSize());
		if (code) code->Release();
		if (errors) errors->Release();
		return out;
	}

	std::string Disassemble(const std::vector<u8>& bytes) {
		auto disassemble = Compiler() ? (pD3DDisassemble)GetProcAddress(Compiler(), "D3DDisassemble") : nullptr;
		ID3DBlob* text = nullptr;
		if (!disassemble || FAILED(disassemble(bytes.data(), bytes.size(), 0, nullptr, &text))) return "";
		std::string out((const char*)text->GetBufferPointer(), text->GetBufferSize());
		text->Release();
		return out;
	}

	bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

	size_t Count(const std::string& text, const char* part) {
		size_t count = 0;
		for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++count;
		return count;
	}
}

TEST(dxbc_checksum_and_chunks_are_the_compilers) {
	const char* shaders[][2] = {{"VS", "vs_5_1"}, {"PS", "ps_5_1"}, {"CS", "cs_5_1"}, {"Depth", "ps_5_1"}, {"PS", "ps_5_0"}};
	for (const auto& shader : shaders) {
		const Compiled compiled = Compile(shader[0], shader[1]);
		CHECK(!compiled.bytes.empty());
		if (compiled.bytes.empty()) {
			printf("  %s\n", compiled.errors.c_str());
			continue;
		}
		u32 checksum[4];
		DxbcChecksum(compiled.bytes.data(), compiled.bytes.size(), checksum);
		CHECK(memcmp(checksum, compiled.bytes.data() + 4, 16) == 0);
		std::vector<DxbcChunk> chunks;
		CHECK(ReadDxbc(compiled.bytes.data(), compiled.bytes.size(), chunks));
		CHECK(WriteDxbc(chunks) == compiled.bytes);
	}
	// Every padding case: the last block's remainder below 56 bytes and at or above.
	std::vector<u8> made;
	for (u32 extra = 0; extra < 64; ++extra) {
		std::vector<DxbcChunk> chunks = {{FourCC("TEST"), std::vector<u8>(extra * 4, (u8)extra)}};
		made = WriteDxbc(chunks);
		std::vector<DxbcChunk> back;
		CHECK(ReadDxbc(made.data(), made.size(), back) && back.size() == 1 && back[0].data == chunks[0].data);
	}
	std::vector<DxbcChunk> none;
	CHECK(!ReadDxbc(made.data(), 16, none));
}

TEST(dxbc_instruments_pixel_vertex_and_compute_shaders) {
	DxbcTarget target;
	target.x = 5;
	target.y = 9;
	target.vertex = 3;
	target.instance = 1;
	target.thread[0] = 2;
	target.thread[1] = 1;

	DxbcInstrumented pixel;
	const Compiled ps = Compile("PS", "ps_5_1");
	CHECK(InstrumentDxbc(ps.bytes.data(), ps.bytes.size(), target, pixel));
	const std::string psText = Disassemble(pixel.bytes);
	CHECK(Has(psText, "dcl_uav_raw U1[0:0], space=900"));
	CHECK(Has(psText, "dcl_input_ps_siv linear noperspective v0.xy, position"));
	CHECK(Has(psText, "ftou r") && Has(psText, "l(5, 9, 0, 0)"));
	CHECK(Has(psText, "imm_atomic_iadd"));
	CHECK(Has(psText, "mov o0.w, r") && Has(psText, "mov o0.xyz, r"));      // the target written through a spare temp
	CHECK(Count(psText, ".w, l(20), r") == pixel.sites.size());     // each record's address
	bool sampled = false, stored = false;
	for (const DxbcSite& site : pixel.sites) {
		sampled |= site.type == 0 && site.mask == 0xF && !site.store;
		stored |= site.store && site.name == "U0" && site.mask == 1;
	}
	CHECK(sampled && stored);
	std::vector<DxbcChunk> chunks;
	CHECK(ReadDxbc(pixel.bytes.data(), pixel.bytes.size(), chunks));
	u32 checksum[4];
	DxbcChecksum(pixel.bytes.data(), pixel.bytes.size(), checksum);
	CHECK(memcmp(checksum, pixel.bytes.data() + 4, 16) == 0);

	// The vertex shader takes neither SV_VertexID nor SV_InstanceID: both are added to its signature.
	DxbcInstrumented vertex;
	const Compiled vs = Compile("VS", "vs_5_1");
	CHECK(InstrumentDxbc(vs.bytes.data(), vs.bytes.size(), target, vertex));
	const std::string vsText = Disassemble(vertex.bytes);
	CHECK(Has(vsText, "vertex_id") && Has(vsText, "instance_id"));
	CHECK(Has(vsText, "SV_VertexID") && Has(vsText, "SV_InstanceID"));
	CHECK(Has(vsText, "l(3)") && Has(vsText, "l(1)"));
	CHECK(Has(vsText, "mov o0.") && Has(vsText, "mov o1.xy, r"));
	CHECK(Count(vsText, ".w, l(20), r") == vertex.sites.size());

	DxbcInstrumented compute;
	const Compiled cs = Compile("CS", "cs_5_1");
	CHECK(InstrumentDxbc(cs.bytes.data(), cs.bytes.size(), target, compute));
	const std::string csText = Disassemble(compute.bytes);
	CHECK(Has(csText, "dcl_input vThreadID.xyz"));
	CHECK(Has(csText, "vThreadID.xyzx, l(2, 1, 0, 0)"));
	CHECK(Count(csText, ".w, l(20), r") == compute.sites.size());

	// A depth output is written through a spare temp too.
	DxbcInstrumented depth;
	const Compiled ds = Compile("Depth", "ps_5_1");
	CHECK(InstrumentDxbc(ds.bytes.data(), ds.bytes.size(), target, depth));
	const std::string depthText = Disassemble(depth.bytes);
	CHECK(Has(depthText, "mov oDepth, r"));
	bool named = false;
	for (const DxbcSite& site : depth.sites) named |= site.name == "oDepth" && site.mask == 1;
	CHECK(named);
}

TEST(dxbc_says_what_it_cannot_instrument) {
	DxbcTarget target;
	DxbcInstrumented out;
	const Compiled old = Compile("PS", "ps_5_0");
	CHECK(!InstrumentDxbc(old.bytes.data(), old.bytes.size(), target, out) && Has(out.problem, "5.0"));
	const Compiled unpositioned = Compile("Unpositioned", "ps_5_1");
	CHECK(!InstrumentDxbc(unpositioned.bytes.data(), unpositioned.bytes.size(), target, out) && Has(out.problem, "SV_Position"));
	const u8 junk[40] = {'D', 'X', 'B', 'C'};
	CHECK(!InstrumentDxbc(junk, sizeof(junk), target, out) && !out.problem.empty());
}

TEST(dxbc_values_read_as_their_sites_say) {
	const u32 values[4] = {0x3f800000, 0xFFFFFFFF, 7, 0x7fc00000};
	DxbcSite site;
	site.mask = 0xF;
	CHECK(DxbcValueText(site, values) == "1, NaN, 9.80909e-45, NaN");
	site.type = 1;
	CHECK(DxbcValueText(site, values) == "1065353216, -1, 7, 2143289344");
	site.type = 3;
	site.mask = 0x6;
	CHECK(DxbcValueText(site, values) == "true, true");
	site.type = 4;
	site.mask = 0xF;
	CHECK(DxbcValueText(site, values) == "1, 0xffffffff, 7, 0x7fc00000");
}
