#include "testing.h"

#include "capfile.h"
#include "d3d12_names.h"
#include "d3d12_records.h"
#include "formats.h"
#include "json.h"
#include "png.h"

#include <windows.h>
#include <cmath>

using namespace fb;

namespace
{
	// Pointers as ids both ways: what a capture writes, a replay of the ids reads back.
	class IdentityWriteMap : public SerWriteMap
	{
	public:
		u64 Id(const void* object) override { return (u64)(uintptr_t)object; }
		void Address(u64 va, u64& id, u64& offset) override {
			id = va >> 32;
			offset = va & 0xFFFFFFFF;
		}
		void CpuDescriptor(u64 ptr, u64& heap, u32& index) override {
			heap = ptr >> 16;
			index = (u32)(ptr & 0xFFFF);
		}
	};

	class IdentityReadMap : public SerReadMap
	{
	public:
		u64 Address(u64 id, u64 offset) override { return id << 32 | offset; }
		u64 CpuDescriptor(u64 heap, u32 index) override { return heap << 16 | index; }
		std::string Name(u64 id) override { return Format("object%llu", (unsigned long long)id); }
	};
}

TEST(JsonRoundTrip) {
	JsonWriter json;
	json.BeginObject().String("name", "a \"quoted\"\nline").Int("n", -5).Double("nan", NAN).BeginArray("list").Int(1).Int(2).EndArray().EndObject();
	JsonValue value;
	std::string error;
	CHECK(JsonValue::Parse(json.Text().data(), json.Text().size(), value, error));
	CHECK(value["name"].Str() == "a \"quoted\"\nline");
	CHECK(value["n"].Int() == -5);
	CHECK(value["nan"].Str() == "NaN");
	CHECK(value["list"].Count() == 2 && value["list"].At(1).Int() == 2);
	CHECK(!JsonValue::Parse("{\"a\":", 5, value, error));
}

TEST(SerializeCommandRoundTrip) {
	using namespace fb::d3d12;
	IdentityWriteMap writeMap;
	ByteWriter out;
	D3D12_VERTEX_BUFFER_VIEW views[2] = {{0x0000000500000010ull, 256, 32}, {0x0000000700000000ull, 64, 16}};
	VertexBuffersArgs args = {1, 2, views};
	WriteCommand(out, &writeMap, Cmd::IASetVertexBuffers, args);
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = (ID3D12Resource*)(uintptr_t)42;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	BarrierArgs barriers = {1, &barrier};
	WriteCommand(out, &writeMap, Cmd::ResourceBarrier, barriers);

	ByteReader in(out.Data(), out.Size());
	IdentityReadMap readMap;
	Arena arena;
	CommandHeader header = in.Get<CommandHeader>();
	CHECK(header.command == (u16)Cmd::IASetVertexBuffers);
	ByteReader argsIn(in.View(header.size), header.size);
	VertexBuffersArgs read = {};
	JsonWriter json;
	json.BeginObject();
	Ser s(argsIn, arena, &readMap, &json);
	SerializeCommand(s, Cmd::IASetVertexBuffers, &read);
	json.EndObject();
	CHECK(!s.Failed() && argsIn.AtEnd());
	CHECK(read.startSlot == 1 && read.count == 2);
	CHECK(read.views[0].BufferLocation == views[0].BufferLocation && read.views[1].StrideInBytes == 16);
	CHECK(json.Text().find("\"$ref\":5") != std::string::npos);
	CHECK(json.Text().find("\"offset\":16") != std::string::npos);

	header = in.Get<CommandHeader>();
	CHECK(header.command == (u16)Cmd::ResourceBarrier);
	ByteReader barrierIn(in.View(header.size), header.size);
	BarrierArgs readBarriers = {};
	JsonWriter json2;
	json2.BeginObject();
	Ser s2(barrierIn, arena, &readMap, &json2);
	SerializeCommand(s2, Cmd::ResourceBarrier, &readBarriers);
	json2.EndObject();
	CHECK(readBarriers.count == 1 && (uintptr_t)readBarriers.barriers[0].Transition.pResource == 42);
	CHECK(json2.Text().find("PIXEL_SHADER_RESOURCE") != std::string::npos);
	CHECK(json2.Text().find("object42") != std::string::npos);
	CHECK(in.AtEnd());
}

