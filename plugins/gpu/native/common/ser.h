// ser.h - one serialization function per API structure, run three ways (docs/22 "The capture file"): writing it in the
// capture layer, reading it back in the replayer, and describing it as JSON (field and enum names) while reading, for
// the API Calls window. A structure's function is a free `void Serialize(fb::Ser& s, T& value)` that names each field:
//
//     void Serialize(fb::Ser& s, D3D12_VIEWPORT& v) {
//         s.Field("TopLeftX", v.TopLeftX);
//         ...
//     }
//
// Objects travel as ids, GPU addresses as (object id, offset), descriptor handles as (heap id, index): the maps below
// translate them on each side, so a replay rebuilds every reference against its own objects.
#pragma once

#include "bytes.h"
#include "json.h"

#include <type_traits>

namespace fb
{
	// Bump allocation for decoded records: zeroed memory that lives until Reset.
	class Arena
	{
	public:
		void* Alloc(size_t size, size_t align = 16);
		template <class T> T* Array(size_t count) { return count ? (T*)Alloc(sizeof(T) * count, alignof(T) < 16 ? 16 : alignof(T)) : nullptr; }
		void Reset();

	private:
		std::vector<std::unique_ptr<u8[]>> blocks_;
		u8* at_ = nullptr;
		size_t left_ = 0;
	};

	// Capture side: object pointers and addresses to ids.
	class SerWriteMap
	{
	public:
		virtual ~SerWriteMap() = default;
		virtual u64 Id(const void* object) = 0;
		virtual void Address(u64 va, u64& id, u64& offset) {
			id = 0;
			offset = va;
		}
		virtual void CpuDescriptor(u64 ptr, u64& heap, u32& index) {
			heap = 0;
			index = (u32)ptr;
		}
		virtual void GpuDescriptor(u64 ptr, u64& heap, u32& index) {
			heap = 0;
			index = (u32)ptr;
		}
	};

	// Replay side: ids back to the replay's objects and addresses, and names for descriptions.
	class SerReadMap
	{
	public:
		virtual ~SerReadMap() = default;
		virtual void* Object(u64 id) { return (void*)(uintptr_t)id; }
		virtual u64 Address(u64 id, u64 offset) { return id ? 0 : offset; }
		virtual u64 CpuDescriptor(u64 heap, u32 index) { return 0; }
		virtual u64 GpuDescriptor(u64 heap, u32 index) { return 0; }
		virtual std::string Name(u64 id) { return std::string(); }
	};

	// Names of enum values for descriptions; a header per API overloads these for its enums.
	template <class E> const char* EnumName(E) { return nullptr; }

	class Ser;
	template <class T> void SerField(Ser& s, const char* name, T& value);

	class Ser
	{
	public:
		Ser(ByteWriter& writer, SerWriteMap* map) : writer_(&writer), writeMap_(map) {}
		Ser(ByteReader& reader, Arena& arena, SerReadMap* map, JsonWriter* describe = nullptr)
			: reader_(&reader), arena_(&arena), readMap_(map), json_(describe) {}

		bool Writing() const { return writer_ != nullptr; }
		bool Reading() const { return reader_ != nullptr; }
		bool Describing() const { return json_ != nullptr; }
		bool Failed() const { return reader_ && reader_->Failed(); }
		JsonWriter* Json() { return json_; }
		Arena* Memory() { return arena_; }
		SerReadMap* ReadMap() { return readMap_; }
		// The id the last Object/Handle/Address/descriptor call wrote or read (the heap's for a descriptor).
		u64 LastId() const { return lastId_; }

		// A value, or a structure through its Serialize.
		template <class T> void Field(const char* name, T& value) {
			if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) Value(name, value);
			else SerField(*this, name, value);
		}

