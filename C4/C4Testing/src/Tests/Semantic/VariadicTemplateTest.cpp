#include "VariadicTemplateTest.h"
#include "Model/TypeHandle.h"
#include "Model/Types/SignatureType.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		ProgramModel& MakeModel(CompilerInstance& compiler, const char* source)
		{
			compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetProgramModelForLowering();
		}

		const ObjectType* Object(const ProgramModel& model, const char* name)
		{
			auto t = model.QueryType(name);
			return t.Ok() ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(t.value)) : nullptr;
		}

		const Type* FieldType(const ObjectType* obj, const char* name)
		{
			if (!obj) return nullptr;
			for (const auto& f : obj->GetFields())
				if (f.mName == name) return f.m_Type;
			return nullptr;
		}

		const ObjectType* FieldObject(const ObjectType* obj, const char* name)
		{
			return dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(FieldType(obj, name)));
		}

		int64 StaticConstant(const ProgramModel& model, const ObjectType* owner, const char* name)
		{
			const Scope* scope = owner ? model.TypeScope(owner) : nullptr;
			const SymbolInfo* sym = scope ? scope->Find(name) : nullptr;
			return sym && !sym->constValue.IsNone() ? sym->constValue.AsInt() : -1;
		}

		const FunctionRecord* Resolve(const ProgramModel& model, const char* name, Core::Vector<const Type*> args)
		{
			const FunctionRecord* rec = nullptr;
			auto r = model.QueryFunction(name, nullptr, nullptr, args, {}, &rec);
			return r.Ok() ? rec : nullptr;
		}
	}

	void VariadicTemplateTest::PackBindsTrailingArguments()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T, ...Rest> struct Tup { public T head; static const int64 RestCount = sizeof...(Rest); }\n"
			"alias Three = Tup<int32, float, int8>;\n"
			"alias One = Tup<int64>;\n");
		const ObjectType* three = Object(model, "Three");
		TEST_ASSERT(three && three->GetTemplateArguments().Count() == 3);
		TEST_ASSERT(FieldType(three, "head") == model.TypeStorePtr()->Builtin(TypeKind::Int32));
		TEST_ASSERT_EQ((int)StaticConstant(model, three, "RestCount"), 2);
		TEST_ASSERT_EQ((int)StaticConstant(model, Object(model, "One"), "RestCount"), 0);
	}

	void VariadicTemplateTest::ExpansionInTemplateArguments()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<...Ts> struct List { public int8 tag; }\n"
			"template<T> struct Box { public T value; }\n"
			"template<...Ts> alias Boxed = List<Box<Ts>...>;\n"
			"template<...Ts> alias Pointers = List<Ts*...>;\n"
			"alias B = Boxed<int32, float>;\n"
			"alias P = Pointers<int16>;\n");
		const ObjectType* b = Object(model, "B");
		TEST_ASSERT(b && b->GetTemplateArguments().Count() == 2);
		const auto* first = dynamic_cast<const ObjectType*>(b->GetTemplateArguments()[0].type);
		TEST_ASSERT(first && FieldType(first, "value") == model.TypeStorePtr()->Builtin(TypeKind::Int32));
		const ObjectType* p = Object(model, "P");
		TEST_ASSERT(p && p->GetTemplateArguments().Count() == 1);
		TEST_ASSERT(p->GetTemplateArguments()[0].type == model.TypeStorePtr()->Pointer(model.TypeStorePtr()->Builtin(TypeKind::Int16)));
	}

	void VariadicTemplateTest::SizeOfPackIsAConstant()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<...Ts> struct Bytes { public int8[sizeof...(Ts)] data; }\n"
			"template<...int32 Ns> struct Seq { static const int32 Sum = (Ns + ... + 0); }\n"
			"alias B3 = Bytes<int8, int16, int32>;\n"
			"alias S = Seq<1, 2, 3, 4>;\n");
		const ObjectType* b = Object(model, "B3");
		TEST_ASSERT(b && b->GetSize() == 3);
		TEST_ASSERT_EQ((int)StaticConstant(model, Object(model, "S"), "Sum"), 10);
	}

	void VariadicTemplateTest::DeducesPackFromCallArguments()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "template<T, ...Ts> int64 Count(T first, Ts... rest) { return 0; }\n");
		TypeStore* ts = model.TypeStorePtr();
		const FunctionRecord* rec = Resolve(model, "Count", { ts->Builtin(TypeKind::Int32), ts->Builtin(TypeKind::Float32), ts->Builtin(TypeKind::Int8) });
		TEST_ASSERT(rec && rec->type->GetParameterCount() == 3);
		TEST_ASSERT(rec->type->GetParameters()[2].m_Type == ts->Builtin(TypeKind::Int8));
		TEST_ASSERT(rec->parameterPacks.Count() == 1 && rec->parameterPacks[0].name == "rest");
		TEST_ASSERT(rec->parameterPacks[0].first == 1 && rec->parameterPacks[0].count == 2);
		const FunctionRecord* one = Resolve(model, "Count", { ts->Builtin(TypeKind::Int32) });
		TEST_ASSERT(one && one->type->GetParameterCount() == 1 && one->parameterPacks[0].count == 0);
	}

	void VariadicTemplateTest::DeducesPackThroughInstance()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<...Ts> struct List { public int8 tag; }\n"
			"template<...Ts> int64 Arity(const List<Ts...>& l) { return sizeof...(Ts); }\n"
			"alias L = List<int32, float>;\n");
		const ObjectType* list = Object(model, "L");
		const FunctionRecord* rec = Resolve(model, "Arity", { list });
		TEST_ASSERT(rec != nullptr);
		TEST_ASSERT(TypeHandle::UnwrapToValue(rec->type->GetParameters()[0].m_Type) == list);
	}

	void VariadicTemplateTest::SpecializationPeelsPack()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<...Ts> struct Tuple { }\n"
			"template<T, ...Rest> struct Tuple<T, Rest...> { public T head; public Tuple<Rest...> tail; }\n"
			"alias T2 = Tuple<int32, float>;\n");
		TypeStore* ts = model.TypeStorePtr();
		const ObjectType* t2 = Object(model, "T2");
		TEST_ASSERT(FieldType(t2, "head") == ts->Builtin(TypeKind::Int32));
		const ObjectType* t1 = FieldObject(t2, "tail");
		TEST_ASSERT(FieldType(t1, "head") == ts->Builtin(TypeKind::Float32));
		const ObjectType* t0 = FieldObject(t1, "tail");
		TEST_ASSERT(t0 && t0->GetFields().Count() == 0 && t0->GetTemplateArguments().Count() == 0);
	}

	void VariadicTemplateTest::FixedPatternBeatsTrailingExpansion()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<...Ts> struct S { public int8 v; }\n"
			"template<T> struct S<T> { public int16 v; }\n"
			"template<T, ...R> struct S<T, R...> { public int32 v; }\n"
			"alias None = S<>;\n"
			"alias One = S<float>;\n"
			"alias Two = S<float, float>;\n");
		TypeStore* ts = model.TypeStorePtr();
		TEST_ASSERT(FieldType(Object(model, "None"), "v") == ts->Builtin(TypeKind::Int8));
		TEST_ASSERT(FieldType(Object(model, "One"), "v") == ts->Builtin(TypeKind::Int16));
		TEST_ASSERT(FieldType(Object(model, "Two"), "v") == ts->Builtin(TypeKind::Int32));
	}

	void VariadicTemplateTest::TopLevelConstIsNotPartOfAPattern()
	{
		// BUG-118: `T const*` and `T*` are one pattern; `const T*` (a pointer to const) is more specialized.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> struct K { public int8 v; }\n"
			"template<T> struct K<T*> { public int16 v; }\n"
			"template<T> struct K<T const*> { public int16 v; }\n"
			"template<T> struct K<const T*> { public int32 v; }\n"
			"alias C = K<const float*>;\n"
			"alias D = K<float*>;\n");
		TEST_ASSERT(FieldType(Object(model, "C"), "v") == model.TypeStorePtr()->Builtin(TypeKind::Int32));
		bool reported = false;
		for (const auto& e : compiler.GetAllErrors())
			reported = reported || std::string(e.message.c_str()).find("are the same specialization of 'K'") != std::string::npos;
		TEST_ASSERT(reported);
	}

	void VariadicTemplateTest::ExplicitFunctionSpecializationHasItsOwnBody()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> T Id(T v) { return v; }\n"
			"template<> int32 Id<int32>(int32 v) { return v + 1; }\n");
		TypeStore* ts = model.TypeStorePtr();
		const FunctionRecord* special = Resolve(model, "Id", { ts->Builtin(TypeKind::Int32) });
		const FunctionRecord* primary = Resolve(model, "Id", { ts->Builtin(TypeKind::Float32) });
		TEST_ASSERT(special && primary && special->bodyNode && primary->bodyNode);
		TEST_ASSERT(special->bodyNode != primary->bodyNode);
	}

	void VariadicTemplateTest::TemplateNameFindsInstanceForSignature()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> T Identity(T v) { return v; }\n"
			"template<R, A> R Convert(A a) { return (R)a; }\n");
		TypeStore* ts = model.TypeStorePtr();
		const Type* i32 = ts->Builtin(TypeKind::Int32);
		const Type* f32 = ts->Builtin(TypeKind::Float32);
		const TemplateNameType* identity = model.QueryTemplateName("Identity", 0, model.Symbols()->GetRoot());
		TEST_ASSERT(identity != nullptr);
		auto fits = identity->Instance(*static_cast<const SignatureType*>(ts->FunPtr(i32, { i32 })));
		TEST_ASSERT(fits.Ok() && fits.value->type->GetReturnType() == i32);
		auto mismatch = identity->Instance(*static_cast<const SignatureType*>(ts->FunPtr(i32, { i32, i32 })));
		TEST_ASSERT(!mismatch.Ok() && std::string(mismatch.message.c_str()).find("no instance") != std::string::npos);
		const TemplateNameType* convert = model.QueryTemplateName("Convert", 0, model.Symbols()->GetRoot());
		auto byReturn = convert->Instance(*static_cast<const SignatureType*>(ts->FunPtr(f32, { i32 })));
		TEST_ASSERT(byReturn.Ok() && byReturn.value->type->GetReturnType() == f32);
		TEST_ASSERT(model.QueryTemplateName("Missing", 0, model.Symbols()->GetRoot()) == nullptr);
	}
} // namespace Testing
