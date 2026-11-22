#include "FunPtrTypeTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/AstNode.h"
#include "Core/StringFormat.h"

namespace Testing
{
	void FunPtrTypeTest::TestSimpleFunPtrType()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<void(int)>");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::FunPtrTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		TEST_PASSED();
	}

	// BUG-391: funcptr<int32(void)> takes nothing, as C's int32 (*)(void) does.
	void FunPtrTypeTest::TestVoidParameterList()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<int32(void)>");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::FunPtrTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 0);
		TEST_ASSERT(errorTracker.GetErrors().Empty());
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrType()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<float(float a, float b)>");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::FunPtrTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 2);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrTypeInQualType()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<void(int code)>");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto qualType = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(qualType != nullptr);
		TEST_ASSERT(qualType->FuncPtrType() != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrInQualType()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<funcptr<int(float)>(int x)>");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto qualType = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(qualType != nullptr);
		TEST_ASSERT(qualType->FuncPtrType() != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrVariableDefinition()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<void(int)> callback = null;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->GetNameToken().GetText().empty());
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrVariableDefinition()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<funcptr<int(float)>(int x)> higherOrder;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrFunctionParameter()
	{
		auto stream = C4Compiler::Source::FromMemory("void Process(funcptr<void(int)> callback) {}");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrReturnFunction()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<void()> CreateAction() { return null; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrInNamespaceBlock()
	{
		auto stream = C4Compiler::Source::FromMemory("namespace Test { funcptr<void(int)> callback; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::NamespaceNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrFunctionInNamespaceBlock()
	{
		auto stream = C4Compiler::Source::FromMemory("namespace Test { funcptr<void()> CreateAction() { return null; } }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::NamespaceNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestNestedFunPtrInNamespaceBlock()
	{
		auto stream = C4Compiler::Source::FromMemory("namespace Test { funcptr<funcptr<int(float)>(int x)> higherOrder; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::NamespaceNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestFunPtrPointerVariable()
	{
		auto stream = C4Compiler::Source::FromMemory("funcptr<int(int x)>* fnPtr = null;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunPtrTypeTest::TestAliasFunPtrType()
	{
		// Naming a funcptr type is done via alias, not a separate statement
		auto stream = C4Compiler::Source::FromMemory("alias EventHandler = funcptr<void(int)>;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::AliasDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

} // namespace Testing
