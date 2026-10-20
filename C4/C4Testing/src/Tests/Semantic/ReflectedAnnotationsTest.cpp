#include "ReflectedAnnotationsTest.h"
#include "Model/Annotations.h"
#include "Model/Types/ObjectType.h"

namespace Testing
{
	namespace
	{
		const Scripting::ProgramModel& MakeModel(Scripting::CompilerInstance& compiler, const char* source)
		{
			uint32 id = compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetCompilationUnit(id)->GetProgramModel();
		}

		Scripting::QueryResult<Core::Vector<Scripting::ReflectedAnnotation>> TypeAnnotations(const Scripting::ProgramModel& model, const char* name)
		{
			for (const Scripting::ObjectType* object : model.ObjectTypes())
				if (object && object->GetName() == name)
				{
					Core::Vector<const Scripting::AstNode*> declarations;
					declarations.Push(object->GetDeclNode());
					return Scripting::ReflectedAnnotations(model, declarations, model.TypeScope(object));
				}
			return Scripting::QueryResult<Core::Vector<Scripting::ReflectedAnnotation>>::Fail(Scripting::QueryError::NotFound, name);
		}
	}

	void ReflectedAnnotationsTest::ArgumentsFold()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace L { const int32 kMax = 10; }\n"
			"[Range(-1, L::kMax * 2), Name(\"x\"), On(true), Scale(0.25)] struct S { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "S");
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT_EQ((int)annotations.value.Count(), 4);
		const auto& range = annotations.value[0];
		TEST_ASSERT(range.name == "Range");
		TEST_ASSERT(range.arguments[0].kind == Scripting::ConstantValue::Kind::Int64 && range.arguments[0].intValue == -1);
		TEST_ASSERT(range.arguments[1].intValue == 20);
		TEST_ASSERT(annotations.value[1].arguments[0].kind == Scripting::ConstantValue::Kind::String);
		TEST_ASSERT(annotations.value[1].arguments[0].stringValue == "x");
		TEST_ASSERT(annotations.value[2].arguments[0].kind == Scripting::ConstantValue::Kind::Bool && annotations.value[2].arguments[0].booleanValue);
		TEST_ASSERT(annotations.value[3].arguments[0].kind == Scripting::ConstantValue::Kind::Float64 && annotations.value[3].arguments[0].floatValue == 0.25);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::CompilerAnnotationsAreSkipped()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "[group(\"g\"), Tag(1)] struct T { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "T");
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT_EQ((int)annotations.value.Count(), 1);
		TEST_ASSERT(annotations.value[0].name == "Tag");
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::NonConstantArgumentFails()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "int32 g_level = 3;\n[Range(0, g_level)] struct U { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "U");
		TEST_ASSERT(!annotations.Ok());
		TEST_ASSERT(annotations.message.find("must be a constant") != Core::String::npos);
		TEST_ASSERT_EQ((int)annotations.loc.line, 2);
		TEST_ASSERT_EQ((int)annotations.loc.column, 11);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::DeclarationThenDefinition()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "[Declared] int32 F();\n[Defined(2)] int32 F() { return 1; }\n");
		Core::Vector<const Scripting::FunctionRecord*> records;
		model.AllFunctionRecords(records);
		const Scripting::FunctionRecord* f = nullptr;
		for (const Scripting::FunctionRecord* record : records)
			if (record->name == "F") f = record;
		TEST_ASSERT(f != nullptr);
		auto annotations = Scripting::ReflectedAnnotations(model, f->declarations, f->scope);
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT_EQ((int)annotations.value.Count(), 2);
		TEST_ASSERT(annotations.value[0].name == "Declared");
		TEST_ASSERT(annotations.value[1].name == "Defined");
		TEST_ASSERT(annotations.value[1].arguments[0].intValue == 2);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::NoAnnotations()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct V { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "V");
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT(annotations.value.Empty());
		Core::Vector<const Scripting::AstNode*> none;
		TEST_ASSERT(Scripting::ReflectedAnnotations(model, none, nullptr).value.Empty());
		TEST_PASSED();
	}
}
