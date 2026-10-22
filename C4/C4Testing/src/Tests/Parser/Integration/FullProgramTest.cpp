#include "FullProgramTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/SafeCastNode.h"
#include "Parser/Nodes/Expressions/TypeCheckNode.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <typeinfo>
#include <vector>

namespace Testing
{
	namespace
	{
		using Listed = std::vector<const Scripting::AstNode*>;

		Listed Present(std::initializer_list<const Scripting::AstNode*> parts)
		{
			Listed held;
			for (const Scripting::AstNode* part : parts)
				if (part) held.push_back(part);
			return held;
		}

		// What a node the parser fills through setters holds, in Children's order; none for another kind.
		std::optional<Listed> Held(const Scripting::AstNode& node)
		{
			using namespace Scripting;
			if (const auto* call = dynamic_cast<const FunctionCallNode*>(&node))
			{
				Listed held = Present({ call->GetCallee().Get() });
				for (const Ref<AstNode>& argument : call->GetArguments()) held.push_back(argument.Get());
				return held;
			}
			if (const auto* cast = dynamic_cast<const CastExpressionNode*>(&node)) return Present({ cast->GetOperand().Get(), cast->GetTargetType().Get() });
			if (const auto* as = dynamic_cast<const SafeCastNode*>(&node)) return Present({ as->GetOperand().Get(), as->GetTargetType().Get() });
			if (const auto* is = dynamic_cast<const TypeCheckNode*>(&node)) return Present({ is->GetOperand().Get(), is->GetTargetType().Get() });
			if (const auto* size = dynamic_cast<const TypeOfExpressionNode*>(&node))
				return size->GetTargetType() ? Present({ size->GetTargetType().Get() }) : Present({ size->GetOperand().Get() });
			return std::nullopt;
		}

		// Every node under `node`: the children of a kind Held knows are what it holds, and asking again gives the same.
		void CheckChildren(const Scripting::AstNode& node, std::set<std::string>& kinds, int& wrong)
		{
			const std::span<const Ref<Scripting::AstNode>> children = node.Children();
			Listed listed;
			for (const Ref<Scripting::AstNode>& child : children) listed.push_back(child.Get());
			if (const std::optional<Listed> held = Held(node))
			{
				kinds.insert(typeid(node).name());
				if (*held != listed) wrong++;
			}
			const std::span<const Ref<Scripting::AstNode>> again = node.Children();
			if (again.data() != children.data() || again.size() != children.size()) wrong++;
			for (const Ref<Scripting::AstNode>& child : children)
				if (child) CheckChildren(*child, kinds, wrong);
		}
	}

