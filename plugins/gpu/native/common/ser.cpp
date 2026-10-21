#include "ser.h"

namespace fb
{
	void* Arena::Alloc(size_t size, size_t align) {
		size_t pad = at_ ? (size_t)((align - ((uintptr_t)at_ & (align - 1))) & (align - 1)) : 0;
		if (!at_ || left_ < size + pad) {
			const size_t block = size + align > 65536 ? size + align : 65536;
			blocks_.emplace_back(new u8[block]);
			at_ = blocks_.back().get();
			left_ = block;
			memset(at_, 0, block);
			pad = (size_t)((align - ((uintptr_t)at_ & (align - 1))) & (align - 1));
		}
		u8* result = at_ + pad;
		at_ += pad + size;
		left_ -= pad + size;
		return result;
	}

	void Arena::Reset() {
		blocks_.clear();
		at_ = nullptr;
		left_ = 0;
	}

	void Ser::Blob(const char* name, const void*& data, u64& size) {
		if (writer_) {
			if (!data) size = 0;
			writer_->Put(size);
			writer_->Bytes(data, (size_t)size);
		}
		else {
			size = reader_->Get<u64>();
			data = reader_->View((size_t)size);
			if (!data) size = 0;
		}
		if (json_) {
			json_->BeginObject(name);
			json_->Uint("bytes", size);
			json_->EndObject();
		}
	}

	void Ser::Text(const char* name, std::string& text) {
		u32 length = (u32)text.size();
		if (writer_) {
			writer_->Put(length);
			writer_->Bytes(text.data(), length);
		}
		else {
			length = reader_->Get<u32>();
			const u8* bytes = reader_->View(length);
			text.assign(bytes ? (const char*)bytes : "", bytes ? length : 0);
		}
		if (json_) json_->String(name, text);
	}

	void Ser::Str(const char* name, const char*& text) {
		u32 length = text ? (u32)strlen(text) : 0;
		u8 present = text ? 1 : 0;
		if (writer_) {
			writer_->Put(present);
			writer_->Put(length);
			writer_->Bytes(text, length);
		}
		else {
			present = reader_->Get<u8>();
			length = reader_->Get<u32>();
			const u8* bytes = reader_->View(length);
			if (!present || !bytes) text = nullptr;
			else {
				char* copy = (char*)arena_->Alloc(length + 1, 1);
				memcpy(copy, bytes, length);
				text = copy;
			}
		}
		if (json_) {
			if (text) json_->String(name, text);
			else json_->Null(name);
		}
	}

	void Ser::WStr(const char* name, const wchar_t*& text) {
		std::string narrow = writer_ && text ? Narrow(text) : std::string();
		u8 present = text ? 1 : 0;
		if (writer_) {
			writer_->Put(present);
			writer_->Put((u32)narrow.size());
			writer_->Bytes(narrow.data(), narrow.size());
		}
		else {
			present = reader_->Get<u8>();
			const u32 length = reader_->Get<u32>();
			const u8* bytes = reader_->View(length);
			narrow.assign(bytes ? (const char*)bytes : "", bytes ? length : 0);
			if (!present) text = nullptr;
			else {
				std::wstring wide = Widen(narrow);
				wchar_t* copy = (wchar_t*)arena_->Alloc((wide.size() + 1) * sizeof(wchar_t), 2);
				memcpy(copy, wide.data(), wide.size() * sizeof(wchar_t));
				text = copy;
			}
		}
		if (json_) {
			if (present) json_->String(name, narrow);
			else json_->Null(name);
		}
	}

	void Ser::DescribeObject(const char* name, u64 id) {
		if (!id) {
			json_->Null(name);
			return;
		}
		json_->BeginObject(name);
		json_->Uint("$ref", id);
		std::string label = readMap_ ? readMap_->Name(id) : std::string();
		if (!label.empty()) json_->String("name", label);
		json_->EndObject();
	}

	void Ser::Address(const char* name, u64& va) {
		u64 id = 0, offset = 0;
		if (writer_) {
			if (va) writeMap_->Address(va, id, offset);
			writer_->Put(id);
			writer_->Put(offset);
		}
		else {
			id = reader_->Get<u64>();
			offset = reader_->Get<u64>();
			va = id ? readMap_->Address(id, offset) : offset;
		}
		lastId_ = id;
		if (json_) {
			if (!id) json_->Uint(name, offset);
			else {
				json_->BeginObject(name);
				json_->Uint("$ref", id);
				std::string label = readMap_ ? readMap_->Name(id) : std::string();
				if (!label.empty()) json_->String("name", label);
				json_->Uint("offset", offset);
				json_->EndObject();
			}
		}
	}

	void Ser::CpuDescriptor(const char* name, u64& ptr) {
		u64 heap = 0;
		u32 index = 0;
		if (writer_) {
			if (ptr) writeMap_->CpuDescriptor(ptr, heap, index);
			writer_->Put(heap);
			writer_->Put(index);
		}
		else {
			heap = reader_->Get<u64>();
			index = reader_->Get<u32>();
			ptr = heap ? readMap_->CpuDescriptor(heap, index) : 0;
		}
		lastId_ = heap;
		if (json_) {
			if (!heap) json_->Null(name);
			else {
				json_->BeginObject(name);
				json_->Uint("$heap", heap);
				std::string label = readMap_ ? readMap_->Name(heap) : std::string();
				if (!label.empty()) json_->String("name", label);
				json_->Uint("index", index);
				json_->EndObject();
			}
		}
	}

	void Ser::GpuDescriptor(const char* name, u64& ptr) {
		u64 heap = 0;
		u32 index = 0;
		if (writer_) {
			if (ptr) writeMap_->GpuDescriptor(ptr, heap, index);
			writer_->Put(heap);
			writer_->Put(index);
		}
		else {
			heap = reader_->Get<u64>();
			index = reader_->Get<u32>();
			ptr = heap ? readMap_->GpuDescriptor(heap, index) : 0;
		}
		lastId_ = heap;
		if (json_) {
			if (!heap) json_->Null(name);
			else {
				json_->BeginObject(name);
				json_->Uint("$heap", heap);
				std::string label = readMap_ ? readMap_->Name(heap) : std::string();
				if (!label.empty()) json_->String("name", label);
				json_->Uint("index", index);
				json_->EndObject();
			}
		}
	}

	std::string JoinFlags(u64 value, const FlagName* names, const char* none) {
		if (!value) return none;
		std::string text;
		u64 rest = value;
		for (const FlagName* n = names; n->name; ++n) {
			if (n->bit && (rest & n->bit) == n->bit) {
				if (!text.empty()) text += '|';
				text += n->name;
				rest &= ~n->bit;
			}
		}
		if (rest) {
			if (!text.empty()) text += '|';
			text += Format("0x%llx", (unsigned long long)rest);
		}
		return text;
	}
}
