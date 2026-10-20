#include "ConstLayersTest.h"
#include "Model/TypeHandle.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/ArrayType.h"
#include <cstring>

namespace Testing
{
	using namespace Scripting;

	static ProgramModel& MakeModel(CompilerInstance& compiler, const char* source)
	{
		compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		return compiler.GetProgramModelForLowering();
	}

	static const PointerType* PointerOf(const ProgramModel& model, const char* variable)
	{
		auto v = model.QueryVariable(variable);
		const Type* t = v.Ok() ? TypeHandle::UnwrapToValue(v.value.type) : nullptr;
		return t && t->GetKind() == TypeKind::Pointer ? static_cast<const PointerType*>(t) : nullptr;
	}

	static bool IsConstVariable(const ProgramModel& model, const char* variable)
	{
		auto v = model.QueryVariable(variable);
		return v.Ok() && v.value.symbol && v.value.symbol->isConst;
	}

	static const FunctionRecord* Method(ProgramModel& model, const char* mangledPrefix, bool isConst)
	{
		Core::Vector<const FunctionRecord*> recs;
		model.AllFunctionRecords(recs);
		for (const auto* r : recs)
			if (r->mangledName.rfind(mangledPrefix, 0) == 0 && r->isConst == isConst) return r;
		return nullptr;
	}

	void ConstLayersTest::EachConstQualifiesTheLayerAfterIt()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 x;\n"
			"int32 const* fixedPointer = &x;\n"     // const pointer to int32
			"const int32* readOnlyTarget = &x;\n"   // pointer to const int32
			"int32 const** outer;\n"                // pointer to const pointer to int32
			"const int32[4]* toConstArray;\n");     // pointer to an array of const int32
		const PointerType* fixed = PointerOf(model, "fixedPointer");
		const PointerType* target = PointerOf(model, "readOnlyTarget");
		const PointerType* outer = PointerOf(model, "outer");
		const PointerType* array = PointerOf(model, "toConstArray");
		TEST_ASSERT(fixed && !fixed->IsConstReferent());
		TEST_ASSERT(target && target->IsConstReferent());
		TEST_ASSERT(outer && outer->IsConstReferent());
		const auto* inner = dynamic_cast<const PointerType*>(outer->GetPointeeType());
		TEST_ASSERT(inner && !inner->IsConstReferent());
		TEST_ASSERT(array && array->IsConstReferent() && dynamic_cast<const ArrayType*>(array->GetPointeeType()));
	}

	void ConstLayersTest::DeclaredEntitiesTakeTheOutermostConst()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 x;\n"
			"const int32 kLimit = 4;\n"
			"int32 const* fixedPointer = &x;\n"
			"const int32* readOnlyTarget = &x;\n"
			"int32 const*[2] constSlots;\n"
			"const int32*[2] mutableSlots;\n"
			"struct S { public const int32 id; public const int32* view; }\n");
		TEST_ASSERT(IsConstVariable(model, "kLimit"));
		TEST_ASSERT(IsConstVariable(model, "fixedPointer"));
		TEST_ASSERT(!IsConstVariable(model, "readOnlyTarget"));
		TEST_ASSERT(IsConstVariable(model, "constSlots"));
		TEST_ASSERT(!IsConstVariable(model, "mutableSlots"));
		auto s = model.QueryType("S");
		const auto* obj = s.Ok() ? dynamic_cast<const ObjectType*>(s.value) : nullptr;
		TEST_ASSERT(obj && obj->GetFieldByName("id")->m_IsConst && !obj->GetFieldByName("view")->m_IsConst);
	}

	void ConstLayersTest::TrailingConstIsAnError()
	{
		CompilerInstance compiler;
		compiler.RegisterInMemory("t.cf", "int32 x;\nint32* const p = &x;\n");
		compiler.CompileAll();
		bool reported = false;
		for (const auto& e : compiler.GetAllDiagnostics())
			reported = reported || e.message.find("'const' after a type must come before") != Core::String::npos;
		TEST_ASSERT(reported);
	}

	void ConstLayersTest::ConstMethodsAreRecordedAndMangledApart()
	{
		// BUG-109 too: const and noexcept combine.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Box {\n"
			"\tpublic int32 v;\n"
			"\tpublic int32& Get() { return v; }\n"
			"\tpublic const int32& Get() const noexcept { return v; }\n"
			"}\n");
		const FunctionRecord* writable = Method(model, "Box_Get_", false);
		const FunctionRecord* readOnly = Method(model, "Box_Get_", true);
		TEST_ASSERT(writable && readOnly && writable != readOnly);
		TEST_ASSERT(writable->mangledName != readOnly->mangledName);
		TEST_ASSERT(readOnly->isNoexcept && !writable->isNoexcept);
	}

	void ConstLayersTest::ReceiverAccessSelectsTheTwin()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Box {\n"
			"\tpublic int32 v;\n"
			"\tpublic int32& Get() { return v; }\n"
			"\tpublic const int32& Get() const { return v; }\n"
			"\tpublic void Set(int32 n) { v = n; }\n"
			"}\n");
		const Type* box = model.QueryType("Box").value;
		LookupContext writable;
		LookupContext readOnly;
		readOnly.receiver = ReceiverAccess::ReadOnly;
		const FunctionRecord* chosen = nullptr;
		TEST_ASSERT(model.QueryFunction("Get", box, nullptr, {}, writable, &chosen).Ok() && chosen && !chosen->isConst);
		TEST_ASSERT(model.QueryFunction("Get", box, nullptr, {}, readOnly, &chosen).Ok() && chosen && chosen->isConst);
		Core::Vector<const Type*> args;
		args.Push(model.TypeStorePtr()->Builtin(TypeKind::Int32));
		auto set = model.QueryFunction("Set", box, nullptr, args, readOnly);
		TEST_ASSERT(!set.Ok() && set.error == QueryError::ReadOnly);
	}

	void ConstLayersTest::OutOfLineDefinitionMatchesConstness()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Box {\n"
			"\tpublic int32 v;\n"
			"\tpublic int32 Read();\n"
			"\tpublic int32 Read() const;\n"
			"}\n"
			"int32 Box::Read() { return 1; }\n"
			"int32 Box::Read() const { return 2; }\n");
		const FunctionRecord* writable = Method(model, "Box_Read_", false);
		const FunctionRecord* readOnly = Method(model, "Box_Read_", true);
		TEST_ASSERT(writable && readOnly && writable->bodyNode && readOnly->bodyNode && writable->bodyNode != readOnly->bodyNode);
	}
} // namespace Testing
