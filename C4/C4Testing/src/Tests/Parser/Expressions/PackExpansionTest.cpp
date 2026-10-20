#include "PackExpansionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"
#include <memory>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// The lexer, error tracker and source outlive the state that refers to them.
		struct Parsed
		{
			Ref<Source> stream;
			Lexer lexer;
			ErrorTracker errors;
			std::unique_ptr<ParserState> state;
			Ref<AstNode> node;

			explicit Parsed(const std::string& source)
				: stream(Source::FromMemory(source)), lexer(*stream, 0),
				  state(std::make_unique<ParserState>(lexer, errors)), node(PrattParser::ParseExpression(*state)) {}
		};

		template<typename T> const T* As(const Ref<AstNode>& node) { return dynamic_cast<const T*>(node.Get()); }

		Core::String NameOf(const Ref<AstNode>& node)
		{
			const auto* id = As<IdentifierExpressionNode>(node);
			return id ? id->GetName() : Core::String();
		}
	}

	void PackExpansionTest::ExpansionInCallArguments()
	{
		Parsed p("G(1, args...)");
		const auto* call = As<FunctionCallNode>(p.node);
		TEST_ASSERT(call && call->GetArguments().Count() == 2);
		const auto* expansion = As<PackExpansionNode>(call->GetArguments()[1]);
		TEST_ASSERT(expansion && NameOf(expansion->GetPattern()) == "args");
		TEST_ASSERT(p.errors.GetErrors().Empty());
	}

	void PackExpansionTest::PatternExpansion()
	{
		Parsed p("G(Wrap(args)...)");
		const auto* call = As<FunctionCallNode>(p.node);
		TEST_ASSERT(call && call->GetArguments().Count() == 1);
		const auto* expansion = As<PackExpansionNode>(call->GetArguments()[0]);
		TEST_ASSERT(expansion != nullptr);
		const auto* pattern = As<FunctionCallNode>(expansion->GetPattern());
		TEST_ASSERT(pattern && NameOf(pattern->GetCallee()) == "Wrap" && NameOf(pattern->GetArguments()[0]) == "args");
	}

	void PackExpansionTest::ExpansionInBracedList()
	{
		Parsed p("{ 0, args... }");
		const auto* brace = As<BracedInitializerNode>(p.node);
		TEST_ASSERT(brace && brace->GetEntries().Count() == 2);
		TEST_ASSERT(As<PackExpansionNode>(brace->GetEntries()[1].m_Value) != nullptr);
	}

	void PackExpansionTest::UnaryFolds()
	{
		Parsed right("(args + ...)");
		const auto* r = As<FoldExpressionNode>(right.node);
		TEST_ASSERT(r && r->GetOperatorToken().GetText() == "+" && NameOf(r->GetFirst()) == "args" && !r->GetLast());
		Parsed left("(... && args)");
		const auto* l = As<FoldExpressionNode>(left.node);
		TEST_ASSERT(l && l->GetOperatorToken().GetText() == "&&" && !l->GetFirst() && NameOf(l->GetLast()) == "args");
	}

	void PackExpansionTest::BinaryFolds()
	{
		Parsed right("(args * ... * 1)");
		const auto* r = As<FoldExpressionNode>(right.node);
		TEST_ASSERT(r && r->GetOperatorToken().GetText() == "*" && NameOf(r->GetFirst()) == "args" && r->GetLast());
		Parsed left("(0 - ... - args)");
		const auto* l = As<FoldExpressionNode>(left.node);
		TEST_ASSERT(l && l->GetOperatorToken().GetText() == "-" && l->GetFirst() && NameOf(l->GetLast()) == "args");
		// An operand binds tighter than the fold's operator: `(a * b + ...)` folds `a * b`.
		Parsed operand("(a * b + ...)");
		const auto* o = As<FoldExpressionNode>(operand.node);
		TEST_ASSERT(o && o->GetOperatorToken().GetText() == "+" && As<BinaryExpressionNode>(o->GetFirst()));
	}

	void PackExpansionTest::CommaFold()
	{
		Parsed p("(Record(args), ...)");
		const auto* fold = As<FoldExpressionNode>(p.node);
		TEST_ASSERT(fold && fold->GetOperatorToken().GetText() == "," && As<FunctionCallNode>(fold->GetFirst()));
		TEST_ASSERT(p.errors.GetErrors().Empty());
	}

	void PackExpansionTest::ParenthesesWithoutEllipsisAreNoFold()
	{
		Parsed p("(a + b)");
		TEST_ASSERT(As<ParenthesizedExpressionNode>(p.node) != nullptr);
	}

	void PackExpansionTest::SizeOfPack()
	{
		Parsed p("sizeof...(Ts)");
		const auto* typeOf = As<TypeOfExpressionNode>(p.node);
		TEST_ASSERT(typeOf && typeOf->GetKind() == TypeOfExpressionNode::Kind::SizeOfPack && typeOf->GetPackName().GetText() == "Ts");
	}

	void PackExpansionTest::MemberTemplateCallWithArguments()
	{
		Parsed p("box.Get<int32>(1)");
		const auto* call = As<FunctionCallNode>(p.node);
		TEST_ASSERT(call && call->HasTemplateArgs() && call->GetTemplateArgs()->ArgumentCount() == 1);
		const auto* member = As<MemberAccessNode>(call->GetCallee());
		TEST_ASSERT(member && member->GetMemberNameToken().GetText() == "Get" && NameOf(member->GetBaseObject()) == "box");
	}

	void PackExpansionTest::MemberComparisonIsNoCall()
	{
		Parsed p("box.size < limit");
		const auto* compare = As<BinaryExpressionNode>(p.node);
		TEST_ASSERT(compare && compare->GetOperatorToken().GetText() == "<" && As<MemberAccessNode>(compare->GetLeftOperand()));
	}
} // namespace Testing
