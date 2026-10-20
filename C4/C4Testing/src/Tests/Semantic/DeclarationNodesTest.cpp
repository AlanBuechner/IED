#include "DeclarationNodesTest.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"

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

		const Scripting::ObjectType* ObjectNamed(const Scripting::ProgramModel& model, const char* name)
		{
			for (const Scripting::ObjectType* object : model.ObjectTypes())
				if (object && object->GetName() == name) return object;
			return nullptr;
		}

		const Scripting::FunctionRecord* RecordNamed(const Scripting::ProgramModel& model, const char* name)
		{
			Core::Vector<const Scripting::FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const Scripting::FunctionRecord* record : records)
				if (record->name == name) return record;
			return nullptr;
		}
	}

	void DeclarationNodesTest::ObjectTypeKeepsItsDeclaration()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "namespace Geo { struct Point { public int32 x; } }\n");
		const Scripting::ObjectType* point = ObjectNamed(model, "Point");
		TEST_ASSERT(point != nullptr);
		const auto* decl = dynamic_cast<const Scripting::ObjectDefinitionNode*>(point->GetDeclNode());
		TEST_ASSERT(decl != nullptr);
		TEST_ASSERT(decl->GetNameToken().GetText() == "Point");
		TEST_PASSED();
	}

	void DeclarationNodesTest::EnumKeepsItsDeclarationAndScope()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "namespace Ui { enum class Mode { A, B } }\n");
		const Scripting::EnumType* mode = nullptr;
		for (const Scripting::EnumType* en : model.EnumTypes())
			if (en && en->GetName() == "Mode") mode = en;
		TEST_ASSERT(mode != nullptr);
		TEST_ASSERT(dynamic_cast<const Scripting::EnumDefinitionNode*>(mode->GetDeclNode()) != nullptr);
		TEST_ASSERT(mode->GetDeclScope() != nullptr);
		TEST_ASSERT(mode->GetDeclScope()->GetName() == "Ui");
		TEST_PASSED();
	}

	void DeclarationNodesTest::TemplateInstanceKeepsTheTemplatesDeclaration()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "template<T> struct Box { public T v; }\nBox<int32> g_box;\n");
		const Scripting::ObjectType* instance = nullptr;
		for (const Scripting::ObjectType* object : model.ObjectTypes())
			if (object && object->GetName() == "Box" && !object->GetTemplateArguments().Empty()) instance = object;
		TEST_ASSERT(instance != nullptr);
		const auto* decl = dynamic_cast<const Scripting::ObjectDefinitionNode*>(instance->GetDeclNode());
		TEST_ASSERT(decl != nullptr);
		TEST_ASSERT(decl->GetNameToken().GetText() == "Box");
		TEST_PASSED();
	}

	void DeclarationNodesTest::FunctionKeepsEveryDeclaration()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "int32 Twice(int32 a);\nint32 Twice(int32 a) { return a * 2; }\n");
		const Scripting::FunctionRecord* twice = RecordNamed(model, "Twice");
		TEST_ASSERT(twice != nullptr);
		TEST_ASSERT_EQ((int)twice->declarations.Count(), 2);
		TEST_ASSERT(dynamic_cast<const Scripting::FunctionDefinitionNode*>(twice->declarations[0]) != nullptr);
		TEST_ASSERT(twice->declarations[0]->startLocation.line == 1);
		TEST_ASSERT(twice->declarations[1]->startLocation.line == 2);
		TEST_PASSED();
	}

	void DeclarationNodesTest::MethodKeepsInClassAndOutOfLineDeclarations()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Counter { public int32 n; public int32 Get(); }\nint32 Counter::Get() { return n; }\n");
		const Scripting::FunctionRecord* get = RecordNamed(model, "Get");
		TEST_ASSERT(get != nullptr);
		TEST_ASSERT_EQ((int)get->declarations.Count(), 2);
		TEST_PASSED();
	}
}
