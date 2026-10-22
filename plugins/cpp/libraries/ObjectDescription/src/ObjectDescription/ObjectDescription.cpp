#include "ObjectDescription.h"
#include "Core/Core.h"
#include "Reflection.h"

namespace Core
{
	std::unordered_map<uint64, ConverterBase*>& ConverterBase::GetObjectConverterFunctions()
	{
		static std::unordered_map<uint64, ConverterBase*> converterFunctions;
		return converterFunctions;
	}

	ObjectDescription ObjectDescription::SerializeObject(void* obj, uint64 typeID)
	{
		ObjectDescription desc(ObjectDescription::Type::Object);

		auto converterIt = ConverterBase::GetObjectConverterFunctions().find(typeID);
		if (converterIt != ConverterBase::GetObjectConverterFunctions().end()) {
			desc = converterIt->second->EncodeObj(obj);
		}
		else
		{
			const Reflect::Class* objectClass = Reflect::Registry::GetRegistry()->GetClass(typeID);
			const auto& properties = objectClass->GetProperties();
			for (const auto& prop : properties) {
				void* propData = (void*)((char*)obj + prop.GetOffset());
				desc.GetAsDescriptionMap()[prop.GetName()] = SerializeObject(propData, prop.GetTypeID());
			}
		}

		return desc;
	}

	void ObjectDescription::DeserializeObject(void* obj, uint64 typeID, const ObjectDescription& desc)
	{

		auto converterIt = ConverterBase::GetObjectConverterFunctions().find(typeID);
		if (converterIt != ConverterBase::GetObjectConverterFunctions().end()) {
			converterIt->second->DecodeObj(obj, desc);
		}
		else
		{
			const Reflect::Class* objectClass = Reflect::Registry::GetRegistry()->GetClass(typeID);
			const auto& properties = objectClass->GetProperties();
			for (const auto& prop : properties)
			{
				if (!desc.HasEntery(prop.GetName()))
					continue;

				const ObjectDescription& propDesc = desc[prop.GetName()];
				void* propData = (void*)((char*)obj + prop.GetOffset());
				DeserializeObject(propData, prop.GetTypeID(), propDesc);
			}
		}
	}

	ObjectDescription::ObjectDescription(Type type) :
		m_Type(type)
	{
	}

	ObjectDescription& ObjectDescription::SetType(Type type)
	{
		m_Type = type;
		return *this;
	}

	bool ObjectDescription::HasEntery(const Core::String& key) const
	{
		CORE_ASSERT(m_Type == ObjectDescription::Type::Object, "Description is not of type object", "");
		return m_Enteries.find(key) != m_Enteries.end();
	}

}



ADD_OBJECT_CONVERTER(Core::String);

ADD_OBJECT_CONVERTER(uint8);
ADD_OBJECT_CONVERTER(uint16);
ADD_OBJECT_CONVERTER(uint32);
ADD_OBJECT_CONVERTER(uint64);

ADD_OBJECT_CONVERTER(int8);
ADD_OBJECT_CONVERTER(int16);
ADD_OBJECT_CONVERTER(int32);
ADD_OBJECT_CONVERTER(int64);

ADD_OBJECT_CONVERTER(float);
ADD_OBJECT_CONVERTER(double);

ADD_OBJECT_CONVERTER(Math::Vector2F);
ADD_OBJECT_CONVERTER(Math::Vector3F);
ADD_OBJECT_CONVERTER(Math::Vector4F);

ADD_OBJECT_CONVERTER(bool);