TEST(SerializeCorruptDataFails) {
	using namespace fb::d3d12;
	u8 junk[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
	ByteReader in(junk, sizeof(junk));
	Arena arena;
	IdentityReadMap readMap;
	Ser s(in, arena, &readMap);
	ViewportsArgs args = {};
	SerializeCommand(s, Cmd::RSSetViewports, &args);
	CHECK(s.Failed());
	CHECK(args.count == 0);
}

TEST(CaptureFileRoundTrip) {
	wchar_t folder[MAX_PATH];
	GetTempPathW(MAX_PATH, folder);
	std::wstring path = std::wstring(folder) + L"flashbang-test.flash";
	std::string error;
	{
		CaptureWriter writer;
		CHECK(writer.Open(path, Api::D3D12, error));
		const char meta[] = "{\"program\":\"test\"}";
		writer.Record(kRecordMeta, meta, sizeof(meta) - 1);
		std::vector<u8> big(5 * 1024 * 1024, 7);
		writer.BeginRecord(d3d12::kInitialContents, big.size());
		writer.Append(big.data(), big.size());
		CHECK(writer.Close(error));
	}
	CaptureReader reader;
	CHECK(reader.Open(path, error));
	CHECK(reader.GetApi() == Api::D3D12);
	CHECK(reader.Records().size() == 2);
	CHECK(reader.Meta() == "{\"program\":\"test\"}");
	CHECK(reader.Records()[1].size == 5 * 1024 * 1024 && reader.Payload(reader.Records()[1])[12345] == 7);
	DeleteFileW(path.c_str());
}

TEST(FormatsDecode) {
	const FormatInfo* rgba8 = DxgiFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
	CHECK(rgba8 && rgba8->bytes == 4);
	u8 texel[4] = {255, 0, 128, 255};
	float out[4];
	DecodeRows(*rgba8, texel, 4, 1, 1, out);
	CHECK(out[0] == 1.0f && out[1] == 0.0f && std::fabs(out[2] - 128.0f / 255.0f) < 1e-6f && out[3] == 1.0f);

	const FormatInfo* bgra = DxgiFormat(DXGI_FORMAT_B8G8R8A8_UNORM);
	DecodeRows(*bgra, texel, 4, 1, 1, out);
	CHECK(std::fabs(out[0] - 128.0f / 255.0f) < 1e-6f && out[2] == 1.0f);

	const FormatInfo* half = DxgiFormat(DXGI_FORMAT_R16G16B16A16_FLOAT);
	u16 halves[4] = {FloatToHalf(1.5f), FloatToHalf(-2.0f), 0x7E00, FloatToHalf(0.25f)};
	DecodeRows(*half, (const u8*)halves, 8, 1, 1, out);
	CHECK(out[0] == 1.5f && out[1] == -2.0f && std::isnan(out[2]) && out[3] == 0.25f);

	const FormatInfo* r11 = DxgiFormat(DXGI_FORMAT_R11G11B10_FLOAT);
	u32 packed = (15u << 6) | ((15u << 6) << 11) | ((15u << 5) << 22);   // 1.0 in each channel
	DecodeRows(*r11, (const u8*)&packed, 4, 1, 1, out);
	CHECK(out[0] == 1.0f && out[1] == 1.0f && out[2] == 1.0f);

	// BC1: red to blue, every texel index 0 (red).
	u8 bc1[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
	float block[16 * 4];
	DecodeRows(*DxgiFormat(DXGI_FORMAT_BC1_UNORM), bc1, 8, 4, 4, block);
	CHECK(block[0] == 1.0f && block[1] == 0.0f && block[2] == 0.0f && block[3] == 1.0f);

	CHECK(std::string(EnumName(DXGI_FORMAT_R16G16B16A16_FLOAT)) == "R16G16B16A16_FLOAT");
	CHECK(std::string(EnumName(D3D12_RESOURCE_STATE_GENERIC_READ)) == "GENERIC_READ");
	CHECK(std::string(EnumName((D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_RENDER_TARGET))) == "RENDER_TARGET|COPY_DEST");
}

TEST(PngHasValidChunks) {
	u8 pixels[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
	std::vector<u8> png = EncodePng(2, 2, pixels, 8);
	CHECK(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
	// IHDR's CRC covers its type and data.
	const u32 crc = Crc32(png.data() + 12, 17);
	CHECK(png[29] == (u8)(crc >> 24) && png[32] == (u8)crc);
}

TEST(MarkerText) {
	const wchar_t wide[] = L"Shadow pass";
	CHECK(d3d12::MarkerText(0, wide, sizeof(wide)) == "Shadow pass");
	const char ansi[] = "GBuffer";
	CHECK(d3d12::MarkerText(1, ansi, sizeof(ansi)) == "GBuffer");
}
