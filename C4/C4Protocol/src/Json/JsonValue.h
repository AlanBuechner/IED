#pragma once
#include "ObjectDescription/JsonObjectDescription.h"
#include "Core/Core.h"

namespace Json
{
	/// Safe wrapper around Core::ObjectDescription that hides its type-tagged union and
	/// throwing accessors; all LSP JSON is built and read through this.
	class Value
	{
	public:
		Value() = default;
		explicit Value(const Core::ObjectDescription& desc) : m_Desc(desc) {}

		static Value Null();
		static Value Bool(bool value);
		static Value Int(int64 value);
		static Value UInt(uint64 value);
		static Value Float(double value);
		static Value String(const Core::String& value);
		static Value MakeObject();
		static Value MakeArray();

		bool IsNull() const { return m_Desc.IsNull(); }
		bool IsBool() const { return m_Desc.IsBool(); }
		bool IsInt() const { return m_Desc.IsInt(); }
		bool IsFloat() const { return m_Desc.IsFloat(); }
		bool IsNumber() const { return m_Desc.IsNumber(); }
		bool IsString() const { return m_Desc.IsString(); }
		bool IsArray() const { return m_Desc.IsArray(); }
		bool IsObject() const { return m_Desc.IsObject(); }

		bool GetBool(bool fallback = false) const;
		int64 GetInt(int64 fallback = 0) const;
		uint64 GetUInt(uint64 fallback = 0) const;
		double GetFloat(double fallback = 0.0) const;
		Core::String GetString(const Core::String& fallback = "") const;

		bool HasMember(const Core::String& key) const;
		Value Member(const Core::String& key, Value fallback = Null()) const;
		void SetMember(const Core::String& key, const Value& value);
		uint32 MemberCount() const;

		uint32 Size() const;
		Value At(uint32 index) const;
		void Append(const Value& element);

		Core::String Serialize() const { return Core::SaveObjectDescriptionToJson(m_Desc); }

		const Core::ObjectDescription& Raw() const { return m_Desc; }

	private:
		Core::ObjectDescription m_Desc;
	};
} // namespace Json
