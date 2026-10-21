// capfile.h - the .flash container (docs/22 "The capture file"): a 64-byte header, then records {u32 type, u32 flags,
// u64 size, payload}. CaptureWriter streams records to disk; CaptureReader maps a file and indexes its records without
// reading their payloads, so a capture of gigabytes opens at once.
#pragma once

#include "bytes.h"

namespace fb
{
	constexpr char kCaptureMagic[8] = {'F', 'L', 'A', 'S', 'H', 'C', 'A', 'P'};
	// Bumped whenever a record's layout changes; a reader refuses other versions instead of misreading them.
	constexpr u32 kCaptureVersion = 1;

	enum class Api : u32 { None = 0, D3D12 = 1, Vulkan = 2 };
	const char* ApiName(Api api);

	struct FileHeader
	{
		char magic[8];
		u32 version;
		u32 api;
		u64 flags;
		u64 reserved[5];
	};
	static_assert(sizeof(FileHeader) == 64, "the header is 64 bytes");

	struct RecordHeader
	{
		u32 type;
		u32 flags;
		u64 size;
	};
	static_assert(sizeof(RecordHeader) == 16, "a record header is 16 bytes");

	// Records every API shares; D3D12's are 100..999 (d3d12_records.h), Vulkan's 1000..1999 (vk_records.h).
	enum : u32
	{
		kRecordMeta = 1,          // JSON: program, frame, devices, comment, warnings
		kRecordFrameBegin = 2,    // what follows is the captured frame
		kRecordFrameEnd = 3,
		kRecordWarning = 4,       // text: something the capture could not record faithfully
		kRecordAnnotation = 5,    // key and value (Gpu::Capture::Annotate)
		kRecordThumbnail = 6,     // u32 width, u32 height, RGBA8 rows
	};

	class CaptureWriter
	{
	public:
		~CaptureWriter();
		bool Open(const std::wstring& path, Api api, std::string& error);
		void Record(u32 type, const void* data, u64 size, u32 flags = 0);
		void Record(u32 type, const ByteWriter& payload, u32 flags = 0) { Record(type, payload.Data(), payload.Size(), flags); }
		// A record whose payload comes in pieces: the header, then exactly `size` bytes through Append.
		void BeginRecord(u32 type, u64 size, u32 flags = 0);
		void Append(const void* data, u64 size);
		bool Close(std::string& error);
		bool IsOpen() const { return file_ != nullptr; }
		u64 Written() const { return written_; }
		bool Failed() const { return failed_; }

	private:
		void Flush();

		void* file_ = nullptr;
		std::vector<u8> buffer_;
		u64 written_ = 0;
		bool failed_ = false;
	};

	struct RecordRef
	{
		u32 type;
		u32 flags;
		u64 offset;   // of the payload, from the file's start
		u64 size;
	};

	class CaptureReader
	{
	public:
		~CaptureReader();
		bool Open(const std::wstring& path, std::string& error);
		Api GetApi() const { return api_; }
		u32 Version() const { return version_; }
		const std::vector<RecordRef>& Records() const { return records_; }
		const u8* Payload(const RecordRef& record) const { return base_ + record.offset; }
		ByteReader Reader(const RecordRef& record) const { return ByteReader(base_ + record.offset, (size_t)record.size); }
		u64 FileSize() const { return size_; }
		// The metadata record's JSON text ("{}" when there is none).
		std::string Meta() const;

	private:
		void* file_ = nullptr;
		void* mapping_ = nullptr;
		const u8* base_ = nullptr;
		u64 size_ = 0;
		Api api_ = Api::None;
		u32 version_ = 0;
		std::vector<RecordRef> records_;
	};
}
