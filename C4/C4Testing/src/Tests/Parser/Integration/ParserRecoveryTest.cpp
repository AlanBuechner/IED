#include "ParserRecoveryTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/ParserState.h"
#include "Parser/Nodes/ErrorNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <string>
#include <vector>

namespace Testing
{
	namespace
	{
		using namespace C4Compiler;

		struct Parsed
		{
			Ref<NamespaceNode> root;
			std::vector<CompilationError> errors;

			bool HasError(const std::string& start, uint32 line, uint32 column) const
			{
				for (const CompilationError& error : errors)
					if (std::string(error.message.c_str()).rfind(start, 0) == 0 && error.location.line == line && error.location.column == column) return true;
				return false;
			}
		};

		// The file parsed as a compilation unit parses it: braces mended first, then the root's declarations.
		Parsed Parse(const char* text)
		{
			Ref<Source> source = Source::FromMemory(text);
			Lexer lexer(*source, 1);
			ErrorTracker errors;
			ParserState state(lexer, errors);
			for (const LexicalDiagnostic& unpaired : lexer.RecoverBraces())
				errors.AddError(unpaired.location, unpaired.message, ErrorSeverity::Error);
			Parsed parsed;
			parsed.root = CreateRef<NamespaceNode>();
			parsed.root->SetIsRoot(true);
			parsed.root->ParseBody(state);
			for (const CompilationError& error : errors.GetErrors()) parsed.errors.push_back(error);
			return parsed;
		}

		std::string NameOf(const AstNode& node)
		{
			if (const auto* object = dynamic_cast<const ObjectDefinitionNode*>(&node)) return object->GetNameToken().GetText().c_str();
			if (const auto* function = dynamic_cast<const FunctionDefinitionNode*>(&node))
				return function->GetName() ? function->GetName()->GetFirstIdentifierToken().GetText().c_str() : "";
			if (const auto* variable = dynamic_cast<const VariableDefinitionNode*>(&node)) return variable->GetNameToken().GetText().c_str();
			if (node.Type() == NodeType::Error) return "<error>";
			return "?";
		}

		// What a list of declarations names, in order; an ErrorNode is "<error>".
		std::vector<std::string> Names(std::span<const Ref<AstNode>> nodes)
		{
			std::vector<std::string> names;
			for (const Ref<AstNode>& node : nodes)
				if (node) names.push_back(NameOf(*node));
			return names;
		}

		std::vector<std::string> Names(const Core::Vector<Ref<AstNode>>& nodes)
		{
			return Names(std::span<const Ref<AstNode>>(nodes.begin(), nodes.end()));
		}

		const ObjectDefinitionNode* TypeNamed(const Parsed& parsed, const std::string& name)
		{
			for (const Ref<AstNode>& child : parsed.root->Children())
				if (const auto* object = dynamic_cast<const ObjectDefinitionNode*>(child.Get()); object && name == object->GetNameToken().GetText().c_str()) return object;
			return nullptr;
		}

		const FunctionDefinitionNode* FunctionNamed(const Parsed& parsed, const std::string& name)
		{
			for (const Ref<AstNode>& child : parsed.root->Children())
				if (const auto* function = dynamic_cast<const FunctionDefinitionNode*>(child.Get()); function && NameOf(*function) == name) return function;
			return nullptr;
		}

		bool HoldsErrorNode(const AstNode& node)
		{
			for (const Ref<AstNode>& child : node.Children())
				if (child && child->Type() == NodeType::Error) return true;
			return false;
		}
	}