	// The parser keeps each node's children as it fills the node, so a walk reads them and never rebuilds them (two
	// threads may walk one body): a call, the casts and the size queries list exactly what they hold.
	void FullProgramTest::ChildrenAreWhatEachNodeHolds()
	{
		auto stream = Scripting::Source::FromMemory(Core::String(
			"class Shape { public int32 sides; }\n"
			"class Square : public Shape { }\n"
			"int32 Probe(Shape* s, int32* values, int32 n)\n"
			"{\n"
			"    int32 total = (int32)sizeof(int32) + static_cast<int32>(n) + sizeof n;\n"
			"    Square* q = s as Square*;\n"
			"    bool square = s is Square*;\n"
			"    total = total > 0 && n < 3 || values[1] == 2 ? Probe(s, values, n - 1) : 0;\n"
			"    return total;\n"
			"}\n"));
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);
		auto root = CreateRef<Scripting::NamespaceNode>();
		root->SetIsRoot(true);
		root->ParseBody(state);
		TEST_ASSERT(errorTracker.GetErrors().Empty());
		std::set<std::string> kinds;
		int wrong = 0;
		CheckChildren(*root, kinds, wrong);
		TEST_ASSERT_EQ(wrong, 0);
		TEST_ASSERT_EQ(kinds.size(), size_t(5));
		TEST_PASSED();
	}

	void FullProgramTest::TestFullProgram()
	{
		auto source = Core::String(R"(
namespace NC {
    enum Color { Red, Green, Blue };
    
    class BaseClass {
        public int baseValue;
    };
    
    template<T>
    public class Container : BaseClass {
        public static int count = 0;
        
        public virtual T Get(int index) const override {
            return null;
        }
    };
    
    struct Point : public BaseClass {
        public float x;
        public float y;
    };
    
    int Add(int a, int b);
    
    int Subtract(int a, int b) {
        return a - b;
    }
    
    public static float GetPI() {
        return 3.14f;
    }
    
    int x = 0;
    const int MAX = 100;
    
    public void ProcessAllStatements() {
        int counter = 0;
        
        if (counter > 0) {
            DoThing();
        }
        
        if (counter == 1) {
            HandleOne();
        } else if (counter == 2) {
            HandleTwo();
        } else {
            HandleOther();
        }
        
        while (counter < 10) {
            counter++;
        }
        
        for (int i = 0; i < 10; i++) {
            DoThing(i);
        }
        
        do {
            Process();
        } while (counter > 0);
        
        switch (counter) {
            case 1:
                HandleOne();
                break;
            default:
                HandleOther();
        }
        
        try {
            RiskyOperation();
        } catch (Exception e) {
            HandleError(e);
        } finally {
            Cleanup();
        }
        
        throw new<Exception>("error");
        
        defer {
            ReleaseResource();
        }
        
        return;
    }
}

namespace Outer {
    namespace Inner {
        class DeepClass {
            public int value;
        };
    }
    
    class OuterClass : Inner::DeepClass {
        public void Process() {
            Inner::DeepClass inner;
            inner.value = 10;
        }
    };
}

int GlobalFunction();
const float GlobalConst = 3.14f;
static int GlobalStatic = 0;
)");

		auto stream = Scripting::Source::FromMemory(source);
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto rootNamespace = CreateRef<Scripting::NamespaceNode>();
		rootNamespace->SetIsRoot(true);
		rootNamespace->ParseBody(state);

		auto children = rootNamespace->Children();
		TEST_ASSERT(!children.empty());
		TEST_ASSERT(children.size() >= 3);
		TEST_PASSED();
	}

	void FullProgramTest::TestEnumWithUnderlyingType()
	{
		auto stream = Scripting::Source::FromMemory("enum Flags : int32 { X = 1, Y = 2, Z = 4 };");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::EnumDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasUnderlyingType());
		TEST_ASSERT(node->MemberCount() == 3);
		TEST_PASSED();
	}

	void FullProgramTest::TestClassWithInheritance()
	{
		auto stream = Scripting::Source::FromMemory("class Derived : public Base1, private Base2 { public int x; };");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasInheritanceList());
		TEST_PASSED();
	}

	void FullProgramTest::TestTemplatedClass()
	{
		auto stream = Scripting::Source::FromMemory("template<T> class Container { public T value; };");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsTemplated());
		TEST_PASSED();
	}

	void FullProgramTest::TestFunctionWithBody()
	{
		auto stream = Scripting::Source::FromMemory("int Add(int a, int b) { return a + b; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsDefinition());
		TEST_ASSERT(node->ParameterCount() == 2);
		TEST_PASSED();
	}

	void FullProgramTest::TestFunctionDeclaration()
	{
		auto stream = Scripting::Source::FromMemory("int Add(int a, int b);");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::FunctionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsDeclaration());
		TEST_PASSED();
	}

	void FullProgramTest::TestNestedControlFlow()
	{
		auto stream = Scripting::Source::FromMemory("if (a) { while (b) { for (int k = 0; k < 10; k++) { if (k == 5) break; } } }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::IfStatementNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetThenBody() != nullptr);
		TEST_PASSED();
	}

	void FullProgramTest::TestCodeBlockDispatch()
	{
		auto stream = Scripting::Source::FromMemory("{ int x = 0; if (x > 0) { return x; } while (true) { break; } }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->StatementCount() >= 3);
		TEST_PASSED();
	}
}
