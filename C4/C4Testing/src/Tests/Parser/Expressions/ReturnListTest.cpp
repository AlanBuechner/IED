#include "ReturnListTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunPtrTypeNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"
#include "Parser/Nodes/Expressions/ValueListNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/DestructuringNode.h"
#include "Parser/Nodes/Statements/ExpressionStatementNode.h"
#include <memory>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// The lexer, error tracker and source outlive the state that refers to them.
		struct Source4
		{
			Ref<Source> stream;
			Lexer lexer;
			ErrorTracker errors;
			std::unique_ptr<ParserState> state;

			explicit Source4(const std::string& text)
				: stream(Source::FromMemory(text)), lexer(*stream, 0), state(std::make_unique<ParserState>(lexer, errors)) {}
		};

		template<typename T> const T* As(const Ref<AstNode>& node) { return dynamic_cast<const T*>(node.Get()); }
	}

	void ReturnListTest::ReturnTypeIsAList()
	{
		Source4 s("[float, bool] Parse(int32 v);");
		auto fn = FunctionDefinitionNode::TryParse(*s.state);
		TEST_ASSERT(fn && fn->GetReturnType() && fn->GetReturnType()->IsReturnList());
		TEST_ASSERT(fn->GetReturnType()->GetReturnList().Count() == 2 && fn->GetAnnotations().Empty());
		TEST_ASSERT(fn->GetReturnType()->ToString() == "[float, bool]");
		TEST_ASSERT(s.errors.GetErrors().Empty());
	}

	void ReturnListTest::AnnotationsBeforeAList()
	{
		Source4 s("[test(\"a\")] [int32, int32] Box::Pair();");
		auto fn = FunctionDefinitionNode::TryParse(*s.state);
		TEST_ASSERT(fn && fn->GetAnnotations().Count() == 1 && fn->GetReturnType()->IsReturnList());
		TEST_ASSERT(fn->GetName()->SegmentCount() == 2);
	}

	void ReturnListTest::AnnotationIsNotAList()
	{
		Source4 s("[operator(\"create\")] void Init(int32 v);");
		auto fn = FunctionDefinitionNode::TryParse(*s.state);
		TEST_ASSERT(fn && fn->GetAnnotations().Count() == 1 && !fn->GetReturnType()->IsReturnList());
	}

	void ReturnListTest::FunPtrReturnsAList()
	{
		Source4 s("funcptr<[int32, bool](float)>");
		auto type = QualTypeNode::TryParse(*s.state);
		TEST_ASSERT(type && type->FuncPtrType());
		const auto* fp = As<FunPtrTypeNode>(type->FuncPtrType());
		TEST_ASSERT(fp && fp->GetReturnType()->IsReturnList() && fp->ParameterCount() == 1);
	}

	void ReturnListTest::PackInAList()
	{
		Source4 s("template<...Ts> [int32, Ts...] Pack(Ts... args);");
		auto fn = FunctionDefinitionNode::TryParse(*s.state);
		TEST_ASSERT(fn && fn->GetReturnType()->IsReturnList());
		const auto& list = fn->GetReturnType()->GetReturnList();
		TEST_ASSERT(list.Count() == 2 && !list[0]->ExpandsPack() && list[1]->ExpandsPack());
	}

	void ReturnListTest::ValueListExpression()
	{
		Source4 s("[a, b + 1]");
		auto node = PrattParser::ParseExpression(*s.state);
		const auto* list = As<ValueListNode>(node);
		TEST_ASSERT(list && list->GetElements().Count() == 2 && As<BinaryExpressionNode>(list->GetElements()[1]));
	}

	void ReturnListTest::LambdaReturnsAList()
	{
		Source4 s("[x](int32 a) -> [int32, int32] { return [a, x]; }");
		auto node = PrattParser::ParseExpression(*s.state);
		const auto* lambda = As<LambdaNode>(node);
		TEST_ASSERT(lambda && lambda->GetCaptures().Count() == 1);
		const auto* returned = As<QualTypeNode>(lambda->GetReturnType());
		TEST_ASSERT(returned && returned->IsReturnList());
	}

	void ReturnListTest::DestructuringElementKinds()
	{
		Source4 s("{ [float a, b, _] = F(); }");
		auto block = CodeBlockNode::TryParse(*s.state);
		TEST_ASSERT(block && block->GetStatements().Count() == 1);
		const auto* d = As<DestructuringNode>(block->GetStatements()[0]);
		TEST_ASSERT(d && d->GetElements().Count() == 3 && d->GetValue());
		using Kind = DestructuringNode::Element::Kind;
		TEST_ASSERT(d->GetElements()[0].kind == Kind::Declare && d->GetElements()[0].name.GetText() == "a");
		TEST_ASSERT(d->GetElements()[1].kind == Kind::Assign && d->GetElements()[1].target);
		TEST_ASSERT(d->GetElements()[2].kind == Kind::Discard);
		TEST_ASSERT(s.errors.GetErrors().Empty());
	}

	void ReturnListTest::ComparisonIsNoDestructuring()
	{
		// `[` ... `]` followed by `==` compares; only a single `=` destructures.
		Source4 s("{ [a, b] == x; }");
		auto block = CodeBlockNode::TryParse(*s.state);
		TEST_ASSERT(block && block->GetStatements().Count() == 1);
		TEST_ASSERT(As<DestructuringNode>(block->GetStatements()[0]) == nullptr);
	}
} // namespace Testing
