#include "Json/JsonValue.h"

namespace Json
{
	Value Value::Null() { return Value(Core::ObjectDescription(Core::ObjectDescription::Type::Null)); }
	Value Value::Bool(bool value) { return Value(Core::ObjectDescription::CreateFrom(value)); }
	Value Value::Int(int64 value) { return Value(Core::ObjectDescription::CreateFrom(value)); }
	Value Value::UInt(uint64 value) { return Value(Core::ObjectDescription::CreateFrom(value)); }
	Value Value::Float(double value) { return Value(Core::ObjectDescription::CreateFrom(value)); }
	Value Value::String(const Core::String& value) { return Value(Core::ObjectDescription::CreateFrom(value)); }

	Value Value::MakeObject() { return Value(Core::ObjectDescription(Core::ObjectDescription::Type::Object)); }
	Value Value::MakeArray() { return Value(Core::ObjectDescription(Core::ObjectDescription::Type::Array)); }

	bool Value::GetBool(bool fallback) const { return m_Desc.IsBool() ? m_Desc.GetAsBool() : fallback; }
	int64 Value::GetInt(int64 fallback) const { return m_Desc.IsInt() ? m_Desc.GetAsInt() : fallback; }
	uint64 Value::GetUInt(uint64 fallback) const { return m_Desc.IsNumber() ? static_cast<uint64>(m_Desc.GetAsUint()) : fallback; }
	double Value::GetFloat(double fallback) const { return m_Desc.IsFloat() ? m_Desc.GetAsFloat() : fallback; }

	Core::String Value::GetString(const Core::String& fallback) const
	{
		return m_Desc.IsString() ? m_Desc.GetAsString() : fallback;
	}

	bool Value::HasMember(const Core::String& key) const
	{
		return m_Desc.IsObject() && m_Desc.HasEntery(key);
	}

	Value Value::Member(const Core::String& key, Value fallback) const
	{
		if (m_Desc.IsObject() && m_Desc.HasEntery(key))
			return Value(m_Desc[key]);
		return fallback;
	}

	void Value::SetMember(const Core::String& key, const Value& value)
	{
		m_Desc.GetAsDescriptionMap()[key] = value.m_Desc;
	}

	uint32 Value::MemberCount() const
	{
		return m_Desc.IsObject() ? static_cast<uint32>(m_Desc.GetAsDescriptionMap().size()) : 0;
	}

	uint32 Value::Size() const
	{
		return m_Desc.IsArray() ? m_Desc.GetAsObjectArray().Count() : 0;
	}

	Value Value::At(uint32 index) const
	{
		if (m_Desc.IsArray() && index < m_Desc.GetAsObjectArray().Count())
			return Value(m_Desc[index]);
		return Null();
	}

	void Value::Append(const Value& element)
	{
		m_Desc.GetAsObjectArray().Push(element.m_Desc);
	}
} // namespace Json
