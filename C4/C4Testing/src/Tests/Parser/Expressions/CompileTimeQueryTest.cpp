#include "CompileTimeQueryTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		struct Parsed
		{
			Ref<AstNode> node;
			std::string firstError;
		};

		Parsed Parse(const std::string& source)
		{
			Ref<Source> stream = Source::FromMemory(source);
			Lexer lexer(*stream, 0);
			ErrorTracker errors;
			ParserState state(lexer, errors);
			Parsed parsed;
			parsed.node = PrattParser::ParseExpression(state);
			if (!errors.GetErrors().Empty()) parsed.firstError = errors.GetErrors()[0].message.c_str();
			return parsed;
		}

		const TypeOfExpressionNode* Query(const Parsed& parsed)
		{
			return dynamic_cast<const TypeOfExpressionNode*>(parsed.node.Get());
		}
	}

	void CompileTimeQueryTest::OperandForms()
	{
		using Kind = TypeOfExpressionNode::Kind;
		const Parsed name = Parse("sizeof(x)");
		TEST_ASSERT(Query(name) && Query(name)->GetKind() == Kind::SizeOf);
		TEST_ASSERT(Query(name)->GetTargetType() && Query(name)->GetOperand()); // a type or a value: the model decides
		const Parsed pointer = Parse("sizeof(int32*)");
		TEST_ASSERT(Query(pointer) && Query(pointer)->GetTargetType() && !Query(pointer)->GetOperand());
		const Parsed sum = Parse("sizeof(a + b)");
		TEST_ASSERT(Query(sum) && !Query(sum)->GetTargetType() && Query(sum)->GetOperand());
		const Parsed bare = Parse("sizeof value");
		TEST_ASSERT(Query(bare) && !Query(bare)->GetTargetType() && Query(bare)->GetOperand());
		const Parsed align = Parse("alignof(Point)");
		TEST_ASSERT(Query(align) && Query(align)->GetKind() == Kind::AlignOf && Query(align)->GetTargetType());
		const Parsed uuid = Parse("__uuidof(IUnknown)");
		TEST_ASSERT(Query(uuid) && Query(uuid)->GetKind() == Kind::UuidOf && !Query(uuid)->GetOperand());
		TEST_ASSERT(name.firstError.empty() && sum.firstError.empty() && bare.firstError.empty());
	}

	void CompileTimeQueryTest::OffsetofPathsAndPacks()
	{
		const Parsed offset = Parse("offsetof(Outer, inner.value)");
		TEST_ASSERT(Query(offset) && Query(offset)->GetKind() == TypeOfExpressionNode::Kind::OffsetOf);
		TEST_ASSERT(Query(offset)->GetFieldPath().Count() == 2);
		TEST_ASSERT(Query(offset)->ToString() == "offsetof(Outer, inner.value)");
		const Parsed pack = Parse("sizeof...(Ts)");
		TEST_ASSERT(Query(pack) && Query(pack)->GetKind() == TypeOfExpressionNode::Kind::SizeOfPack);
		TEST_ASSERT(Query(pack)->GetPackName().GetText() == "Ts");
	}

	void CompileTimeQueryTest::SizeofBindsLikeAPrefixOperator()
	{
		const Parsed product = Parse("sizeof x * 2");
		const auto* binary = dynamic_cast<const BinaryExpressionNode*>(product.node.Get());
		TEST_ASSERT(binary && binary->GetOperatorToken().GetText() == "*");
		TEST_ASSERT(dynamic_cast<const TypeOfExpressionNode*>(binary->GetLeftOperand().Get()));
		const Parsed member = Parse("sizeof o.inner");
		TEST_ASSERT(Query(member) && Query(member)->GetOperand() && Query(member)->GetOperand()->ToString().find("inner") != Core::String::npos);
	}

	void CompileTimeQueryTest::MalformedQueriesAreErrors()
	{
		TEST_ASSERT(Parse("offsetof(Outer)").firstError.find("Expected '(Type, field)' after 'offsetof'") == 0);
		TEST_ASSERT(Parse("offsetof(Outer, )").firstError == "offsetof needs a field name, got ')'");
		TEST_ASSERT(Parse("offsetof(Outer, a b)").firstError == "Expected ')' to close offsetof, got 'b'");
		TEST_ASSERT(Parse("__uuidof(1 + 2)").firstError.find("Expected '(Type)' after '__uuidof'") == 0);
		TEST_ASSERT(Parse("sizeof...(1)").firstError == "Expected '(pack)' after 'sizeof...'");
	}
} // namespace Testing
