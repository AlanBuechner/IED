#include "DeclaredAnnotationsTest.h"
#include "Compiler/CompilerInstance.h"
#include "Model/Annotations.h"
#include "Model/DeclaredAnnotations.h"
#include "Model/ProgramModel.h"
#include "Model/Types/ObjectType.h"

namespace Testing
{
	namespace
	{
		// A library's annotations, declared in the global namespace so a use anywhere names them alone.
		const char* kLibrary = R"(annotation Requires(typename component, typename... more) on type;
annotation Requires(const char* typeName, const char*... more) on type;
annotation Component on type;
)";

		const Scripting::ProgramModel& MakeModel(Scripting::CompilerInstance& compiler, const std::string& source)
		{
			uint32 id = compiler.RegisterInMemory("t.cf", source.c_str());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetCompilationUnit(id)->GetProgramModel();
		}

		// The diagnostics whose message holds `text`.
		Core::Vector<Scripting::CompilationError> Saying(const Scripting::CompilerInstance& compiler, const char* text)
		{
			Core::Vector<Scripting::CompilationError> found;
			for (const Scripting::CompilationError& error : compiler.GetAllDiagnostics())
				if (error.message.find(text) != Core::String::npos) found.Push(error);
			return found;
		}

		int Count(const Scripting::CompilerInstance& compiler, Scripting::ErrorSeverity severity)
		{
			int count = 0;
			for (const Scripting::CompilationError& error : compiler.GetAllDiagnostics())
				if (error.severity == severity) ++count;
			return count;
		}

		int Errors(const Scripting::CompilerInstance& compiler) { return Count(compiler, Scripting::ErrorSeverity::Error); }

		bool At(const Scripting::CompilationError& error, uint32 line, uint32 column) { return error.location.line == line && error.location.column == column; }

		Core::Vector<Scripting::ReflectedAnnotation> Registered(const Scripting::ProgramModel& model, const char* type, const char* field = nullptr)
		{
			for (const Scripting::ObjectType* object : model.ObjectTypes())
			{
				if (!object || object->GetName() != type) continue;
				Core::Vector<const Scripting::AstNode*> declarations;
				Scripting::AnnotationPlace place = Scripting::AnnotationPlace::Type;
				if (!field) declarations.Push(object->GetDeclNode());
				else
				{
					place = Scripting::AnnotationPlace::Field;
					for (const auto& info : object->GetFields())
						if (info.mName == field) declarations.Push(info.m_DeclaringNode);
				}
				auto folded = Scripting::ReflectedAnnotations(model, declarations, place, model.TypeScope(object));
				return folded.Ok() ? folded.value : Core::Vector<Scripting::ReflectedAnnotation>();
			}
			return {};
		}

		size_t OverloadCount(const Scripting::DeclaredAnnotations& declared, const char* qualifiedName)
		{
			const auto* overloads = declared.Named(qualifiedName);
			return overloads ? overloads->Count() : 0;
		}

