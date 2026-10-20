#include "FullProgramTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
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
