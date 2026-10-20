#include "FunPtrTypeTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/AstNode.h"
#include "Core/StringFormat.h"

namespace Testing
{
	void FunPtrTypeTest::TestSimpleFunPtrType()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<void(int)>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunPtrTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrType()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<float(float a, float b)>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunPtrTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 2);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrTypeInQualType()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<void(int code)>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto qualType = Scripting::QualTypeNode::TryParse(state);
		TEST_ASSERT(qualType != nullptr);
		TEST_ASSERT(qualType->FuncPtrType() != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrInQualType()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<funcptr<int(float)>(int x)>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto qualType = Scripting::QualTypeNode::TryParse(state);
		TEST_ASSERT(qualType != nullptr);
		TEST_ASSERT(qualType->FuncPtrType() != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrVariableDefinition()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<void(int)> callback = null;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->GetNameToken().GetText().empty());
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrVariableDefinition()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<funcptr<int(float)>(int x)> higherOrder;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrFunctionParameter()
	{
		auto stream = Scripting::Source::FromMemory("void Process(funcptr<void(int)> callback) {}");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrReturnFunction()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<void()> CreateAction() { return null; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrInNamespaceBlock()
	{
		auto stream = Scripting::Source::FromMemory("namespace Test { funcptr<void(int)> callback; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrFunctionInNamespaceBlock()
	{
		auto stream = Scripting::Source::FromMemory("namespace Test { funcptr<void()> CreateAction() { return null; } }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrInNamespaceBlock()
	{
		auto stream = Scripting::Source::FromMemory("namespace Test { funcptr<funcptr<int(float)>(int x)> higherOrder; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrPointerVariable()
	{
		auto stream = Scripting::Source::FromMemory("funcptr<int(int x)>* fnPtr = null;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestAliasFunPtrType()
	{
		// Naming a funcptr type is done via alias, not a separate statement
		auto stream = Scripting::Source::FromMemory("alias EventHandler = funcptr<void(int)>;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AliasDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

} // namespace Testing