		// What the annotations a type carries register as: each one's name.
		Core::String NamesOn(const Scripting::ProgramModel& model, const char* type)
		{
			Core::String names;
			for (const Scripting::ReflectedAnnotation& annotation : Registered(model, type)) names += (names.empty() ? "" : " ") + annotation.name;
			return names;
		}
	}

	void DeclaredAnnotationsTest::FoundByTheirQualifiedName()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, std::string(kLibrary) + "struct M { public int32 v; }\n[Requires(M), Component] struct C { public int32 v; }\n"
			"namespace Lib { annotation Requires(int32 n) on type; }\n");
		TEST_ASSERT(compiler.GetAllErrors().Empty());
		const Scripting::DeclaredAnnotations& declared = model.Annotations();
		TEST_ASSERT_EQ((int)OverloadCount(declared, "Requires"), 2);
		TEST_ASSERT_EQ((int)OverloadCount(declared, "Lib::Requires"), 1); // another annotation, of its namespace
		TEST_ASSERT_EQ((int)OverloadCount(declared, "Component"), 1);
		TEST_ASSERT(declared.Named("Range") == nullptr);
		TEST_ASSERT((*declared.Named("Requires"))[0]->qualifiedName == "Requires");
		TEST_ASSERT((*declared.Named("Lib::Requires"))[0]->qualifiedName == "Lib::Requires");
		TEST_ASSERT((*declared.Named("Requires"))[0]->ToString() == "Requires(typename component, typename... more)");
		TEST_ASSERT((*declared.Named("Requires"))[1]->ToString() == "Requires(const char* typeName, const char*... more)");
		TEST_ASSERT(declared.MisspeltBy("Requires", {}).Empty()); // a declared name misspells nothing
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AUseNamesOneAsAnyNameIsNamed()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, R"(annotation Tag on type;
namespace A
{
	annotation Tag(int32 n) on type;
	[Tag(1)] struct In { public int32 v; }
	namespace B { [Tag(2)] struct Deep { public int32 v; } }
}
[Tag] struct Out { public int32 v; }
[A::Tag(3)] struct Qualified { public int32 v; }
namespace C { [Tag] struct Other { public int32 v; } }
namespace A { struct Holder { [Tag(4)] struct Nested { public int32 v; } public int32 w; } }
)");
		TEST_ASSERT(compiler.GetAllDiagnostics().Empty());
		TEST_ASSERT(NamesOn(model, "In") == "A::Tag"); // the namespace's own hides the global one
		TEST_ASSERT(NamesOn(model, "Deep") == "A::Tag"); // from an inner namespace, out
		TEST_ASSERT(NamesOn(model, "Out") == "Tag");
		TEST_ASSERT(NamesOn(model, "Qualified") == "A::Tag");
		TEST_ASSERT(NamesOn(model, "Other") == "Tag"); // another namespace sees the global one
		TEST_ASSERT(NamesOn(model, "Nested") == "A::Tag"); // from a type, out through its namespace
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AnInnerOneHidesAnOuterOne()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(annotation Tag on type;
namespace A { annotation Tag(int32 n) on type; [Tag] struct NoArgument { public int32 v; } }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "[Tag] takes 1 argument (n), not 0").Count(), 1); // the global [Tag] is not tried
		TEST_ASSERT_EQ(Errors(compiler), 1);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::ANamespacesOneIsNotSeenOutsideIt()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(namespace Scene { annotation Range(double min, double max) on field; }
struct S { [Range(0, 1)] public float a; [Scene::Range(0, 1)] public float b; }
namespace Scene { struct T { [Range(0, 1)] public float c; } }
namespace Other { struct U { [Range(0, 1)] public float d; } }
annotation Global on type;
[Scene::Global] struct V { public int32 v; }
)");
		auto outside = Saying(compiler, "annotation [Range] is not visible here: Scene::Range is declared in namespace Scene: write [Scene::Range]");
		TEST_ASSERT_EQ((int)outside.Count(), 2); // at global scope and in another namespace
		TEST_ASSERT(At(outside[0], 2, 13));
		TEST_ASSERT_EQ((int)Saying(compiler, "annotation [Scene::Global] is not visible here: Global is declared in the global namespace: write [Global]").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 3); // b and c see it
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AMisspeltOneIsLookedUpWhereItIsWritten()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(namespace Scene { annotation Range(double min, double max) on field; struct S { [Rang(0, 1)] public float a; } }
struct T { [Scene::Rang(0, 1)] public float b; [Rang(0, 1)] public float c; }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "unknown annotation 'Rang': did you mean 'Range'?").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "unknown annotation 'Scene::Rang': did you mean 'Range'?").Count(), 1);
		auto unseen = Saying(compiler, "annotation [Rang] is not declared");
		TEST_ASSERT_EQ((int)unseen.Count(), 1); // outside Scene, nothing it sees is near it
		TEST_ASSERT(unseen[0].severity == Scripting::ErrorSeverity::Warning);
		TEST_ASSERT_EQ(Errors(compiler), 2);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AMisspeltOneIsAnError()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, std::string(kLibrary) + "struct M { public int32 v; }\n[Component, Requiers(M)] struct P { public int32 v; }\n");
		auto errors = Saying(compiler, "unknown annotation 'Requiers': did you mean 'Requires'? (an annotation nothing declares is kept for reflection, "
			"and nothing that reads [Requires] sees it)");
		TEST_ASSERT_EQ((int)errors.Count(), 1);
		TEST_ASSERT(At(errors[0], 5, 13));
		TEST_ASSERT(errors[0].severity == Scripting::ErrorSeverity::Error);
		Scripting::CompilerInstance cased;
		MakeModel(cased, std::string(kLibrary) + "[requires] struct Q { public int32 v; }\n");
		TEST_ASSERT_EQ((int)Saying(cased, "unknown annotation 'requires': did you mean 'Requires'").Count(), 1); // case counts
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::EveryKindOfDeclarationIsChecked()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, std::string(kLibrary) + R"(struct F { [Componnt] public int32 field; [Requirs] public void Method([Componet] int32 p) { } }
[Requres] int32 Free() { return 0; }
[Compnent] int32 g_global = 1;
[Requirez] enum class E { A }
union U { [Componnent] int32 i; float f; }
)");
		for (const char* misspelt : { "'Componnt'", "'Requirs'", "'Componet'", "'Requres'", "'Compnent'", "'Requirez'", "'Componnent'" })
			TEST_ASSERT_EQ((int)Saying(compiler, (std::string("unknown annotation ") + misspelt).c_str()).Count(), 1);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AnUndeclaredOneIsAWarning()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, std::string(kLibrary) + "[Serialize, Tint(1, 2)] struct U { [Lib::Hidden] public int32 v; }\n");
		TEST_ASSERT_EQ(Errors(compiler), 0);
		auto serialize = Saying(compiler, "annotation [Serialize] is not declared: declare it, what it takes and where it goes, with `annotation Serialize(...) on ...;`");
		TEST_ASSERT_EQ((int)serialize.Count(), 1);
		TEST_ASSERT(serialize[0].severity == Scripting::ErrorSeverity::Warning);
		TEST_ASSERT(At(serialize[0], 4, 2));
		TEST_ASSERT_EQ((int)Saying(compiler, "annotation [Tint] is not declared").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "annotation [Lib::Hidden] is not declared: declare it, what it takes and where it goes, with `annotation Hidden(").Count(), 1);
		Scripting::CompilerInstance typo;
		MakeModel(typo, "[Test] void F() { }\nint32 main() { return 0; }\n");
		TEST_ASSERT_EQ((int)typo.GetAllDiagnostics().Count(), 1); // the parser's "did you mean 'test'", not a second warning
		TEST_ASSERT_EQ((int)Saying(typo, "did you mean 'test'").Count(), 1);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::TheClosestIsNamed()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "annotation Hidden; annotation Hinted; annotation Bidden;\n");
		const Scripting::DeclaredAnnotations& declared = model.Annotations();
		TEST_ASSERT(declared.MisspeltBy("Hiddem", {})[0]->name == "Hidden"); // one edit beats two
		TEST_ASSERT(declared.MisspeltBy("Xidden", {})[0]->name == "Bidden"); // a tie: the name first in order, whatever the map's
		TEST_ASSERT(declared.MisspeltBy("Hxxxxx", {}).Empty());
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::TheArgumentCountIsChecked()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(annotation None;
annotation Two(int32 a, int32 b);
annotation Optional(int32 a, int32 b = 2, int32 c = 3);
annotation AtLeast(int32 first, int32... more);
[None(1), Two(1), Optional, AtLeast] struct S { public int32 v; }
[Two(1, 2, 3)] struct T { public int32 v; }
[Optional(1, 2, 3, 4)] struct U { public int32 v; }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "[None] takes no arguments, not 1").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Two] takes 2 arguments (a, b), not 1").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Optional] takes 1 to 3 arguments (a, b, c), not 0").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[AtLeast] takes at least 1 argument (first, more...), not 0").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Two] takes 2 arguments (a, b), not 3").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Optional] takes 1 to 3 arguments (a, b, c), not 4").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 6);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::EachArgumentsKindIsChecked()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(struct Mesh { public int32 v; }
annotation Range(double min, double max);
annotation Label(const char* text);
annotation On(bool on);
annotation Of(typename type);
annotation Count(int32 n);
[Range(0, "far")] struct A { public int32 v; }
[Label(3), On(1), Of("Mesh"), Count(0.5)] struct B { public int32 v; }
[Range(true, Mesh), Label(Mesh), Of(Mesh), Count(2), On(false), Label("ok")] struct C { public int32 v; }
)");
		auto far = Saying(compiler, "[Range] argument 2 ('max') is a double: \"far\" is a string");
		TEST_ASSERT_EQ((int)far.Count(), 1);
		TEST_ASSERT(At(far[0], 7, 11)); // at the argument
		TEST_ASSERT_EQ((int)Saying(compiler, "[Label] argument 1 ('text') is a string: 3 is an integer").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[On] argument 1 ('on') is a bool: 1 is an integer").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Of] argument 1 ('type') is a type: \"Mesh\" is a string").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Count] argument 1 ('n') is an int32: 0.5 is a number with a fraction").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Range] argument 1 ('min') is a double: true is a bool").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Label] argument 1 ('text') is a string: 'Mesh' is a type").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 7); // C's [Of], [Count], [On] and second [Label] fit
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AnIntegerMustFitItsParameter()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(annotation Byte(uint8 b);
annotation Small(int8 s);
annotation Unsigned(uint64 u);
[Byte(255), Small(-128), Unsigned(0)] struct A { public int32 v; }
[Byte(256)] struct B { public int32 v; }
[Small(-129)] struct C { public int32 v; }
[Unsigned(-1)] struct D { public int32 v; }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "[Byte] argument 1 ('b') is a uint8: 256 does not fit in a uint8").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Small] argument 1 ('s') is an int8: -129 does not fit in an int8").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Unsigned] argument 1 ('u') is a uint64: -1 does not fit in a uint64").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 3);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AnEnumParameterTakesItsEnumerators()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, R"(namespace Render { enum class Pass { Opaque, Glass } enum class Other { Glass } enum Plain { First, Second } }
annotation Drawn(Render::Pass pass);
annotation Kind(Render::Plain kind);
[Drawn(Render::Pass::Glass)] struct A { public int32 v; }
namespace Render { [Drawn(Pass::Opaque), Kind(Second)] struct B { public int32 v; } }
[Drawn(Render::Other::Glass)] struct C { public int32 v; }
[Drawn(1)] struct D { public int32 v; }
[Drawn("Glass")] struct E { public int32 v; }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "[Drawn] argument 1 ('pass') is a Render::Pass value: Render::Other::Glass is none of its enumerators (Render::Pass::Opaque, Render::Pass::Glass)").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Drawn] argument 1 ('pass') is a Render::Pass value: 1 is none of its enumerators").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Drawn] argument 1 ('pass') is a Render::Pass value: \"Glass\" is a string").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 3);
		auto glass = Registered(model, "A");
		TEST_ASSERT_EQ((int)glass.Count(), 1);
		TEST_ASSERT(glass[0].arguments[0].value.kind == Scripting::ConstantValue::Kind::Int64 && glass[0].arguments[0].value.intValue == 1); // its value
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::OverloadsAreChosenByTheirArguments()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, R"(struct Mesh { public int32 v; }
annotation Range(int64 min, int64 max);
annotation Range(double min, double max);
annotation Of(typename type);
annotation Of(const char* name);
annotation Pick(int64 a);
annotation Pick(int32 a);
[Range(0, 1), Of(Mesh)] struct Ints { public int32 v; }
[Range(0, 1.5), Of("Mesh")] struct Mixed { public int32 v; }
[Pick(1)] struct Tie { public int32 v; }
[Of(true)] struct None { public int32 v; }
)");
		auto ambiguous = Saying(compiler, "[Pick] with these arguments is ambiguous: Pick(int64 a) and Pick(int32 a) each take them");
		TEST_ASSERT_EQ((int)ambiguous.Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "no [Of] takes these arguments (Of(typename type) argument 1 ('type') is a type: true is a bool; "
			"Of(const char* name) argument 1 ('name') is a string: true is a bool)").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 2);
		auto ints = Registered(model, "Ints");
		TEST_ASSERT(ints[0].declaration && ints[0].declaration->ToString() == "Range(int64 min, int64 max)"); // exact beats converted
		TEST_ASSERT(ints[0].arguments[1].value.kind == Scripting::ConstantValue::Kind::Int64);
		TEST_ASSERT(ints[1].declaration->ToString() == "Of(typename type)");
		auto mixed = Registered(model, "Mixed");
		TEST_ASSERT(mixed[0].declaration->ToString() == "Range(double min, double max)");
		TEST_ASSERT(mixed[1].declaration->ToString() == "Of(const char* name)");
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::ReflectionRegistersWhatTheDeclarationTakes()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, R"(annotation Range(double min, double max) on field;
annotation Step(double by = 0.25, bool snap = true) on field;
annotation Note(const char* text = "none") on field;
struct S { [Range(0, 2), Step, Note] public float a; [Step(1)] public float b; [Free(1)] public float c; }
)");
		TEST_ASSERT_EQ(Errors(compiler), 0);
		auto a = Registered(model, "S", "a");
		TEST_ASSERT_EQ((int)a.Count(), 3);
		TEST_ASSERT(a[0].arguments[0].value.kind == Scripting::ConstantValue::Kind::Float64 && a[0].arguments[0].value.floatValue == 0.0); // an integer for a double
		TEST_ASSERT(a[0].arguments[1].value.kind == Scripting::ConstantValue::Kind::Float64 && a[0].arguments[1].value.floatValue == 2.0);
		TEST_ASSERT_EQ((int)a[1].arguments.Count(), 2); // its defaults
		TEST_ASSERT(a[1].arguments[0].value.floatValue == 0.25 && a[1].arguments[1].value.booleanValue);
		TEST_ASSERT(a[2].arguments[0].value.stringValue == "none");
		auto b = Registered(model, "S", "b");
		TEST_ASSERT(b[0].arguments[0].value.kind == Scripting::ConstantValue::Kind::Float64 && b[0].arguments[0].value.floatValue == 1.0);
		TEST_ASSERT(b[0].arguments[1].value.booleanValue); // the default after what is written
		auto c = Registered(model, "S", "c");
		TEST_ASSERT(!c[0].declaration && c[0].arguments[0].value.kind == Scripting::ConstantValue::Kind::Int64); // undeclared: as written
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::APackTakesAnyNumber()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, std::string(kLibrary) + R"(annotation Groups(const char*... names) on type;
struct A { public int32 v; } struct B { public int32 v; }
[Groups, Requires(A)] struct One { public int32 v; }
[Groups("x", "y", "z"), Requires(A, B, A)] struct Three { public int32 v; }
[Requires("A", "B")] struct Named { public int32 v; }
[Requires(A, "B")] struct Mixed { public int32 v; }
[Requires] struct None { public int32 v; }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "no [Requires] takes these arguments").Count(), 2); // a type and a string; nothing
		TEST_ASSERT_EQ(Errors(compiler), 2);
		auto one = Registered(model, "One");
		TEST_ASSERT(one[0].arguments.Empty() && one[1].arguments.Count() == 1 && one[1].arguments[0].type);
		auto three = Registered(model, "Three");
		TEST_ASSERT(three[0].arguments.Count() == 3 && three[0].arguments[2].value.stringValue == "z");
		TEST_ASSERT(three[1].arguments.Count() == 3 && three[1].arguments[2].type);
		auto named = Registered(model, "Named");
		TEST_ASSERT(named[0].declaration->ToString() == "Requires(const char* typeName, const char*... more)");
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::PlacesAreChecked()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(annotation OnType on type;
annotation Range(double min, double max) on field, parameter;
annotation Handler on method;
annotation Entry on function;
annotation Setting on variable;
annotation Flags on enum, union;
annotation Anywhere;
[OnType, Anywhere, Range(0, 1)] struct S
{
	[Range(0, 1), OnType, Anywhere] public float speed;
	[Handler] public void Good([Range(0, 1)] float d) { }
	[Entry] public void Bad() { }
	[Handler] public static void AlsoGood() { }
}
[Entry] void Run() { }
[Handler] void NotAMethod() { }
[Setting] int32 g_good = 1;
[Flags] enum class E { A }
[Flags] union U { int32 i; float f; }
[Setting, Flags] struct T { [Setting] public int32 field; }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "[Range] goes on a field or a parameter, not on a type").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[OnType] goes on a type, not on a field").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Entry] goes on a function, not on a method").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Handler] goes on a method, not on a function").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Setting] goes on a variable, not on a type").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Flags] goes on an enum or a union, not on a type").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "[Setting] goes on a variable, not on a field").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 7);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::ADeclarationThatDeclaresNothingIsAnError()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(struct Mesh { public int32 v; }
annotation BadPlace on thing;
annotation BadType(Mesh m);
annotation Unknown(Nowhere n);
annotation BadOrder(int32 a = 1, int32 b);
annotation PackFirst(int32... a, int32 b);
annotation TypeDefault(typename t = 1);
annotation BadDefault(int32 n = "x");
annotation test;
annotation annotation;
annotation Twice(int32 a);
annotation Twice(int32 b);
annotation TwoFields on field, parameter, field;
annotation Fine(int32 a, int32 b = 2, int32... rest);
)");
		auto place = Saying(compiler, "'thing' is no place an annotation goes: one of type, enum, union, field, method, function, parameter, variable");
		TEST_ASSERT_EQ((int)place.Count(), 1);
		TEST_ASSERT(At(place[0], 2, 24)); // at the place
		TEST_ASSERT_EQ((int)Saying(compiler, "an annotation parameter is a bool, an integer, a float or double, a const char*, an enum or typename: 'Mesh' is none of them").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "'Nowhere' names no type").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "a parameter with a default is followed only by parameters with one, or a pack: 'int32 b' has none").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "a pack is an annotation's last parameter: 'int32... a' is followed by more").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "'typename t' takes no default").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "the default of 'int32 n' is an int32: \"x\" is a string").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "'test' is the compiler's own annotation").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "'annotation' starts an annotation's declaration: it names none").Count(), 1);
		auto twice = Saying(compiler, "[Twice] is declared twice with the same parameters: Twice(int32 a)");
		TEST_ASSERT_EQ((int)twice.Count(), 1);
		TEST_ASSERT(At(twice[0], 12, 12)); // at the second one's name
		TEST_ASSERT_EQ((int)Saying(compiler, "[TwoFields] goes on a field once: 'field' is written twice").Count(), 1);
		TEST_ASSERT_EQ(Errors(compiler), 11);
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::ItsSyntaxErrorsAreSaidOnce()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(annotation Open(int32 a;
annotation NoPlace on 3;
annotation Missing field;
annotation Dots(int32 a, ...);
annotation Fine(int32 a) on field;
struct S { annotation Inner on field; public int32 v; }
[annotation("type")] void Old();
namespace N { annotation Last on type }
struct T { [Fine(2)] public int32 v; }
)");
		TEST_ASSERT_EQ((int)Saying(compiler, "Expected ')' to close the parameters of [Open], got ';'").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "Expected a place [NoPlace] goes (type, field, ...), got '3'").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "Expected '(', 'on' or ';' after the declaration of [Missing], got 'field'").Count(), 1);
		TEST_ASSERT_EQ((int)Saying(compiler, "an annotation takes no C '...': name what it takes as a pack").Count(), 1);
		auto inner = Saying(compiler, "[Inner] is declared at namespace scope, not in a type");
		TEST_ASSERT_EQ((int)inner.Count(), 1);
		TEST_ASSERT(At(inner[0], 6, 12));
		auto old = Saying(compiler, "[annotation] declares nothing: an annotation is declared by `annotation Name(parameters) on places;` at namespace scope");
		TEST_ASSERT_EQ((int)old.Count(), 1);
		TEST_ASSERT(At(old[0], 7, 2));
		TEST_ASSERT_EQ((int)Saying(compiler, "Expected ',' or ';' after the declaration of [Last], got '}'").Count(), 1); // the namespace keeps its brace
		TEST_ASSERT_EQ(Errors(compiler), 7); // each said once: what follows parses
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::AnnotationIsStillAName()
	{
		Scripting::CompilerInstance compiler;
		MakeModel(compiler, R"(struct Annotation { public int32 v; }
Annotation annotation;
int32 g_annotation = 1;
struct Holder { public int32 annotation; }
int32 Use(int32 annotation) { return annotation; }
int32 main()
{
	int32 annotation = 2;
	annotation = annotation + Use(annotation);
	Holder holder;
	holder.annotation = annotation;
	return holder.annotation;
}
)");
		TEST_ASSERT(compiler.GetAllDiagnostics().Empty());
		TEST_PASSED();
	}

	void DeclaredAnnotationsTest::ADeclarationIsNoFunction()
	{
		// A call to one is the error NegativeTest AnnotationCalled says.
		Scripting::CompilerInstance unused;
		const auto& model = MakeModel(unused, "annotation Marked;\n[Marked] int32 main() { return 0; }\n");
		TEST_ASSERT(unused.GetAllDiagnostics().Empty());
		Core::Vector<const Scripting::FunctionRecord*> records;
		model.AllFunctionRecords(records);
		for (const Scripting::FunctionRecord* record : records) TEST_ASSERT(record->name != "Marked"); // never a function of the program
		TEST_PASSED();
	}
}
