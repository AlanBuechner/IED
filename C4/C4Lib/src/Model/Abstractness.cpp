#include "Model/Abstractness.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/VirtualTables.h"

namespace Scripting::Abstractness
{
	namespace
	{
		// The first virtual method of `type`'s vtables whose final overrider has no implementation.
		const ObjectType::MethodInfo* Unimplemented(const ProgramModel& model, const ObjectType* type)
		{
			if (!type->IsPolymorphic()) return nullptr;
			for (const VirtualTables::VptrLocation& location : VirtualTables::VptrLocations(type))
				for (const VirtualTables::Slot& slot : VirtualTables::PrimarySlots(location.layout))
				{
					VirtualTables::Overrider runs = VirtualTables::FinalOverrider(type, location, slot);
					const FunctionRecord* rec = model.RecordForMethod(runs.owner, runs.method);
					if (!rec || !(rec->bodyNode || rec->isLinked || rec->isHostDefined)) return runs.method;
				}
			return nullptr;
		}

		const Type* ValueType(const Type* t)
		{
			while (t && (t->GetKind() == TypeKind::Alias || t->GetKind() == TypeKind::Array))
				t = t->GetKind() == TypeKind::Alias ? static_cast<const AliasType*>(t)->GetAliasedType() : static_cast<const ArrayType*>(t)->GetElementType();
			return t;
		}
	}

	Core::String WhyAbstract(const ProgramModel& model, const ObjectType* type)
	{
		if (type->IsInterface()) return "'" + type->GetName() + "' is an interface";
		if (type->IsAbstract()) return "'" + type->GetName() + "' is abstract";
		if (const ObjectType::MethodInfo* missing = Unimplemented(model, type))
			return "'" + type->GetName() + "' is abstract: '" + missing->mName + "' is not implemented";
		return "";
	}

	Core::String WhyNoValue(const ProgramModel& model, const Type* type)
	{
		const Type* t = ValueType(type);
		if (!t || t->GetKind() != TypeKind::Object) return "";
		return WhyAbstract(model, static_cast<const ObjectType*>(t));
	}

	Core::String ByValue(const Core::String& what, const Core::String& why)
	{
		return what + ": " + why + "; use a pointer or a reference";
	}

	Core::String Signature(const ProgramModel& model, const FunctionRecord& fn)
	{
		if (!fn.type) return "";
		if (Core::String why = WhyNoValue(model, fn.type->GetReturnType()); !why.empty())
			return ByValue("the return value of '" + fn.name + "'", why);
		const auto& params = fn.type->GetParameters();
		for (uint32 i = 0; i < params.Count(); ++i)
			if (Core::String why = WhyNoValue(model, params[i].m_Type); !why.empty())
				return ByValue("parameter '" + (i < fn.paramNames.Count() ? fn.paramNames[i] : Core::String("?")) + "' of '" + fn.name + "'", why);
		return "";
	}

	Core::String Creation(const ProgramModel& model, const Type* type)
	{
		Core::String why = WhyNoValue(model, type);
		return why.empty() ? why : "cannot create a '" + ValueType(type)->GetName() + "': " + why;
	}
} // namespace Scripting::Abstractness
