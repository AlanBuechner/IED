#include "ReflectedAnnotationsTest.h"
#include "Model/Annotations.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"

namespace Testing
{
	namespace
	{
		const C4Compiler::ProgramModel& MakeModel(C4Compiler::CompilerInstance& compiler, const char* source)
		{
			uint32 id = compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetCompilationUnit(id)->GetProgramModel();
		}

		C4Compiler::QueryResult<Core::Vector<C4Compiler::ReflectedAnnotation>> TypeAnnotations(const C4Compiler::ProgramModel& model, const char* name)
		{
			for (const C4Compiler::ObjectType* object : model.ObjectTypes())
				if (object && object->GetName() == name)
				{
					Core::Vector<const C4Compiler::AstNode*> declarations;
					declarations.Push(object->GetDeclNode());
					return C4Compiler::ReflectedAnnotations(model, declarations, C4Compiler::AnnotationPlace::Type, model.TypeScope(object));
				}
			return C4Compiler::QueryResult<Core::Vector<C4Compiler::ReflectedAnnotation>>::Fail(C4Compiler::QueryError::NotFound, name);
		}

		Core::String TypeName(const C4Compiler::ReflectedArgument& argument)
		{
			return argument.type ? C4Compiler::QualifiedName(argument.type) : Core::String("(none)");
		}

		bool HasError(const C4Compiler::CompilerInstance& compiler, const char* text)
		{
			for (const C4Compiler::CompilationError& error : compiler.GetAllErrors())
				if (error.message.find(text) != Core::String::npos) return true;
			return false;
		}

		const C4Compiler::AnnotationItem* FirstAnnotationOf(const C4Compiler::ProgramModel& model, const char* name)
		{
			for (const C4Compiler::ObjectType* object : model.ObjectTypes())
				if (object && object->GetName() == name)
					if (const auto* declaration = dynamic_cast<const C4Compiler::ObjectDefinitionNode*>(object->GetDeclNode()))
						return declaration->GetAnnotations()[0]->GetBracketGroups()[0].annotations[0].Get();
			return nullptr;
		}
	}

