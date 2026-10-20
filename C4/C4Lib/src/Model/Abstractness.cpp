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
		const Core::String name = "'" + type->GetName() + "'";
		if (type->IsInterface()) return name + " is an interface";
		if (type->IsAbstract()) return name + " is abstract";
		if (const ObjectType::MethodInfo* missing = Unimplemented(model, type))
			return name + " is abstract: '" + missing->mName + "' is not implemented";
		return "";
	}

	Core::String WhyNoValue(const ProgramModel& model, const Type* type)
	{
		const Type* t = ValueType(type);
		if (!t || t->GetKind() != TypeKind::Object) return "";
		return WhyAbstract(model, static_cast<const ObjectType*>(t));
	}

	Core::String ByValue(const ProgramModel& model, const Core::String& what, const Type* type)
	{
		Core::String why = WhyNoValue(model, type);
		return why.empty() ? why : what + ": " + why + "; use a pointer or a reference";
	}

	Core::String Signature(const ProgramModel& model, const FunctionRecord& fn)
	{
		if (!fn.type) return "";
		if (Core::String why = ByValue(model, "the return value of '" + fn.name + "'", fn.type->GetReturnType()); !why.empty()) return why;
		const auto& params = fn.type->GetParameters();
		for (uint32 i = 0; i < params.Count(); ++i)
		{
			const Core::String name = i < fn.paramNames.Count() ? fn.paramNames[i] : Core::String("?");
			if (Core::String why = ByValue(model, "parameter '" + name + "' of '" + fn.name + "'", params[i].m_Type); !why.empty()) return why;
		}
		return "";
	}

	Core::String Creation(const ProgramModel& model, const Type* type)
	{
		Core::String why = WhyNoValue(model, type);
		return why.empty() ? why : "cannot create a '" + ValueType(type)->GetName() + "': " + why;
	}
} // namespace Scripting::Abstractness
