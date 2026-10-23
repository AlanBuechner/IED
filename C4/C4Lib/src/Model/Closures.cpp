#include "Model/Closures.h"
#include "Model/ProgramModel.h"
#include "Model/TypeStore.h"

namespace Scripting
{
	namespace
	{
		constexpr const char* kCallOperator = "call";
		constexpr const char* kEmptyField = "__empty"; // a closure without captures still has a size (C++: 1)

		const Type* FieldTypeOf(const ClosureCapture& capture, TypeStore& types)
		{
			switch (capture.kind)
			{
			case ClosureCapture::Kind::Reference:
			case ClosureCapture::Kind::This: return types.Pointer(capture.type, capture.constReferent);
			default: return capture.type;
			}
		}

		ObjectType::FieldInfo Field(const Core::String& name, const Type* type)
		{
			ObjectType::FieldInfo f;
			f.mName = name;
			f.m_Type = type;
			return f;
		}
	}

	bool Closure::IsOnePointer() const
	{
		return captures.Count() == 1 && (captures[0].kind == ClosureCapture::Kind::Reference || captures[0].kind == ClosureCapture::Kind::This);
	}

	const ClosureCapture* Closure::ThisCapture() const
	{
		for (const ClosureCapture& c : captures)
			if (c.kind == ClosureCapture::Kind::This || c.kind == ClosureCapture::Kind::ThisCopy) return &c;
		return nullptr;
	}

	const Closure* ClosureTypes::Create(ProgramModel& model, const ClosureSpec& spec)
	{
		// Made from the spec alone, so a frozen model has it made between rounds (ParallelLowering).
		model.Writes().Ask("a closure", [&model, spec] { Create(model, spec); });
		// Named for the code that writes it and its place there, so every program with that code names it alike.
		const uint32 index = model.m_ClosuresByOwner[spec.owner]++;
		const Core::String name = "__c4_closure_" + spec.owner + "_" + std::to_string(index);
		auto closure = std::make_unique<Closure>();
		closure->displayName = (index == 0 ? Core::String("lambda") : "lambda #" + std::to_string(index + 1)) + " in " + spec.ownerName;
		closure->captures = spec.captures;
		closure->enclosingType = spec.enclosingType;
		closure->isMutable = spec.isMutable;

		ObjectType* type = NewClosureType(name, closure->captures, *model.m_Types);
		model.m_Types->Own(type);
		model.m_ObjectTypes.Push(type);
		closure->type = type;
		const FunctionType* signature = model.m_Types->Own(new FunctionType(spec.returned, spec.params));
		closure->call = AddCallMethod(model, type, spec, signature, closure->CapturesNothing());
		type->CalculateLayout();

		const Closure* stored = closure.get();
		model.m_Closures.emplace(type, std::move(closure));
		model.m_ClosuresMade.emplace(std::make_pair(spec.owner, spec.body), stored);
		return stored;
	}

	ObjectType* ClosureTypes::NewClosureType(const Core::String& name, Core::Vector<ClosureCapture>& captures, TypeStore& types)
	{
		ObjectType* type = new ObjectType(TypeKind::Object, name);
		type->SetObjectKind(ObjectType::ObjectKind::Struct);
		type->SetDefined(true);
		for (uint32 i = 0; i < captures.Count(); ++i)
		{
			captures[i].field = i;
			type->AddField(Field("__capture_" + captures[i].name, FieldTypeOf(captures[i], types)));
		}
		if (captures.Empty()) type->AddField(Field(kEmptyField, types.Builtin(TypeKind::UInt8)));
		return type;
	}

	const FunctionRecord* ClosureTypes::AddCallMethod(ProgramModel& model, ObjectType* type, const ClosureSpec& spec, const FunctionType* signature,
		bool isStatic)
	{
		ObjectType::MethodInfo m;
		m.mName = "Call";
		m.m_ReturnType = signature->GetReturnType();
		for (const auto& p : signature->GetParameters()) m.m_ParameterTypes.Push(p.m_Type);
		m.m_IsStatic = isStatic;
		m.m_OperatorAnnotations.Push(kCallOperator);
		m.m_HasBody = true;
		type->AddMethod(m);

		FunctionRecord rec;
		rec.type = signature;
		rec.scope = spec.scope;
		rec.containingType = type;
		rec.isStatic = isStatic;
		rec.isLocal = true; // lowered by the lambda's lowering, emitted next to the function that wrote it
		rec.bodyNode = spec.body;
		rec.definingUnit = spec.unitId;
		rec.name = m.mName;
		rec.mangledName = type->GetName() + "_Call_" + signature->OverloadSuffix();
		rec.minArity = signature->GetParameterCount();
		rec.operatorAnnotations.Push(kCallOperator);
		for (const Core::String& p : spec.paramNames) rec.paramNames.Push(p);
		rec.paramIsConst = spec.paramIsConst;
		rec.isConst = !isStatic && !spec.isMutable; // like C++: a lambda's call operator is const unless `mutable`
		const FunctionRecord* stored = model.m_Symbols->AddFunctionRecord(rec);
		model.m_MethodRecords[type].Push(stored);
		model.m_RecordBySignature.emplace(rec.type, stored);
		return stored;
	}
}