		template <class T> void Value(const char* name, T& value) {
			static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>, "Value takes numbers and enums");
			if (writer_) writer_->Put(value);
			else reader_->Bytes(&value, sizeof(T));
			if (json_) Describe(name, value);
		}

		// A bit set described as the names of its bits ("RENDER_TARGET|COPY_DEST").
		template <class T> void Flags(const char* name, T& value, std::string (*names)(u64)) {
			if (writer_) writer_->Put(value);
			else reader_->Bytes(&value, sizeof(T));
			if (json_) json_->String(name, names((u64)value));
		}

		// A fixed array of values or structures.
		template <class T, size_t N> void Fixed(const char* name, T (&items)[N]) {
			if (json_) json_->BeginArray(name);
			for (size_t i = 0; i < N; ++i) Field(nullptr, items[i]);
			if (json_) json_->EndArray();
		}

		// `count` items at `items` (count serialized by the caller); reading allocates them.
		template <class T, class C> void Items(const char* name, T*& items, C count) {
			using U = std::remove_const_t<T>;
			U* at = writer_ ? const_cast<U*>(items) : arena_->Array<U>((size_t)count);
			if (json_) json_->BeginArray(name);
			for (C i = 0; i < count; ++i) Field(nullptr, at[i]);
			if (json_) json_->EndArray();
			if (reader_) items = at;
		}

		// A count and that many items; the count's own field is left out of the description.
		template <class T, class C> void Array(const char* name, T*& items, C& count) {
			u64 wide = (u64)count;
			if (writer_) {
				if (!items) wide = 0;
				writer_->Put(wide);
			}
			else {
				wide = reader_->Get<u64>();
				if (wide > reader_->Remaining()) {
					// Corrupt: every item takes at least a byte.
					reader_->View(reader_->Remaining() + 1);
					wide = 0;
				}
				count = (C)wide;
			}
			Items(name, items, (C)wide);
		}

		// A structure behind a pointer that may be null.
		template <class T> void Optional(const char* name, T*& item) {
			using U = std::remove_const_t<T>;
			u8 present = item ? 1 : 0;
			if (writer_) writer_->Put(present);
			else present = reader_->Get<u8>();
			if (!present) {
				if (reader_) item = nullptr;
				if (json_) json_->Null(name);
				return;
			}
			U* at = writer_ ? const_cast<U*>(item) : arena_->Array<U>(1);
			Field(name, *at);
			if (reader_) item = at;
		}

		// Raw bytes (shader bytecode, constants); reading points into the record, which outlives the use.
		void Blob(const char* name, const void*& data, u64& size);
		template <class C> void Blob(const char* name, const void*& data, C& size) {
			u64 wide = (u64)size;
			Blob(name, data, wide);
			size = (C)wide;
		}
		void Str(const char* name, const char*& text);
		void WStr(const char* name, const wchar_t*& text);
		void Text(const char* name, std::string& text);

		// An API object (an interface pointer, a dispatchable handle) as its id.
		template <class I> void Object(const char* name, I*& object) {
			u64 id = writer_ ? writeMap_->Id((const void*)object) : 0;
			if (writer_) writer_->Put(id);
			else {
				id = reader_->Get<u64>();
				object = (I*)readMap_->Object(id);
			}
			lastId_ = id;
			if (json_) DescribeObject(name, id);
		}
		// A non-dispatchable 64-bit handle (Vulkan) as its id.
		template <class H> void Handle(const char* name, H& handle) {
			static_assert(sizeof(H) == 8, "handles are 64-bit");
			u64 id = 0;
			if (writer_) {
				u64 raw;
				memcpy(&raw, &handle, 8);
				id = raw ? writeMap_->Id((const void*)(uintptr_t)raw) : 0;
				writer_->Put(id);
			}
			else {
				id = reader_->Get<u64>();
				const u64 raw = (u64)(uintptr_t)readMap_->Object(id);
				memcpy(&handle, &raw, 8);
			}
			lastId_ = id;
			if (json_) DescribeObject(name, id);
		}
		void Address(const char* name, u64& va);
		void CpuDescriptor(const char* name, u64& ptr);
		void GpuDescriptor(const char* name, u64& ptr);

		// Begins and ends a structure of the description (SerField does this for Field).
		void BeginStruct(const char* name) {
			if (json_) json_->BeginObject(name);
		}
		void EndStruct() {
			if (json_) json_->EndObject();
		}

	private:
		template <class T> void Describe(const char* name, T value) {
			if constexpr (std::is_enum_v<T>) {
				const char* text = EnumName(value);
				if (text) json_->String(name, text);
				else json_->Int(name, (i64)value);
			}
			else if constexpr (std::is_same_v<T, bool>) json_->Bool(name, value);
			else if constexpr (std::is_floating_point_v<T>) json_->Double(name, (double)value);
			else if constexpr (std::is_signed_v<T>) json_->Int(name, (i64)value);
			else json_->Uint(name, (u64)value);
		}
		void DescribeObject(const char* name, u64 id);

		ByteWriter* writer_ = nullptr;
		ByteReader* reader_ = nullptr;
		Arena* arena_ = nullptr;
		SerWriteMap* writeMap_ = nullptr;
		SerReadMap* readMap_ = nullptr;
		JsonWriter* json_ = nullptr;
		u64 lastId_ = 0;
	};

	// The default for structures: an object of the description holding its fields. An API header overloads it for
	// types that read better another way (descriptor handles as "heap[index]").
	template <class T> void SerField(Ser& s, const char* name, T& value) {
		s.BeginStruct(name);
		Serialize(s, value);
		s.EndStruct();
	}

	// "A|B|C" for the bits of `value` named in `names` (bit, name pairs ending in a null name); unnamed bits as hex.
	struct FlagName
	{
		u64 bit;
		const char* name;
	};
	std::string JoinFlags(u64 value, const FlagName* names, const char* none = "0");
}
