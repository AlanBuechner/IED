#include "capfile.h"

#include <windows.h>

namespace fb
{
	static constexpr size_t kWriteBuffer = 4 * 1024 * 1024;

	const char* ApiName(Api api) {
		switch (api) {
			case Api::D3D12: return "D3D12";
			case Api::Vulkan: return "Vulkan";
			default: return "none";
		}
	}

	CaptureWriter::~CaptureWriter() {
		std::string ignored;
		Close(ignored);
	}

	bool CaptureWriter::Open(const std::wstring& path, Api api, std::string& error) {
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			error = Format("cannot create %s (error %lu)", Narrow(path).c_str(), GetLastError());
			return false;
		}
		file_ = file;
		failed_ = false;
		written_ = 0;
		buffer_.reserve(kWriteBuffer);
		FileHeader header = {};
		memcpy(header.magic, kCaptureMagic, 8);
		header.version = kCaptureVersion;
		header.api = (u32)api;
		Append(&header, sizeof(header));
		return true;
	}

	void CaptureWriter::Flush() {
		if (!file_ || buffer_.empty()) return;
		const u8* at = buffer_.data();
		size_t left = buffer_.size();
		while (left && !failed_) {
			DWORD wrote = 0;
			const DWORD chunk = left > 0x40000000 ? 0x40000000 : (DWORD)left;
			if (!WriteFile((HANDLE)file_, at, chunk, &wrote, nullptr) || wrote == 0) failed_ = true;
			at += wrote;
			left -= wrote;
		}
		buffer_.clear();
	}

	void CaptureWriter::Append(const void* data, u64 size) {
		if (!file_ || !size) return;
		written_ += size;
		if (size >= kWriteBuffer) {
			Flush();
			const u8* at = (const u8*)data;
			while (size && !failed_) {
				DWORD wrote = 0;
				const DWORD chunk = size > 0x40000000 ? 0x40000000 : (DWORD)size;
				if (!WriteFile((HANDLE)file_, at, chunk, &wrote, nullptr) || wrote == 0) failed_ = true;
				at += wrote;
				size -= wrote;
			}
			return;
		}
		if (buffer_.size() + size > kWriteBuffer) Flush();
		buffer_.insert(buffer_.end(), (const u8*)data, (const u8*)data + size);
	}

	void CaptureWriter::BeginRecord(u32 type, u64 size, u32 flags) {
		RecordHeader header = {type, flags, size};
		Append(&header, sizeof(header));
	}

	void CaptureWriter::Record(u32 type, const void* data, u64 size, u32 flags) {
		BeginRecord(type, size, flags);
		Append(data, size);
	}

	bool CaptureWriter::Close(std::string& error) {
		if (!file_) return !failed_;
		Flush();
		CloseHandle((HANDLE)file_);
		file_ = nullptr;
		if (failed_) error = "writing the capture failed (disk full?)";
		return !failed_;
	}

	CaptureReader::~CaptureReader() {
		if (base_) UnmapViewOfFile(base_);
		if (mapping_) CloseHandle((HANDLE)mapping_);
		if (file_) CloseHandle((HANDLE)file_);
	}

	bool CaptureReader::Open(const std::wstring& path, std::string& error) {
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			error = Format("cannot open %s (error %lu)", Narrow(path).c_str(), GetLastError());
			return false;
		}
		file_ = file;
		LARGE_INTEGER size;
		GetFileSizeEx(file, &size);
		size_ = (u64)size.QuadPart;
		if (size_ < sizeof(FileHeader)) {
			error = "not a Flashbang capture (too short)";
			return false;
		}
		HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!mapping) {
			error = Format("cannot map the capture (error %lu)", GetLastError());
			return false;
		}
		mapping_ = mapping;
		base_ = (const u8*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
		if (!base_) {
			error = Format("cannot map the capture (error %lu)", GetLastError());
			return false;
		}
		FileHeader header;
		memcpy(&header, base_, sizeof(header));
		if (memcmp(header.magic, kCaptureMagic, 8) != 0) {
			error = "not a Flashbang capture";
			return false;
		}
		version_ = header.version;
		if (header.version != kCaptureVersion) {
			error = Format("capture format version %u; this replayer reads version %u", header.version, kCaptureVersion);
			return false;
		}
		api_ = (Api)header.api;
		u64 at = sizeof(FileHeader);
		while (at + sizeof(RecordHeader) <= size_) {
			RecordHeader record;
			memcpy(&record, base_ + at, sizeof(record));
			at += sizeof(RecordHeader);
			if (record.size > size_ - at) {
				error = Format("the capture is truncated (record %zu)", records_.size());
				return false;
			}
			records_.push_back({record.type, record.flags, at, record.size});
			at += record.size;
		}
		return true;
	}

	std::string CaptureReader::Meta() const {
		for (const RecordRef& record : records_)
			if (record.type == kRecordMeta) return std::string((const char*)Payload(record), (size_t)record.size);
		return "{}";
	}
}