	void ParserRecoveryTest::MemberThatDoesNotParseIsOneError()
	{
		const Parsed parsed = Parse(
			"struct Point\n"
			"{\n"
			"\tpublic int32 x;\n"
			"\tpublic int32 y +;\n"
			"\tpublic int32 Sum() { return x; }\n"
			"}\n"
			"struct Size { public int32 w; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("Expected ';' after the field 'y', got '+'", 4, 2));
		const ObjectDefinitionNode* point = TypeNamed(parsed, "Point");
		TEST_ASSERT(point != nullptr);
		TEST_ASSERT(Names(point->GetBodyMembers()) == std::vector<std::string>({ "x", "Sum" }));
		TEST_ASSERT(HoldsErrorNode(*point));
		TEST_ASSERT(TypeNamed(parsed, "Size") != nullptr);
		TEST_PASSED();
	}

	void ParserRecoveryTest::StatementThatDoesNotParseIsOneError()
	{
		const Parsed parsed = Parse(
			"int32 F(int32 v)\n"
			"{\n"
			"\tint32 a = v *;\n"
			"\treturn v;\n"
			"}\n"
			"int32 G() { return 1; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("Expected expression, got ';'", 3, 15));
		const FunctionDefinitionNode* f = FunctionNamed(parsed, "F");
		TEST_ASSERT(f != nullptr && f->GetBody());
		TEST_ASSERT_EQ(f->GetBody()->GetStatements().Count(), uint32(2));
		TEST_ASSERT(FunctionNamed(parsed, "G") != nullptr);
		TEST_PASSED();
	}

	void ParserRecoveryTest::MissingParenBeforeBlockIsOneError()
	{
		const Parsed parsed = Parse(
			"int32 Twice(int32 v)\n"
			"{\n"
			"\tif (v > 0 {\n"
			"\t\treturn v * 2;\n"
			"\t}\n"
			"\treturn 0;\n"
			"}\n"
			"int32 After() { return 1; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("Expected ')' after condition, got '{'", 3, 12));
		const FunctionDefinitionNode* twice = FunctionNamed(parsed, "Twice");
		TEST_ASSERT(twice != nullptr && twice->GetBody());
		TEST_ASSERT_EQ(twice->GetBody()->GetStatements().Count(), uint32(2));
		TEST_ASSERT(FunctionNamed(parsed, "After") != nullptr);
		TEST_PASSED();
	}

	void ParserRecoveryTest::MissingParenAfterACallIsOneError()
	{
		const Parsed parsed = Parse(
			"bool Ready(int32 v) { return v > 0; }\n"
			"int32 Twice(int32 v)\n"
			"{\n"
			"\tif (Ready(v) {\n"
			"\t\treturn v * 2;\n"
			"\t}\n"
			"\treturn 0;\n"
			"}\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("Expected ')' after condition, got '{'", 4, 15));
		TEST_PASSED();
	}

	void ParserRecoveryTest::BrokenLambdaInACallIsOneError()
	{
		// The skip of what did not parse goes past the lambda's body to the call's `)` and the `;`: they are its own.
		const Parsed parsed = Parse(
			"int32 Apply(funcptr<int32(int32)> f) { return 0; }\n"
			"void F() { int32 z = Apply([&alias](int32 v) -> int32 { return v; }); int32 after = 1; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("Expected a capture in lambda, got 'alias'", 2, 30));
		const FunctionDefinitionNode* f = FunctionNamed(parsed, "F");
		TEST_ASSERT(f != nullptr && f->GetBody());
		TEST_ASSERT(NameOf(*f->GetBody()->GetStatements()[f->GetBody()->StatementCount() - 1]) == "after"); // the block went on
		TEST_PASSED();
	}

	void ParserRecoveryTest::UnclosedTypeBodyEndsByIndentation()
	{
		const Parsed parsed = Parse(
			"struct Point\n"
			"{\n"
			"\tpublic int32 x;\n"
			"\tpublic int32 Get() { return x; }\n"
			"\n"
			"int32 Twice(int32 v) { return v * 2; }\n"
			"\n"
			"struct Size\n"
			"{\n"
			"\tpublic int32 w;\n"
			"}\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("this '{' has no matching '}': by the indentation, its block ends before line 6", 2, 1));
		TEST_ASSERT(Names(parsed.root->Children()) == std::vector<std::string>({ "Point", "Twice", "Size" }));
		TEST_ASSERT(Names(TypeNamed(parsed, "Point")->GetBodyMembers()) == std::vector<std::string>({ "x", "Get" }));
		TEST_PASSED();
	}

	void ParserRecoveryTest::UnclosedBlockInMethodEndsAtOuterBrace()
	{
		const Parsed parsed = Parse(
			"struct Counter\n"
			"{\n"
			"\tpublic int32 count;\n"
			"\tpublic void Add(int32 n)\n"
			"\t{\n"
			"\t\tif (n > 0)\n"
			"\t\t{\n"
			"\t\t\tcount = count + n;\n"
			"\t}\n"
			"\tpublic int32 Get() { return count; }\n"
			"}\n"
			"struct Size { public int32 w; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("this '{' has no matching '}': by the indentation, its block ends before line 9", 7, 3));
		TEST_ASSERT(Names(TypeNamed(parsed, "Counter")->GetBodyMembers()) == std::vector<std::string>({ "count", "Add", "Get" }));
		TEST_ASSERT(TypeNamed(parsed, "Size") != nullptr);
		TEST_PASSED();
	}

	void ParserRecoveryTest::ExtraClosingBraceIsLeftOut()
	{
		const Parsed parsed = Parse(
			"int32 Clamp(int32 x)\n"
			"{\n"
			"\tif (x > 10)\n"
			"\t\treturn 10;\n"
			"\t}\n"
			"\treturn x;\n"
			"}\n"
			"int32 After() { return 1; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("this '}' has no matching '{': it is left out", 5, 2));
		const FunctionDefinitionNode* clamp = FunctionNamed(parsed, "Clamp");
		TEST_ASSERT(clamp != nullptr && clamp->GetBody());
		TEST_ASSERT_EQ(clamp->GetBody()->GetStatements().Count(), uint32(2));
		TEST_ASSERT(Names(parsed.root->Children()) == std::vector<std::string>({ "Clamp", "After" }));
		TEST_PASSED();
	}

	void ParserRecoveryTest::StrayBraceAtRootIsOneError()
	{
		const Parsed parsed = Parse(
			"}\n"
			"int32 After() { return 1; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT(parsed.HasError("Unexpected token '}'", 1, 1));
		TEST_ASSERT(Names(parsed.root->Children()) == std::vector<std::string>({ "<error>", "After" }));
		TEST_PASSED();
	}

	void ParserRecoveryTest::GarbageSkipsToNextDeclaration()
	{
		const Parsed parsed = Parse(
			"int32 x = 1;\n"
			"+ - * garbage here ;\n"
			"struct Size { public int32 w; }\n"
			"= = more\n"
			"struct Point { public int32 x; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(2));
		TEST_ASSERT(parsed.HasError("Unexpected token '+'", 2, 1));
		TEST_ASSERT(Names(parsed.root->Children()) == std::vector<std::string>({ "x", "<error>", "Size", "<error>", "Point" }));
		TEST_PASSED();
	}

	void ParserRecoveryTest::FunctionWithBrokenParametersKeepsTheRest()
	{
		const Parsed parsed = Parse(
			"int32 Twice(int32 v) { return v * 2; }\n"
			"\n"
			"int32 Bad( { return 1; }\n"
			"\n"
			"struct Size\n"
			"{\n"
			"\tpublic int32 w;\n"
			"}\n"
			"\n"
			"int32 Area(Size s) { return s.w; }\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(1));
		TEST_ASSERT_EQ(parsed.errors[0].location.line, uint32(3));
		TEST_ASSERT(FunctionNamed(parsed, "Twice") != nullptr);
		TEST_ASSERT(TypeNamed(parsed, "Size") != nullptr);
		TEST_ASSERT(FunctionNamed(parsed, "Area") != nullptr);
		TEST_PASSED();
	}

	void ParserRecoveryTest::NamespaceOpenAtEndOfFileIsAnError()
	{
		// The braces pair up (a stray `}` before), so nothing mends them: the namespace still says it is not closed.
		const Parsed parsed = Parse(
			"}\n"
			"namespace Geo\n"
			"{\n"
			"\tint32 x;\n");
		TEST_ASSERT_EQ(parsed.errors.size(), size_t(2));
		TEST_ASSERT(parsed.HasError("Unexpected token '}'", 1, 1));
		bool closingError = false;
		for (const CompilationError& error : parsed.errors) closingError = closingError || error.message == "Expected '}' to close the namespace body";
		TEST_ASSERT(closingError);
		TEST_PASSED();
	}

	void ParserRecoveryTest::WellFormedFileHasNoErrorNode()
	{
		const Parsed parsed = Parse(
			"namespace Geo\n"
			"{\n"
			"\tstruct Size { public int32 w; public int32 Area() { if (w > 0) { return w * w; } return 0; } }\n"
			"\tint32 Twice(int32 v) { Size s = { v }; return s.Area() + v * 2; }\n"
			"}\n");
		TEST_ASSERT(parsed.errors.empty());
		TEST_ASSERT(Names(parsed.root->Children()) == std::vector<std::string>({ "?" }));
		TEST_ASSERT(!HoldsErrorNode(*parsed.root));
		TEST_PASSED();
	}
}
