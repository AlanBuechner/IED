#include "InterfaceTextTest.h"
#include "Compiler/CompilerInstance.h"
#include "Script/InterfaceText.h"
#include <algorithm>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// The interface text of `source`, parsed as one file; "<failed>" when it cannot be made.
		std::string Interface(const std::string& source)
		{
			CompilerInstance compiler;
			const uint32 id = compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			Ref<CompilationUnit> unit = compiler.GetCompilationUnit(id);
			if (!unit) return "<failed>";
			return InterfaceText(source, unit->GetOriginalChildren()).value_or("<failed>");
		}

		bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

		size_t Lines(const std::string& text) { return static_cast<size_t>(std::count(text.begin(), text.end(), '\n')); }

		// The parse errors of `text` compiled alone.
		size_t ParseErrors(const std::string& text)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("i.cf", text);
			compiler.CompileAll();
			size_t errors = 0;
			for (const CompilationError& e : compiler.GetAllDiagnostics())
				if (e.severity != ErrorSeverity::Warning) errors++;
			return errors;
		}
	}

	void InterfaceTextTest::FunctionBodyBecomesADeclaration()
	{
		const std::string text = Interface("int32 Add(int32 a, int32 b) {\n\treturn a + b;\n}\n");
		TEST_ASSERT(Has(text, "int32 Add(int32 a, int32 b) ;"));
		TEST_ASSERT(!Has(text, "return"));
		TEST_PASSED();
	}

	void InterfaceTextTest::MethodBodiesBecomeDeclarations()
	{
		const std::string text = Interface(
			"class Counter {\n"
			"\tpublic int32 count;\n"
			"\t[operator(\"create\")] public void Init() { count = 0; }\n"
			"\tpublic int32 Get() const { return count; }\n"
			"\tpublic virtual void Bump() { count++; }\n"
			"}\n");
		TEST_ASSERT(Has(text, "public int32 count;"));
		TEST_ASSERT(Has(text, "[operator(\"create\")] public void Init() ;"));
		TEST_ASSERT(Has(text, "public int32 Get() const ;"));
		TEST_ASSERT(Has(text, "public virtual void Bump() ;"));
		TEST_ASSERT(!Has(text, "count = 0") && !Has(text, "count++"));
		TEST_PASSED();
	}

	void InterfaceTextTest::OutOfLineDefinitionDisappears()
	{
		const std::string text = Interface(
			"class Point { public int32 x; public void Move(int32 dx); }\n"
			"void Point::Move(int32 dx) {\n\tx += dx;\n}\n"
			"int32 After() { return 1; }\n");
		TEST_ASSERT(Has(text, "public void Move(int32 dx);"));
		TEST_ASSERT(!Has(text, "Point::Move"));
		TEST_ASSERT(Has(text, "int32 After() ;"));
		TEST_PASSED();
	}

	void InterfaceTextTest::AnnotationsGoWithAnOutOfLineDefinition()
	{
		const std::string text = Interface(
			"struct V { public int32 x; [operator(\"+\")] public V Add(const V& o) const; }\n"
			"[operator(\"+\")] V V::Add(const V& o) const { V r; r.x = x + o.x; return r; }\n");
		TEST_ASSERT_EQ(std::count(text.begin(), text.end(), '+'), (std::ptrdiff_t)1); // only the declaration's annotation
		TEST_ASSERT(!Has(text, "V::Add"));
		TEST_PASSED();
	}

	// BUG-241: the return type's leading const was outside the definition's range and stayed behind.
	void InterfaceTextTest::ALeadingConstGoesWithItsDefinition()
	{
		const std::string text = Interface(
			"struct S { public int32 v; public const char* Name() const; }\n"
			"const char* S::Name() const { return \"s\"; }\n"
			"int32 After() { return 1; }\n");
		TEST_ASSERT(!Has(text, "S::Name"));
		size_t consts = 0;
		for (size_t at = text.find("const"); at != std::string::npos; at = text.find("const", at + 1)) consts++;
		TEST_ASSERT_EQ(consts, (size_t)2); // the declaration's two
		TEST_ASSERT_EQ(ParseErrors(text), (size_t)0);
		TEST_PASSED();
	}

	// A header declares the function and a source defines it: the stripped definition would be a second declaration,
	// which moves the function's visibility to the source, out of a header includer's reach.
	void InterfaceTextTest::ADefinitionDeclaredElsewhereDisappears()
	{
		CompilerInstance compiler;
		const std::string source = "int32 Add(int32 a, int32 b) {\n\treturn a + b;\n}\nint32 Keep() { return 1; }\n";
		const uint32 id = compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		const Core::Vector<Ref<AstNode>>& nodes = compiler.GetCompilationUnit(id)->GetOriginalChildren();
		const std::unordered_set<const AstNode*> elsewhere = { nodes[0].Get() };
		const std::string text = InterfaceText(source, nodes, elsewhere).value_or("<failed>");
		TEST_ASSERT(!Has(text, "Add"));
		TEST_ASSERT(Has(text, "int32 Keep() ;"));
		TEST_ASSERT_EQ(Lines(text), Lines(source));
		TEST_PASSED();
	}

	void InterfaceTextTest::TemplatesKeepTheirBodies()
	{
		const std::string text = Interface(
			"template<T> T Twice(T v) { return v + v; }\n"
			"template<T> class Box { public T v; public T Get() { return v; } public void Set(T value); }\n"
			"template<T> void Box<T>::Set(T value) { v = value; }\n"
			"template<> int32 Twice<int32>(int32 v) { return v * 2; }\n"
			"class Plain { public int32 n; template<T> public T As() { return (T)n; } public int32 Get() { return n; } }\n");
		TEST_ASSERT(Has(text, "return v + v;"));
		TEST_ASSERT(Has(text, "public T Get() { return v; }"));
		TEST_ASSERT(Has(text, "template<T> void Box<T>::Set(T value) { v = value; }"));
		TEST_ASSERT(Has(text, "return v * 2;"));
		TEST_ASSERT(Has(text, "return (T)n;"));
		TEST_ASSERT(Has(text, "public int32 Get() ;"));
		TEST_PASSED();
	}

	void InterfaceTextTest::NamespacesAndExtensionsAreWalked()
	{
		const std::string text = Interface(
			"namespace Geo { namespace Inner { int32 Deep() { return 3; } } }\n"
			"struct P { public int32 x; }\n"
			"public void Shift(int32 by) extends P { this->x += by; }\n");
		TEST_ASSERT(Has(text, "int32 Deep() ;"));
		TEST_ASSERT(Has(text, "public void Shift(int32 by) extends P ;"));
		TEST_ASSERT(!Has(text, "x += by"));
		TEST_PASSED();
	}

	void InterfaceTextTest::InitializersStay()
	{
		const std::string text = Interface(
			"const int32 kSize = 4 * 4;\n"
			"int32[kSize] g_cells;\n"
			"struct Item { public int32 count = 3; }\n"
			"enum Mode { Off = 0, On = 7 }\n"
			"funcptr<int32(int32)> g_op = [](int32 v) -> int32 { return v + 1; };\n");
		TEST_ASSERT(Has(text, "const int32 kSize = 4 * 4;"));
		TEST_ASSERT(Has(text, "public int32 count = 3;"));
		TEST_ASSERT(Has(text, "On = 7"));
		TEST_ASSERT(Has(text, "return v + 1;"));
		TEST_PASSED();
	}

	void InterfaceTextTest::LinesKeepTheirNumbers()
	{
		const std::string source =
			"int32 First() {\n\treturn 1;\n}\n"
			"class C {\n\tpublic int32 Two() {\n\t\treturn 2;\n\t}\n}\n"
			"void C2() {}\n"
			"int32 g_marker = 5;\n";
		const std::string text = Interface(source);
		TEST_ASSERT_EQ(Lines(text), Lines(source));
		const size_t markerLine = Lines(text.substr(0, text.find("g_marker")));
		TEST_ASSERT_EQ(markerLine, Lines(source.substr(0, source.find("g_marker"))));
		TEST_PASSED();
	}

	void InterfaceTextTest::TheResultParses()
	{
		const std::string text = Interface(
			"class Point { public int32 x; public void Move(int32 dx); public int32 Get() const { return x; } }\n"
			"void Point::Move(int32 dx) { x += dx; }\n"
			"template<T> T Twice(T v) { return v + v; }\n"
			"int32 main() { Point p; p.Move(2); return Twice(p.Get()) - 4; }\n");
		TEST_ASSERT(text != "<failed>");
		TEST_ASSERT_EQ(ParseErrors(text), (size_t)0);
		TEST_PASSED();
	}

	void InterfaceTextTest::NodesOfAnotherTextFail()
	{
		CompilerInstance compiler;
		const std::string source = "int32 Long() {\n\treturn 1;\n}\n";
		const uint32 id = compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		TEST_ASSERT(!InterfaceText("int32", compiler.GetCompilationUnit(id)->GetOriginalChildren()).has_value());
		TEST_PASSED();
	}
} // namespace Testing
