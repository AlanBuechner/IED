#include "IncludeVisibilityTest.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include <map>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		struct Source
		{
			const char* name;
			std::string text;
		};

		// A program of several files, analyzed: its errors, its model, and each file's unit.
		class Program
		{
		public:
			explicit Program(const std::vector<Source>& files)
			{
				for (const Source& file : files) m_Units[file.name] = m_Compiler.RegisterInMemory(file.name, file.text.c_str());
				m_Compiler.CompileAll();
				m_Compiler.AnalyzeProgram();
			}

			const ProgramModel& Model() { return m_Compiler.GetProgramModel(); }
			uint32 Unit(const char* file) { return m_Units[file]; }
			std::string Errors()
			{
				std::string all;
				for (const CompilationError& e : m_Compiler.GetAllDiagnostics())
					if (e.severity != ErrorSeverity::Warning) all += std::string(e.message.c_str()) + "\n";
				return all;
			}
			bool Reports(const std::string& part) { return Errors().find(part) != std::string::npos; }

			// `name(arguments)` called in `file`: the query's answer.
			QueryResult<const FunctionType*> Call(const char* file, const char* name, const std::vector<TypeKind>& arguments,
				const FunctionRecord** record = nullptr)
			{
				Core::Vector<const Type*> types;
				for (TypeKind kind : arguments) types.Push(Model().TypeStorePtr()->Builtin(kind));
				LookupContext context;
				context.unitId = Unit(file);
				return Model().QueryFunction(name, nullptr, nullptr, types, context, record);
			}

		private:
			CompilerInstance m_Compiler;
			std::map<std::string, uint32> m_Units;
		};

		const char* const c_Widgets = "namespace N { struct Widget { int32 size; } }\n";
		const char* const c_Main = "int32 main() { return 0; }\n";

		std::string NotIncluded(const char* what, const char* name, const char* file)
		{
			return std::string("no ") + what + " named " + name + " is visible here: it is declared in " + file + ", which this file does not include";
		}
	}

	void IncludeVisibilityTest::ADeclarationNamesWhatItsFileIncludes()
	{
		Program included({ { "widgets.ch", c_Widgets },
			{ "main.cf", std::string("include \"widgets.ch\"\nstruct Holder { N::Widget* widget; }\nint32 Count(N::Widget& widget) { return widget.size; }\n") + c_Main } });
		TEST_ASSERT_EQ(included.Errors(), std::string());

		// The roadmap's example: it compiled until the header that declares N::Widget left the program.
		Program leaning({ { "widgets.ch", c_Widgets }, { "other.cf", "include \"widgets.ch\"\nN::Widget g_Widget;\n" },
			{ "main.cf", std::string("int32 Count(N::Widget& widget) { return 0; }\n") + c_Main } });
		TEST_ASSERT(leaning.Reports("unresolved signature for function 'Count': " + NotIncluded("type", "Widget", "widgets.ch")));
		TEST_PASSED();
	}

	void IncludeVisibilityTest::EveryKindOfDeclarationNeedsTheInclude()
	{
		Program program({ { "widgets.ch", "struct Widget { int32 size; }\nenum Shade : uint8 { Dark, Light }\n" },
			{ "box.ch", "template<T> struct Box { T* held; }\n" },
			{ "other.cf", "include \"widgets.ch\"\nWidget g_Widget;\n" },
			{ "main.cf", std::string("include \"box.ch\"\n"
				"struct Holder { Widget* field; }\n"
				"Widget* g_Global;\n"
				"struct Derived : Widget { int32 more; }\n"
				"alias Thing = Widget;\n"
				"Box<Widget> g_Boxed;\n"
				"struct Api { Widget* Make(); void Take(Widget& widget); }\n"
				"Widget* Free();\n"
				"int32 Length() extends Widget;\n"
				"union Either { Widget* widget; int32 number; }\n"
				"enum Level : Shade { Low }\n") + c_Main } });
		const std::string why = NotIncluded("type", "Widget", "widgets.ch");
		TEST_ASSERT(program.Reports("unresolved type for field 'field' in 'Holder': " + why));
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Global': " + why));
		TEST_ASSERT(program.Reports("unresolved base class for 'Derived': " + why));
		TEST_ASSERT(program.Reports("unresolved alias target for 'Thing': " + why));
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Boxed': " + why));
		TEST_ASSERT(program.Reports("unresolved method 'Make' in 'Api': " + why));
		TEST_ASSERT(program.Reports("unresolved method 'Take' in 'Api': " + why));
		TEST_ASSERT(program.Reports("unresolved signature for function 'Free': " + why));
		TEST_ASSERT(program.Reports("unresolved type for field 'widget' in the union 'Either': " + why));
		TEST_ASSERT(program.Reports("unresolved enum 'Level': underlying type: " + NotIncluded("type", "Shade", "widgets.ch")));
		TEST_PASSED();
	}

	void IncludeVisibilityTest::AnIncludeReachesWhatItIncludes()
	{
		Program program({ { "widgets.ch", c_Widgets }, { "panel.ch", "include \"widgets.ch\"\nstruct Panel { N::Widget first; }\n" },
			{ "window.ch", "include \"panel.ch\"\nstruct Window { Panel panel; }\n" },
			{ "main.cf", std::string("include \"window.ch\"\nstruct App { Window window; N::Widget* focus; }\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		TEST_PASSED();
	}

	// Headers that name each other cannot include each other: each declares the other's type ahead.
	void IncludeVisibilityTest::AForwardDeclarationIsEnoughForAPointer()
	{
		Program program({ { "parent.ch", "struct Child;\nstruct Parent { Child* first; }\n" },
			{ "child.ch", "struct Parent;\nstruct Child { Parent* owner; }\n" },
			{ "main.cf", std::string("include \"parent.ch\"\ninclude \"child.ch\"\nParent g_Root;\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		TEST_PASSED();
	}

	void IncludeVisibilityTest::AConstantInADeclarationNeedsItsHeader()
	{
		const char* limits = "const int32 kLimit = 4;\nnamespace Sizes { const int32 kRow = 8; }\n";
		Program included({ { "limits.ch", limits }, { "main.cf", std::string("include \"limits.ch\"\nint32[kLimit] g_Slots;\nint32[Sizes::kRow] g_Row;\n") + c_Main } });
		TEST_ASSERT_EQ(included.Errors(), std::string());
		Program leaning({ { "limits.ch", limits }, { "other.cf", "include \"limits.ch\"\nint32[kLimit] g_Other;\n" },
			{ "main.cf", std::string("int32[kLimit] g_Slots;\nint32[Sizes::kRow] g_Row;\n") + c_Main } });
		TEST_ASSERT(leaning.Reports("unresolved type for variable 'g_Slots': the array size 'kLimit' is not a constant expression: " +
			NotIncluded("constant", "kLimit", "limits.ch")));
		TEST_ASSERT(leaning.Reports(NotIncluded("constant", "Sizes::kRow", "limits.ch")));
		TEST_PASSED();
	}

	void IncludeVisibilityTest::AClassTemplateNeedsItsHeader()
	{
		const char* box = "template<T> struct Box { T held; }\ntemplate<T> alias Pair = Box<T>[2];\n";
		Program program({ { "box.ch", box }, { "other.cf", "include \"box.ch\"\nBox<int32> g_Other;\n" },
			{ "main.cf", std::string("Box<int32> g_Box;\nPair<int32> g_Pair;\n") + c_Main } });
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Box': " + NotIncluded("type", "Box", "box.ch")));
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Pair': " + NotIncluded("type", "Pair", "box.ch")));
		TEST_PASSED();
	}

	// The user includes only box.ch; the instance's field names a type of the file box.ch includes.
	void IncludeVisibilityTest::AnInstanceSeesWhatItsTemplatesFileIncludes()
	{
		Program program({ { "tag.ch", "struct Tag { uint8 value; }\n" },
			{ "box.ch", "include \"tag.ch\"\ntemplate<T> struct Box { T held; Tag tag; }\n" },
			{ "main.cf", std::string("include \"box.ch\"\nstruct Local { int32 x; }\nBox<Local> g_Box;\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		TEST_PASSED();
	}

	// The template's file leans on a header only its user includes: the instance is the template's file's, so it fails,
	// and the error, reported where the instance is written, names the template's file.
	void IncludeVisibilityTest::AnInstanceDoesNotSeeWhatOnlyItsUserIncludes()
	{
		Program program({ { "tag.ch", "struct Tag { uint8 value; }\n" },
			{ "box.ch", "template<T> struct Box { T held; Tag tag; }\n" },
			{ "main.cf", std::string("include \"tag.ch\"\ninclude \"box.ch\"\nBox<int32> g_Box;\n") + c_Main } });
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Box': cannot instantiate Box: "));
		TEST_ASSERT(program.Reports("no type named Tag is visible in box.ch: it is declared in tag.ch, which box.ch does not include"));
		TEST_PASSED();
	}

	void IncludeVisibilityTest::AnInstancesConstantNeedsItsTemplatesInclude()
	{
		Program program({ { "limits.ch", "const int32 kLimit = 4;\n" },
			{ "box.ch", "template<T> struct Box { T[kLimit] held; }\n" },
			{ "main.cf", std::string("include \"limits.ch\"\ninclude \"box.ch\"\nBox<int32> g_Box;\n") + c_Main } });
		TEST_ASSERT(program.Reports("no constant named kLimit is visible in box.ch: it is declared in limits.ch, which box.ch does not include"));
		TEST_PASSED();
	}

	// A template's own namespace is where its names are looked up, whichever namespace writes the instance.
	void IncludeVisibilityTest::AnInstanceNamesItsTemplatesNamespace()
	{
		Program program({ { "shapes.ch", "namespace A { struct Helper { int32 v; } const int32 kCount = 3;\n"
				"template<T> struct Box { T held; Helper helper; int32[kCount] slots; } }\n" },
			{ "main.cf", std::string("include \"shapes.ch\"\nnamespace B { struct Helper { int64 other; } A::Box<int32> g_Box; }\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		auto box = program.Model().QueryVariable("B::g_Box");
		TEST_ASSERT(box.Ok());
		TEST_ASSERT_EQ(static_cast<int64>(box.value.type->GetSize()), int64(20));
		TEST_PASSED();
	}

	void IncludeVisibilityTest::AnInstanceIsTheSameWhoeverAsksFirst()
	{
		const Source tag{ "tag.ch", "struct Tag { uint8 value; }\n" };
		const Source box{ "box.ch", "include \"tag.ch\"\ntemplate<T> struct Box { T held; Tag tag; }\n" };
		const Source seeing{ "seeing.cf", "include \"box.ch\"\nBox<int32> g_Seeing;\n" };
		const Source main{ "main.cf", std::string("include \"box.ch\"\nBox<int32> g_Main;\n") + c_Main };
		for (const std::vector<Source>& order : { std::vector<Source>{ tag, box, seeing, main }, std::vector<Source>{ main, seeing, box, tag } })
		{
			Program program(order);
			TEST_ASSERT_EQ(program.Errors(), std::string());
			auto first = program.Model().QueryVariable("g_Seeing");
			auto second = program.Model().QueryVariable("g_Main");
			TEST_ASSERT(first.Ok() && second.Ok() && first.value.type == second.value.type);
			TEST_ASSERT_EQ(static_cast<int64>(first.value.type->GetSize()), int64(8));
		}
		TEST_PASSED();
	}

	// `template<T> void Box<T>::Fill()` is written in box.cf: its body's names resolve as box.cf's do.
	void IncludeVisibilityTest::AnOutOfLineTemplateMethodSeesItsOwnFile()
	{
		Program program({ { "box.ch", "template<T> struct Box { T held; void Fill(); int32 Size() { return 1; } }\n" },
			{ "box.cf", "include \"box.ch\"\ntemplate<T> void Box<T>::Fill() { }\n" },
			{ "main.cf", std::string("include \"box.ch\"\nBox<int32> g_Box;\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		Core::Vector<const FunctionRecord*> records;
		program.Model().AllFunctionRecords(records);
		uint32 fill = 0, size = 0;
		for (const FunctionRecord* record : records)
		{
			if (record->templateOrigin != TemplateOrigin::ClassMember) continue;
			if (record->name == "Fill") fill = record->LookupUnit();
			if (record->name == "Size") size = record->LookupUnit();
		}
		TEST_ASSERT_EQ(fill, program.Unit("box.cf"));
		TEST_ASSERT_EQ(size, program.Unit("box.ch"));
		TEST_PASSED();
	}

	// BUG-476: `M::Clamp` was callable from a file that included nothing, while `M::Round` beside it was not.
	void IncludeVisibilityTest::AFunctionTemplateIsFoundAsAFunctionIs()
	{
		const char* scalar = "namespace M { template<T> T Clamp(T v, T lo, T hi) { return v; } float Round(float v) { return v; } }\n";
		Program program({ { "scalar.ch", scalar }, { "seeing.cf", "include \"scalar.ch\"\nfloat Seeing(float x) { return M::Round(x); }\n" },
			{ "main.cf", c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		const std::vector<TypeKind> three = { TypeKind::Float32, TypeKind::Float32, TypeKind::Float32 };
		TEST_ASSERT(program.Call("seeing.cf", "M::Clamp", three).Ok());
		TEST_ASSERT(program.Call("seeing.cf", "M::Round", { TypeKind::Float32 }).Ok());
		auto clamp = program.Call("main.cf", "M::Clamp", three);
		auto round = program.Call("main.cf", "M::Round", { TypeKind::Float32 });
		TEST_ASSERT_EQ(std::string(clamp.message.c_str()), NotIncluded("function", "M::Clamp", "scalar.ch"));
		TEST_ASSERT_EQ(std::string(round.message.c_str()), NotIncluded("function", "M::Round", "scalar.ch"));
		TEST_PASSED();
	}

	// BUG-476 as it was met: the file includes the header of the templates, not the one of the plain overload it means.
	void IncludeVisibilityTest::AnUnseenOverloadIsNamedBesideAMismatch()
	{
		Program program({ { "scalar.ch", "namespace M { float Round(float v) { return v; } }\n" },
			{ "vector.ch", "namespace M { template<T> struct Vec { T x; }\ntemplate<T> Vec<T> Round(Vec<T> v) { return v; } }\n" },
			{ "seeing.cf", "include \"scalar.ch\"\nfloat Seeing(float x) { return M::Round(x); }\n" },
			{ "main.cf", std::string("include \"vector.ch\"\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		auto round = program.Call("main.cf", "M::Round", { TypeKind::Float32 });
		TEST_ASSERT(!round.Ok());
		const std::string said = round.message.c_str();
		TEST_ASSERT(said.find("(another 'M::Round' is declared in scalar.ch, which this file does not include)") != std::string::npos);
		TEST_PASSED();
	}

	// The declaration in the header is what a caller's file includes; the body is in a source file nobody includes.
	void IncludeVisibilityTest::ATemplateDeclaredInAHeaderIsDefinedInItsSource()
	{
		Program program({ { "twice.ch", "template<T> T Twice(T v);\n" },
			{ "twice.cf", "include \"twice.ch\"\ntemplate<T> T Twice(T v) { return v + v; }\n" },
			{ "main.cf", std::string("include \"twice.ch\"\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		const FunctionRecord* record = nullptr;
		auto twice = program.Call("main.cf", "Twice", { TypeKind::Int32 }, &record);
		TEST_ASSERT(twice.Ok() && record != nullptr);
		TEST_ASSERT(record->bodyNode != nullptr);
		TEST_ASSERT_EQ(record->LookupUnit(), program.Unit("twice.cf"));
		TEST_PASSED();
	}

	// A method and an extension method are reached through the object, whose type its file could name.
	void IncludeVisibilityTest::MembersAreReachedThroughTheirObject()
	{
		Program program({ { "widget.ch", "struct Widget { int32 size; int32 Size() { return size; } }\n" },
			{ "widget_more.ch", "include \"widget.ch\"\nint32 Doubled() extends Widget { return size * 2; }\n" },
			{ "make.ch", "include \"widget.ch\"\nWidget* Make();\n" },
			{ "main.cf", std::string("include \"make.ch\"\n") + c_Main } });
		TEST_ASSERT_EQ(program.Errors(), std::string());
		auto widget = program.Model().QueryType("Widget", program.Unit("main.cf"));
		TEST_ASSERT(widget.Ok());
		LookupContext context;
		context.unitId = program.Unit("main.cf");
		TEST_ASSERT(program.Model().QueryFunction("Size", widget.value, nullptr, {}, context).Ok());
		TEST_ASSERT(program.Model().QueryFunction("Doubled", widget.value, nullptr, {}, context).Ok());
		TEST_PASSED();
	}

	void IncludeVisibilityTest::ToolsAskingForNoFileSeeEverything()
	{
		Program program({ { "widgets.ch", c_Widgets }, { "box.ch", "template<T> struct Box { T held; }\n" }, { "main.cf", c_Main } });
		const uint32 everyFile = 0;
		TEST_ASSERT(program.Model().QueryType("N::Widget", everyFile).Ok());
		TEST_ASSERT(!program.Model().QueryType("N::Widget", program.Unit("main.cf")).Ok());
		TEST_PASSED();
	}
} // namespace Testing
