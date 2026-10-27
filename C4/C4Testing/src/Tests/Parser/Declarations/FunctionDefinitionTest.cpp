#include "FunctionDefinitionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"

namespace Testing
{
	void FunctionDefinitionTest::TestSimpleDeclaration()
	{
		auto stream = Scripting::Source::FromMemory("void DoThing();");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsDeclaration());
		TEST_ASSERT(!node->IsDefinition());
		TEST_ASSERT(node->GetAccessModifier() == Scripting::AccessModifier::None);
		TEST_PASSED();
	}

	namespace
	{
		struct ParsedFunction
		{
			Ref<Scripting::FunctionDefinitionNode> node;
			uint32 errors = 0;
		};

		ParsedFunction ParseFunction(const char* source)
		{
			auto stream = Scripting::Source::FromMemory(source);
			Scripting::Lexer lexer(*stream, 0);
			Scripting::ErrorTracker errorTracker;
			Scripting::ParserState state(lexer, errorTracker);
			ParsedFunction parsed;
			parsed.node = Scripting::FunctionDefinitionNode::TryParse(state);
			parsed.errors = errorTracker.GetErrors().Count();
			return parsed;
		}
	}

	void FunctionDefinitionTest::TestFinalEitherSide()
	{
		ParsedFunction leading = ParseFunction("public final int32 Value() const override { return 1; }");
		TEST_ASSERT(leading.node != nullptr);
		TEST_ASSERT(leading.node->IsFinal() && leading.node->IsOverride() && leading.node->IsConstMethod());
		TEST_ASSERT(leading.errors == 0);
		ParsedFunction trailing = ParseFunction("virtual int32 Value() const final { return 1; }");
		TEST_ASSERT(trailing.node != nullptr);
		TEST_ASSERT(trailing.node->IsFinal() && trailing.node->IsVirtual() && trailing.node->IsConstMethod());
		TEST_ASSERT(trailing.errors == 0);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestAbstractIsPureVirtual()
	{
		ParsedFunction parsed = ParseFunction("public abstract float Area() const;");
		TEST_ASSERT(parsed.node != nullptr);
		TEST_ASSERT(parsed.node->IsAbstract());
		TEST_ASSERT(parsed.node->IsVirtual());
		TEST_ASSERT(parsed.node->IsPureVirtual());
		TEST_ASSERT(parsed.node->IsDeclaration());
		TEST_ASSERT(parsed.errors == 0);
		TEST_PASSED();
	}

	// BUG-391: C's `(void)` alone is an empty parameter list; a void among parameters stays a parameter (the model
	// refuses it).
	void FunctionDefinitionTest::TestVoidParameterListIsEmpty()
	{
		ParsedFunction empty = ParseFunction("int32 Answer(void) { return 42; }");
		TEST_ASSERT(empty.node != nullptr);
		TEST_ASSERT_EQ(empty.node->GetParameters().Count(), 0u);
		TEST_ASSERT(empty.errors == 0);
		ParsedFunction declared = ParseFunction("int32 Answer( void );");
		TEST_ASSERT(declared.node != nullptr && declared.node->GetParameters().Count() == 0 && declared.node->IsDeclaration());
		ParsedFunction named = ParseFunction("int32 Measure(int32 width, void height);");
		TEST_ASSERT(named.node != nullptr);
		TEST_ASSERT_EQ(named.node->GetParameters().Count(), 2u);
		ParsedFunction pointer = ParseFunction("int32 Read(void* data) { return 0; }");
		TEST_ASSERT(pointer.node != nullptr && pointer.node->GetParameters().Count() == 1);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestRejectedModifiers()
	{
		const char* sources[] = { "abstract float Area() { return 1.0f; }", "static abstract float Area();", "concurrent void Work() { }" };
		for (const char* source : sources)
		{
			ParsedFunction parsed = ParseFunction(source);
			TEST_ASSERT(parsed.node != nullptr);
			TEST_ASSERT(parsed.errors == 1);
		}
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestWithBody()
	{
		auto stream = Scripting::Source::FromMemory("void DoThing() { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsDefinition());
		TEST_ASSERT(!node->IsDeclaration());
		TEST_ASSERT(node->GetBody() != nullptr);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestWithAccessModifier()
	{
		auto stream = Scripting::Source::FromMemory("public void DoThing();");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAccessModifier() == Scripting::AccessModifier::Public);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestStaticFunction()
	{
		auto stream = Scripting::Source::FromMemory("static void DoThing();");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsStatic());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestVirtualFunction()
	{
		auto stream = Scripting::Source::FromMemory("virtual void DoThing();");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsVirtual());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestWithParameters()
	{
		auto stream = Scripting::Source::FromMemory("void DoThing(int i, float f);");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 2);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestWithTemplate()
	{
		auto stream = Scripting::Source::FromMemory("template<T> void DoThing();");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsTemplated());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestWithAnnotation()
	{
		auto stream = Scripting::Source::FromMemory("[Test] void DoThing();");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAnnotations().Count() == 1);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestConstQualifier()
	{
		auto stream = Scripting::Source::FromMemory("void DoThing() const;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsConstMethod() && !node->IsNoexcept());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestNoexceptQualifier()
	{
		auto stream = Scripting::Source::FromMemory("void DoThing() noexcept;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsNoexcept() && !node->IsConstMethod());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestQualifiersCombine()
	{
		// BUG-109: the qualifiers were one enum, so a later one replaced an earlier one.
		auto stream = Scripting::Source::FromMemory("virtual int32 Get() const noexcept = 0;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsConstMethod() && node->IsNoexcept() && node->IsPureVirtual());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestPureVirtual()
	{
		auto stream = Scripting::Source::FromMemory("virtual void DoThing() = 0;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsVirtual());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestVariadic()
	{
		auto stream = Scripting::Source::FromMemory("void Printf(const char* fmt, ...);");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsVariadic());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestComplexQualifiedName()
	{
		auto stream = Scripting::Source::FromMemory("void NC::SomeClass<float>::DoThing();");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestNoMatch()
	{
		auto stream = Scripting::Source::FromMemory("while (x > 0) { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestParameterPack()
	{
		auto stream = Scripting::Source::FromMemory("template<T, ...Ts> int32 F(T first, Ts... rest);");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr && node->GetParameters().Count() == 2);
		TEST_ASSERT(!node->GetParameters()[0]->IsPack() && node->GetParameters()[1]->IsPack());
		TEST_ASSERT(node->GetParameters()[1]->GetNameToken().GetText() == "rest");
		TEST_ASSERT(!node->GetOwnerTemplateDefinition());
		TEST_PASSED();
	}

	void FunctionDefinitionTest::TestOwnerTemplateHeader()
	{
		// BUG-117: an out-of-line member template of a class template has the class's header, then its own.
		auto stream = Scripting::Source::FromMemory("template<T> template<U> U Box<T>::Get(U u) { return u; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr && node->GetOwnerTemplateDefinition() && node->GetTemplateDefinition());
		TEST_ASSERT(node->GetOwnerTemplateDefinition()->GetParameter(0)->GetName() == "T");
		TEST_ASSERT(node->GetTemplateDefinition()->GetParameter(0)->GetName() == "U");
		TEST_ASSERT(node->GetName()->SegmentCount() == 2);
		TEST_PASSED();
	}
} // namespace Testing
