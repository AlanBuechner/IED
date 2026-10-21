// bytes.h - little-endian byte streams: ByteWriter grows a buffer, ByteReader walks one with bounds checks (a short or
// corrupt record sets Failed() and reads zeros from then on, so a bad capture never reads outside its memory).
#pragma once

#include "fb_base.h"

namespace fb
{
	class ByteWriter
	{
	public:
		void Bytes(const void* data, size_t size) {
			if (!size) return;
			const size_t at = data_.size();
			data_.resize(at + size);
			memcpy(data_.data() + at, data, size);
		}
		template <class T> void Put(const T& value) { Bytes(&value, sizeof(T)); }
		// Reserves `size` bytes and returns where they start (valid until the next write).
		u8* Reserve(size_t size) {
			const size_t at = data_.size();
			data_.resize(at + size);
			return data_.data() + at;
		}
		void PatchU32(size_t at, u32 value) { memcpy(data_.data() + at, &value, 4); }
		void PatchU64(size_t at, u64 value) { memcpy(data_.data() + at, &value, 8); }
		size_t Size() const { return data_.size(); }
		const u8* Data() const { return data_.data(); }
		std::vector<u8>& Buffer() { return data_; }
		void Clear() { data_.clear(); }

	private:
		std::vector<u8> data_;
	};

	class ByteReader
	{
	public:
		ByteReader() = default;
		ByteReader(const void* data, size_t size) : at_((const u8*)data), end_((const u8*)data + size) {}

		bool Bytes(void* out, size_t size) {
			if (failed_ || (size_t)(end_ - at_) < size) {
				failed_ = true;
				if (out && size) memset(out, 0, size);
				return false;
			}
			if (size) memcpy(out, at_, size);
			at_ += size;
			return true;
		}
		template <class T> T Get() {
			T value;
			Bytes(&value, sizeof(T));
			return value;
		}
		// A view of the next `size` bytes, skipped; null when there are not that many.
		const u8* View(size_t size) {
			if (failed_ || (size_t)(end_ - at_) < size) {
				failed_ = true;
				return nullptr;
			}
			const u8* view = at_;
			at_ += size;
			return view;
		}
		size_t Remaining() const { return (size_t)(end_ - at_); }
		bool Failed() const { return failed_; }
		bool AtEnd() const { return at_ == end_; }
		const u8* Position() const { return at_; }

	private:
		const u8* at_ = nullptr;
		const u8* end_ = nullptr;
		bool failed_ = false;
	};
}