	void ReflectedAnnotationsTest::ArgumentsFold()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace L { const int32 kMax = 10; }\n"
			"[Range(-1, L::kMax * 2), Name(\"x\"), On(true), Scale(0.25)] struct S { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "S");
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT_EQ((int)annotations.value.Count(), 4);
		const auto& range = annotations.value[0];
		TEST_ASSERT(range.name == "Range");
		TEST_ASSERT(range.arguments[0].value.kind == C4Compiler::ConstantValue::Kind::Int64 && range.arguments[0].value.intValue == -1);
		TEST_ASSERT(range.arguments[1].value.intValue == 20 && !range.arguments[1].type);
		TEST_ASSERT(annotations.value[1].arguments[0].value.kind == C4Compiler::ConstantValue::Kind::String);
		TEST_ASSERT(annotations.value[1].arguments[0].value.stringValue == "x");
		TEST_ASSERT(annotations.value[2].arguments[0].value.kind == C4Compiler::ConstantValue::Kind::Bool && annotations.value[2].arguments[0].value.booleanValue);
		TEST_ASSERT(annotations.value[3].arguments[0].value.kind == C4Compiler::ConstantValue::Kind::Float64 && annotations.value[3].arguments[0].value.floatValue == 0.25);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::CompilerAnnotationsAreSkipped()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "[group(\"g\"), Tag(1)] struct T { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "T");
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT_EQ((int)annotations.value.Count(), 1);
		TEST_ASSERT(annotations.value[0].name == "Tag");
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::NonConstantArgumentFails()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "int32 g_level = 3;\n[Range(0, g_level)] struct U { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "U");
		TEST_ASSERT(!annotations.Ok());
		TEST_ASSERT(annotations.message.find("must be a constant") != Core::String::npos);
		TEST_ASSERT_EQ((int)annotations.loc.line, 2);
		TEST_ASSERT_EQ((int)annotations.loc.column, 11);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::DeclarationThenDefinition()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "[Declared] int32 F();\n[Defined(2)] int32 F() { return 1; }\n");
		Core::Vector<const C4Compiler::FunctionRecord*> records;
		model.AllFunctionRecords(records);
		const C4Compiler::FunctionRecord* f = nullptr;
		for (const C4Compiler::FunctionRecord* record : records)
			if (record->name == "F") f = record;
		TEST_ASSERT(f != nullptr);
		auto annotations = C4Compiler::ReflectedAnnotations(model, f->declarations, C4Compiler::AnnotationPlace::Function, f->scope);
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT_EQ((int)annotations.value.Count(), 2);
		TEST_ASSERT(annotations.value[0].name == "Declared");
		TEST_ASSERT(annotations.value[1].name == "Defined");
		TEST_ASSERT(annotations.value[1].arguments[0].value.intValue == 2);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::NoAnnotations()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct V { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "V");
		TEST_ASSERT(annotations.Ok());
		TEST_ASSERT(annotations.value.Empty());
		Core::Vector<const C4Compiler::AstNode*> none;
		TEST_ASSERT(C4Compiler::ReflectedAnnotations(model, none, C4Compiler::AnnotationPlace::Type, nullptr).value.Empty());
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::TypeArgumentsResolve()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace R { struct Mesh { public int32 v; } [Requires(Mesh)] struct Renderer { public int32 v; } }\n"
			"template<T> struct Box { public T value; }\n"
			"alias MeshAlias = R::Mesh;\n"
			"[Requires(R::Mesh, Box<int32>, MeshAlias), Holds(int32, R::Mesh*, const R::Mesh&)] struct Collider { public int32 v; }\n");
		auto collider = TypeAnnotations(model, "Collider");
		TEST_ASSERT(collider.Ok());
		const auto& required = collider.value[0].arguments;
		TEST_ASSERT(TypeName(required[0]) == "R::Mesh" && required[0].value.IsNone());
		TEST_ASSERT(TypeName(required[1]) == "Box<int32>");
		TEST_ASSERT(TypeName(required[2]) == "R::Mesh"); // an alias is the type it names
		const auto& holds = collider.value[1].arguments;
		TEST_ASSERT(TypeName(holds[0]) == "int32");
		TEST_ASSERT(TypeName(holds[1]) == "R::Mesh*");
		TEST_ASSERT(holds[2].type != nullptr);
		auto renderer = TypeAnnotations(model, "Renderer");
		TEST_ASSERT(renderer.Ok() && TypeName(renderer.value[0].arguments[0]) == "R::Mesh"); // unqualified, from its namespace
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::ConstantNamesStayConstants()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace L { const int32 kMax = 10; enum class Mode { A, B } }\n"
			"struct kLocal { public int32 v; }\n"
			"[Range(L::kMax, L::Mode::B), Kind(kLocal)] struct S { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "S");
		TEST_ASSERT(annotations.Ok());
		const auto& range = annotations.value[0].arguments;
		TEST_ASSERT(!range[0].type && range[0].value.intValue == 10);
		TEST_ASSERT(!range[1].type && range[1].value.intValue == 1); // an enum member is a constant
		TEST_ASSERT(TypeName(annotations.value[1].arguments[0]) == "kLocal");
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::UnknownNameIsNeither()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "namespace R { struct Mesh { public int32 v; } }\n[Requires(R::Mesj)] struct T { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "T");
		TEST_ASSERT(!annotations.Ok());
		TEST_ASSERT(annotations.message.find("must be a constant number, bool or string, or a type") != Core::String::npos);
		TEST_ASSERT(annotations.message.find("'R::Mesj' is neither a constant (") != Core::String::npos);
		TEST_ASSERT(annotations.message.find(") nor a type (") != Core::String::npos);
		TEST_ASSERT_EQ((int)annotations.loc.line, 2);
		TEST_ASSERT_EQ((int)annotations.loc.column, 11);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::UnknownTypeNamesNoType()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "[Points(Nowhere*)] struct U { public int32 v; }\n");
		auto annotations = TypeAnnotations(model, "U");
		TEST_ASSERT(!annotations.Ok());
		TEST_ASSERT(annotations.message.find("'Nowhere*' names no type") != Core::String::npos);
		TEST_ASSERT_EQ((int)annotations.loc.column, 9);
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::NameOfIsAString()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace C { template<T> struct Arr { public T* items; } }\n"
			"namespace N { struct P { public int32 v; } [Named(nameof(P), nameof(C::Arr<int32>))] struct Q { public int32 v; } }\n");
		auto annotations = TypeAnnotations(model, "Q");
		TEST_ASSERT(annotations.Ok());
		const auto& named = annotations.value[0].arguments;
		TEST_ASSERT(!named[0].type && named[0].value.kind == C4Compiler::ConstantValue::Kind::String);
		TEST_ASSERT(named[0].value.stringValue == "N::P"); // BUG-463, from the declaration's namespace
		TEST_ASSERT(named[1].value.stringValue == "C::Arr<int32>");
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::TheModelReportsWhatDoesNotFold()
	{
		C4Compiler::CompilerInstance bad;
		MakeModel(bad, "struct Field { [Of(Nowhere)] public int32 v; public void F([From(Missing)] int32 x) { } }\n");
		TEST_ASSERT(HasError(bad, "'Nowhere' is neither a constant")); // a field's: before any code is made, an editor sees it
		TEST_ASSERT(HasError(bad, "'Missing' is neither a constant"));  // a parameter's
		C4Compiler::CompilerInstance good;
		MakeModel(good, "namespace R { struct Mesh { public int32 v; } }\n[Requires(R::Mesh)] struct C { public int32 v; }\n");
		TEST_ASSERT(good.GetAllErrors().Empty());
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::TheModelMakesTheInstancesTheyName()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "template<T> struct Box { public T value; public T Get() { return value; } }\n[Holds(Box<float>)] struct C { public int32 v; }\n");
		TEST_ASSERT(compiler.GetAllErrors().Empty());
		const C4Compiler::ObjectType* made = nullptr;
		for (const C4Compiler::ObjectType* object : model.ObjectTypes())
			if (object && C4Compiler::QualifiedName(object) == "Box<float>") made = object;
		TEST_ASSERT(made != nullptr);                      // nothing but the annotation names it
		TEST_ASSERT(made->IsDefined() && made->GetSize() == 4); // laid out before lowering
		TEST_PASSED();
	}

	void ReflectedAnnotationsTest::ArgumentsReadAsTypes()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace R { struct Mesh { public int32 v; } }\n[Mixed(R::Mesh, 2 + 3, \"text\", R::Mesh*, Name)] struct M { public int32 v; }\n");
		const C4Compiler::AnnotationItem* item = FirstAnnotationOf(model, "M");
		TEST_ASSERT(item != nullptr && item->GetArguments().Count() == 5);
		TEST_ASSERT(item->GetArgumentType(0) && item->GetArguments()[0].Get() != item->GetArgumentType(0).Get()); // a name: both
		TEST_ASSERT(!item->GetArgumentType(1));                                                                   // an expression
		TEST_ASSERT(!item->GetArgumentType(2));                                                                   // a string
		TEST_ASSERT(item->GetArguments()[3].Get() == item->GetArgumentType(3).Get());                             // a type alone
		TEST_ASSERT(item->GetArgumentType(4) != nullptr);
		TEST_PASSED();
	}
}
