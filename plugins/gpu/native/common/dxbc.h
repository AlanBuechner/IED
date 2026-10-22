// dxbc.h - DXBC shader containers (docs/22 "Shader debugging"): the chunks of a compiled Shader Model 4/5 shader, the
// container's checksum (MD5 with DXBC's own padding, which the runtime checks), and the instrumentation that makes a
// shader log the values the chosen invocation computes - the D3D12 side of instrumented replay. The tokens are the
// D3D10/11 tokenized program format; Shader Model 5.1 resources are declared with an id, a register range and a space.
#pragma once

#include "fb_base.h"

#include <string>
#include <vector>

namespace fb
{
	struct DxbcChunk
	{
		u32 fourcc = 0;
		std::vector<u8> data;
	};

	// The chunks of a container; false when `bytes` is not one.
	bool ReadDxbc(const u8* bytes, size_t size, std::vector<DxbcChunk>& chunks);
	// A container of `chunks`, its checksum computed.
	std::vector<u8> WriteDxbc(const std::vector<DxbcChunk>& chunks);
	// The checksum DXBC keeps at bytes 4 to 20 of a container of `size` bytes.
	void DxbcChecksum(const u8* bytes, size_t size, u32 checksum[4]);
	u32 FourCC(const char* text);

	// A logged value: the instruction that made it, by its byte offset in the original program (D3DDisassemble prints
	// it with D3D_DISASM_ENABLE_INSTRUCTION_OFFSET; it names the listing line), the register it wrote or the store's
	// destination, the components and how they read.
	struct DxbcSite
	{
		u32 offset = 0;
		std::string name;          // "r3.xy", "o0.xyzw", "U1"
		u32 mask = 0;              // the components written
		u32 type = 0;              // 0 float, 1 int, 2 uint, 3 bool, 4 raw bits
		bool store = false;        // a store's value, not a register write
	};

	struct DxbcTarget
	{
		u32 x = 0, y = 0;          // a pixel shader's pixel
		u32 vertex = 0, instance = 0;
		u32 thread[3] = {};
		u32 space = 900;           // the trace buffer's register space; its register is 0
	};

	// The trace buffer's layout, in bytes: the first word counts the bytes handed out (from kDxbcTraceFirst); bytes 4 to
	// 24 are the scratch record; then a region per chosen invocation of kDxbcRecords records of 20 bytes: the site's
	// index + 1, then the register's four components.
	constexpr u32 kDxbcTraceFirst = 64;
	constexpr u32 kDxbcRecordBytes = 20;
	constexpr u32 kDxbcRecords = 16384;
	constexpr u32 kDxbcRegions = 8;
	constexpr u32 kDxbcTraceBytes = kDxbcTraceFirst + kDxbcRegions * kDxbcRecords * kDxbcRecordBytes;

	struct DxbcInstrumented
	{
		std::vector<u8> bytes;     // the container, without the embedded root signature (RTS0)
		std::vector<DxbcSite> sites;
		std::string problem;
	};

	// Rewrites a pixel, vertex or compute shader (Shader Model 5.1) to log the target's values into a raw UAV at
	// register 0 of `target.space`; false with `out.problem` when it cannot.
	bool InstrumentDxbc(const u8* bytes, size_t size, const DxbcTarget& target, DxbcInstrumented& out);
	// A logged record's components as the site reads them: "0.5, 1, 2".
	std::string DxbcValueText(const DxbcSite& site, const u32 values[4]);

	// An element of a shader's input or output signature: its semantic, system value (D3D_NAME), component type
	// (1 uint, 2 int, 3 float), register and components.
	struct DxbcElement
	{
		std::string name;
		u32 index = 0, system = 0, component = 0, reg = 0, mask = 0;
	};
	// The input ('I': ISGN or ISG1) or output ('O': OSGN or OSG1) signature of a DXBC or DXIL container; false when it
	// has none.
	bool ReadDxbcSignature(const u8* bytes, size_t size, char which, std::vector<DxbcElement>& out);
}
