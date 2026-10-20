#include "MultipleReturnTest.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Compiler/ErrorTracker.h"
#include "Model/TypeHandle.h"
#include <cstring>

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

		const FunctionRecord* Resolve(const ProgramModel& model, const char* name, Core::Vector<const Type*> args)
		{
			const FunctionRecord* rec = nullptr;
			auto r = model.QueryFunction(name, nullptr, nullptr, args, {}, &rec);
			return r.Ok() ? rec : nullptr;
		}

		const ObjectType* ReturnedList(const FunctionRecord* rec)
		{
			const auto* list = rec ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(rec->type->GetReturnType())) : nullptr;
			return list && list->IsReturnList() ? list : nullptr;
		}

		int Count(const HIRFunction& fn, HIRNode::Kind kind)
		{
			int count = 0;
			for (const auto& node : fn.m_Nodes) count += node->kind == kind ? 1 : 0;
			return count;
		}
	}

	void MultipleReturnTest::ListTypesAreInternedStructs()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"[int32, bool] A() { return [1, true]; }\n"
			"[int32, bool] B() { return [2, false]; }\n"
			"[bool, int32] C() { return [true, 3]; }\n");
		const ObjectType* a = ReturnedList(Resolve(model, "A", {}));
		const ObjectType* b = ReturnedList(Resolve(model, "B", {}));
		const ObjectType* c = ReturnedList(Resolve(model, "C", {}));
		TEST_ASSERT(a && a == b && c && c != a);
		TEST_ASSERT(a->GetFields().Count() == 2 && a->GetName() == "[int32, bool]");
		TEST_ASSERT(a->GetFields()[0].m_Type == model.TypeStorePtr()->Builtin(TypeKind::Int32));
		TEST_ASSERT_EQ((int)a->GetSize(), 8); // a C struct: int32, then bool, padded to 4
	}

	void MultipleReturnTest::TemplateListResolvesInBindings()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "template<T> [T, bool] Find(T v) { return [v, true]; }\n");
		const ObjectType* list = ReturnedList(Resolve(model, "Find", { model.TypeStorePtr()->Builtin(TypeKind::Int64) }));
		TEST_ASSERT(list && list->GetFields()[0].m_Type == model.TypeStorePtr()->Builtin(TypeKind::Int64));
	}

	void MultipleReturnTest::PackExpandsInAList()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "template<...Ts> [int8, Ts...] Pack(Ts... a) { return [(int8)0, a...]; }\n");
		TypeStore* ts = model.TypeStorePtr();
		const ObjectType* list = ReturnedList(Resolve(model, "Pack", { ts->Builtin(TypeKind::Int32), ts->Builtin(TypeKind::Float32) }));
		TEST_ASSERT(list && list->GetFields().Count() == 3);
		TEST_ASSERT(list->GetFields()[1].m_Type == ts->Builtin(TypeKind::Int32) && list->GetFields()[2].m_Type == ts->Builtin(TypeKind::Float32));
	}

	void MultipleReturnTest::DestructuringMovesObjectsOut()
	{
		// The list is held once; an object element is taken out of it (relocation), never copied.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Res { public int32 id; [operator(\"copy\")] public void Copy(const Res& o) { id = o.id; } [operator(\"delete\")] public void Delete() {} }\n"
			"[Res, int32] Make() { Res r; return [r, 2]; }\n"
			"int32 F() { [Res r, int32 n] = Make(); return n; }\n");
		ErrorTracker errors;
		const FunctionRecord* f = Resolve(model, "F", {});
		HIRFunction fn = LowerFunction(model, errors, f);
		TEST_ASSERT(fn.complete && errors.GetErrors().Empty());
		int copies = 0;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::Call && static_cast<const CallNode*>(node.get())->callee &&
				static_cast<const CallNode*>(node.get())->callee->name == "Copy") ++copies;
		TEST_ASSERT_EQ(copies, 0);
		TEST_ASSERT(Count(fn, HIRNode::Kind::LocalDecl) >= 2);
	}
} // namespace Testing
