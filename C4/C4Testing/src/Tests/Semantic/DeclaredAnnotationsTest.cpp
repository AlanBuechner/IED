#include "DeclaredAnnotationsTest.h"
#include "Compiler/CompilerInstance.h"
#include "Model/DeclaredAnnotations.h"
#include "Model/ProgramModel.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"

namespace Testing
{
	namespace
	{
		const char* kLibrary =
			"namespace Lib { namespace Annotations {\n"
			"\t[annotation] struct Requires {}\n"
			"\t[annotation] struct Component {}\n"
			"} }\n";

		const Scripting::ProgramModel& MakeModel(Scripting::CompilerInstance& compiler, const std::string& source)
		{
			uint32 id = compiler.RegisterInMemory("t.cf", source.c_str());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetCompilationUnit(id)->GetProgramModel();
		}

		// The errors whose message holds `text`.
		Core::Vector<Scripting::CompilationError> ErrorsSaying(const Scripting::CompilerInstance& compiler, const char* text)
		{
			Core::Vector<Scripting::CompilationError> found;
			for (const Scripting::CompilationError& error : compiler.GetAllErrors())
				if (error.message.find(text) != Core::String::npos) found.Push(error);
			return found;
		}

		std::string NameOf(const Scripting::ObjectType* type) { return type ? Scripting::QualifiedName(type).c_str() : "(none)"; }
	}

	void DeclaredAnnotationsTest::FoundByTheirOwnName()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, std::string(kLibrary) + "[Lib::Annotations::Requires, Component] struct C { public int32 v; }\n");
		TEST_ASSERT(compiler.GetAllErrors().Empty());
		Scripting::DeclaredAnnotations declared(model);
		TEST_ASSERT(NameOf(declared.Find("Requires")) == "Lib::Annotations::Requires");
		TEST_ASSERT(NameOf(declared.Find("Lib::Requires")) == "Lib::Annotations::Requires"); // by its last part
		TEST_ASSERT(declared.Find("Range") == nullptr);
		TEST_ASSERT(declared.MisspeltBy("Requires") == nullptr); // a declared name misspells nothing
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AMisspeltOneIsAnError()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, std::string(kLibrary) + "struct M { public int32 v; }\n[Component, Requiers(M)] struct P { public int32 v; }\n");
		auto errors = ErrorsSaying(compiler, "unknown annotation 'Requiers': did you mean 'Requires', which Lib::Annotations::Requires declares?");
		TEST_ASSERT_EQ((int)errors.Count(), 1);
		TEST_ASSERT_EQ((int)errors[0].location.line, 6);
		TEST_ASSERT_EQ((int)errors[0].location.column, 13);
		TEST_ASSERT(errors[0].severity == Scripting::ErrorSeverity::Error);
		Scripting::CompilerInstance cased;
		MakeModel(cased, std::string(kLibrary) + "[requires] struct Q { public int32 v; }\n");
		TEST_ASSERT_EQ((int)ErrorsSaying(cased, "unknown annotation 'requires': did you mean 'Requires'").Count(), 1); // case counts
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::EveryKindOfDeclarationIsChecked()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, std::string(kLibrary) +
			"struct F { [Componnt] public int32 field; [Requirs] public void Method([Componet] int32 p) { } }\n"
			"[Requres] int32 Free() { return 0; }\n"
			"[Compnent] int32 g_global = 1;\n"
			"[Requirez] enum class E { A }\n");
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "unknown annotation 'Componnt'").Count(), 1);
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "unknown annotation 'Requirs'").Count(), 1);
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "unknown annotation 'Componet'").Count(), 1);
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "unknown annotation 'Requres'").Count(), 1);
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "unknown annotation 'Compnent'").Count(), 1);
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "unknown annotation 'Requirez'").Count(), 1);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::NamesFarFromEveryDeclaredOneStayFree()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, std::string(kLibrary) + "[Range(0, 1), Hidden, Serialize, Requests] struct U { public int32 v; }\n");
		TEST_ASSERT(compiler.GetAllErrors().Empty()); // undeclared annotations are as free as ever (D-42)
		Scripting::CompilerInstance none;
		MakeModel(none, "[Requiers(1)] struct V { public int32 v; }\n");
		TEST_ASSERT(none.GetAllErrors().Empty()); // nothing declared: nothing is a typo
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::TheClosestIsNamed()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "[annotation] struct Hidden {} [annotation] struct Hinted {} [annotation] struct Bidden {}\n");
		Scripting::DeclaredAnnotations declared(model);
		TEST_ASSERT(NameOf(declared.MisspeltBy("Hiddem")) == "Hidden"); // one edit beats two
		TEST_ASSERT(NameOf(declared.MisspeltBy("Xidden")) == "Bidden"); // a tie: the name first in order, whatever the map's
		TEST_ASSERT(declared.MisspeltBy("Hxxxxx") == nullptr);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::OnlyAStructOrClassDeclaresOne()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler,
			"[annotation] int32 Free() { return 0; }\n"
			"struct S { [annotation] public int32 field; }\n"
			"[annotation] template<T> struct Box { public T value; }\n"
			"[annotation] class Fine { public int32 v; }\n");
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "[annotation] declares an annotation by the struct or class it marks").Count(), 2);
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "a struct or class that is no template: 'Box' is one").Count(), 1);
		TEST_ASSERT_EQ((int)compiler.GetAllErrors().Count(), 3); // a class is fine
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::ItTakesNoArguments()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, "[annotation(\"x\")] struct A {}\n");
		TEST_ASSERT_EQ((int)ErrorsSaying(compiler, "'annotation' takes no arguments").Count(), 1);
		TEST_PASSED();
	}
}
