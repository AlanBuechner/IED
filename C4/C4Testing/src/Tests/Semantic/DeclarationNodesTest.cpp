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
		const C4Compiler::ProgramModel& MakeModel(C4Compiler::CompilerInstance& compiler, const char* source)
		{
			uint32 id = compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetCompilationUnit(id)->GetProgramModel();
		}

		const C4Compiler::ObjectType* ObjectNamed(const C4Compiler::ProgramModel& model, const char* name)
		{
			for (const C4Compiler::ObjectType* object : model.ObjectTypes())
				if (object && object->GetName() == name) return object;
			return nullptr;
		}

		const C4Compiler::FunctionRecord* RecordNamed(const C4Compiler::ProgramModel& model, const char* name)
		{
			Core::Vector<const C4Compiler::FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const C4Compiler::FunctionRecord* record : records)
				if (record->name == name) return record;
			return nullptr;
		}
	}

	void DeclarationNodesTest::ObjectTypeKeepsItsDeclaration()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "namespace Geo { struct Point { public int32 x; } }\n");
		const C4Compiler::ObjectType* point = ObjectNamed(model, "Point");
		TEST_ASSERT(point != nullptr);
		const auto* decl = dynamic_cast<const C4Compiler::ObjectDefinitionNode*>(point->GetDeclNode());
		TEST_ASSERT(decl != nullptr);
		TEST_ASSERT(decl->GetNameToken().GetText() == "Point");
		TEST_PASSED();
	}

	void DeclarationNodesTest::EnumKeepsItsDeclarationAndScope()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "namespace Ui { enum class Mode { A, B } }\n");
		const C4Compiler::EnumType* mode = nullptr;
		for (const C4Compiler::EnumType* en : model.EnumTypes())
			if (en && en->GetName() == "Mode") mode = en;
		TEST_ASSERT(mode != nullptr);
		TEST_ASSERT(dynamic_cast<const C4Compiler::EnumDefinitionNode*>(mode->GetDeclNode()) != nullptr);
		TEST_ASSERT(mode->GetDeclScope() != nullptr);
		TEST_ASSERT(mode->GetDeclScope()->GetName() == "Ui");
		TEST_PASSED();
	}

	void DeclarationNodesTest::TemplateInstanceKeepsTheTemplatesDeclaration()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "template<T> struct Box { public T v; }\nBox<int32> g_box;\n");
		const C4Compiler::ObjectType* instance = nullptr;
		for (const C4Compiler::ObjectType* object : model.ObjectTypes())
			if (object && object->GetName() == "Box" && !object->GetTemplateArguments().Empty()) instance = object;
		TEST_ASSERT(instance != nullptr);
		const auto* decl = dynamic_cast<const C4Compiler::ObjectDefinitionNode*>(instance->GetDeclNode());
		TEST_ASSERT(decl != nullptr);
		TEST_ASSERT(decl->GetNameToken().GetText() == "Box");
		TEST_PASSED();
	}

	void DeclarationNodesTest::FunctionKeepsEveryDeclaration()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "int32 Twice(int32 a);\nint32 Twice(int32 a) { return a * 2; }\n");
		const C4Compiler::FunctionRecord* twice = RecordNamed(model, "Twice");
		TEST_ASSERT(twice != nullptr);
		TEST_ASSERT_EQ((int)twice->declarations.Count(), 2);
		TEST_ASSERT(dynamic_cast<const C4Compiler::FunctionDefinitionNode*>(twice->declarations[0]) != nullptr);
		TEST_ASSERT(twice->declarations[0]->startLocation.line == 1);
		TEST_ASSERT(twice->declarations[1]->startLocation.line == 2);
		TEST_PASSED();
	}

	void DeclarationNodesTest::MethodKeepsInClassAndOutOfLineDeclarations()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Counter { public int32 n; public int32 Get(); }\nint32 Counter::Get() { return n; }\n");
		const C4Compiler::FunctionRecord* get = RecordNamed(model, "Get");
		TEST_ASSERT(get != nullptr);
		TEST_ASSERT_EQ((int)get->declarations.Count(), 2);
		TEST_PASSED();
	}
}
