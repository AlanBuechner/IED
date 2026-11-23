#include "DeclarationFingerprintTest.h"
#include "Compiler/CompilerInstance.h"
#include <memory>
#include <string>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// The fingerprint of `text` parsed as the program's one file.
		uint64 FingerprintOf(const std::string& text)
		{
			auto compiler = std::make_unique<CompilerInstance>();
			compiler->RegisterInMemory("main.cf", text);
			compiler->RegisterInMemory("lib.ch", "int32 Lib();\n");
			compiler->CompileAll();
			return compiler->GetCompilationUnit(1)->Fingerprint();
		}

		bool Keeps(const std::string& before, const std::string& after) { return FingerprintOf(before) == FingerprintOf(after); }
	}

	void DeclarationFingerprintTest::AStatementInAPlainBodyKeepsIt()
	{
		TEST_ASSERT(Keeps("int32 F(int32 v) { return v * 2; }\n", "int32 F(int32 v) { int32 w = v + 1; return w * 3; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::AMethodsBodyKeepsIt()
	{
		TEST_ASSERT(Keeps("class P { public int32 x; public int32 Get() { return x; } };\nint32 P::Out() { return 1; }\n",
			"class P { public int32 x; public int32 Get() { return x + 1; } };\nint32 P::Out() { return 2; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::CommentsAndLinesKeepIt()
	{
		TEST_ASSERT(Keeps("int32 F() { return 1; }\nint32 G() { return 2; }\n",
			"// a comment\n\nint32 F()\n{\n\treturn 1;\n}\n/* more */ int32 G() { return 2; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::ASignatureChangesIt()
	{
		TEST_ASSERT(!Keeps("int32 F(int32 v) { return v; }\n", "int32 F(int64 v) { return v; }\n"));
		TEST_ASSERT(!Keeps("int32 F(int32 v) { return v; }\n", "int32 G(int32 v) { return v; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::AFieldChangesIt()
	{
		TEST_ASSERT(!Keeps("class P { public int32 x; };\n", "class P { public int32 x; public int32 y; };\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::AnIncludeChangesIt()
	{
		TEST_ASSERT(!Keeps("int32 F() { return 1; }\n", "include \"lib.ch\"\nint32 F() { return 1; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::AConstantsValueChangesIt()
	{
		TEST_ASSERT(!Keeps("const int32 c_Size = 4;\nint32 g_Table[c_Size];\n", "const int32 c_Size = 8;\nint32 g_Table[c_Size];\n"));
		TEST_ASSERT(!Keeps("enum E { A = 1 };\n", "enum E { A = 2 };\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::ATemplatesBodyChangesIt()
	{
		TEST_ASSERT(!Keeps("template<T> T Twice(T v) { return v * 2; }\n", "template<T> T Twice(T v) { return v * 3; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::AClassTemplatesMethodBodyChangesIt()
	{
		TEST_ASSERT(!Keeps("template<T> class Box { public T v; public T Get() { return v; } };\n",
			"template<T> class Box { public T v; public T Get() { T w = v; return w; } };\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::ACompileTimeBodyChangesIt()
	{
		TEST_ASSERT(!Keeps("[compile_time] int32 Square(int32 v) { return v * v; }\n",
			"[compile_time] int32 Square(int32 v) { return v * v + 1; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::AnInlineBodyChangesIt()
	{
		TEST_ASSERT(!Keeps("inline int32 F(int32 v) { return v; }\n", "inline int32 F(int32 v) { return v + 1; }\n"));
		TEST_PASSED();
	}

	void DeclarationFingerprintTest::AnAnnotationChangesIt()
	{
		TEST_ASSERT(!Keeps("int32 F() { return 1; }\n", "[reflect] int32 F() { return 1; }\n"));
		TEST_PASSED();
	}
} // namespace Testing
