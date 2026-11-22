#include "ModelParityTest.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Model/OverloadResolution.h"
#include <functional>

namespace Testing
{
	// Build the program model for one in-memory unit and hand back a reference to it (the model is
	// move-only - it owns its data, doc 03 - so phases hold a reference to the one shared model). The
	// caller keeps the compiler alive for as long as it uses the returned reference.
	static const C4Compiler::ProgramModel& MakeModel(C4Compiler::CompilerInstance& compiler, const char* source)
	{
		uint32 id = compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto unit = compiler.GetCompilationUnit(id);
		return unit->GetProgramModel();
	}

	void ModelParityTest::TypesAndLayoutMatchHandComputed()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct P { int32 x; float y; }\n"   // size 8, align 4
			"struct Q { double d; int32 i; }\n"   // size 16 (padded to the double's 8-align), align 8
			"int32[4] arr;\n");

		auto p = model.QueryType("P");
		TEST_ASSERT(p.Ok());
		TEST_ASSERT_EQ((int)model.SizeOf(p.value).value, 8);
		TEST_ASSERT_EQ((int)model.AlignOf(p.value).value, 4);

		auto q = model.QueryType("Q");
		TEST_ASSERT(q.Ok());
		TEST_ASSERT_EQ((int)model.SizeOf(q.value).value, 16);
		TEST_ASSERT_EQ((int)model.AlignOf(q.value).value, 8);

		auto a = model.QueryVariable("arr");
		TEST_ASSERT(a.Ok());
		TEST_ASSERT_EQ((int)model.SizeOf(a.value.type).value, 16); // 4 * sizeof(int32)
		const C4Compiler::Type* elem = model.ElementOf(a.value.type);
		TEST_ASSERT(elem != nullptr);
		TEST_ASSERT_EQ((int)elem->GetSize(), 4);
		TEST_PASSED();
	}

	void ModelParityTest::QualifiedNamesResolveToConcreteTypes()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace Engine {\n"
			"    struct Transform { float x; float y; }\n"   // size 8, align 4
			"    int32 AxisCount() { return 2; }\n"
			"}\n");

		auto t = model.QueryType("Engine::Transform");
		TEST_ASSERT(t.Ok());
		TEST_ASSERT_EQ((int)model.SizeOf(t.value).value, 8);

		auto f = model.QueryFunction("Engine::AxisCount");
		TEST_ASSERT(f.Ok());
		TEST_ASSERT_EQ((int)f.value->GetParameterCount(), 0);

		// A name not in the namespace is NotFound, not a wrong hit.
		auto none = model.QueryType("Engine::Missing");
		TEST_ASSERT(none.error == C4Compiler::QueryError::NotFound);
		TEST_PASSED();
	}

	void ModelParityTest::InheritanceSizeIsBasePlusMembers()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Base { int32 a; int32 b; }\n"   // size 8
			"struct Derived : Base { float f; }\n"); // +4 => size 12, align 4

		auto d = model.QueryType("Derived");
		TEST_ASSERT(d.Ok());
		TEST_ASSERT_EQ((int)model.SizeOf(d.value).value, 12);
		TEST_ASSERT_EQ((int)model.AlignOf(d.value).value, 4);
		TEST_PASSED();
	}

	void ModelParityTest::AliasIsTransparentToSameInstance()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct P { int32 x; }\nalias Pt = P;\n");

		auto p = model.QueryType("P");
		auto a = model.QueryType("Pt");
		TEST_ASSERT(p.Ok() && a.Ok());
		TEST_ASSERT(a.value == p.value); // alias unwraps to the SAME instance, not a copy
		TEST_PASSED();
	}

	void ModelParityTest::ConstantsAndEnumsFoldToKnownValues()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace Math {\n"
			"    const int32 K = 41;\n"
			"    enum Color { Red = 1, Green = 5 };\n"
			"}\n");

		auto k = model.QueryConstant("Math::K");
		TEST_ASSERT(k.Ok());
		TEST_ASSERT_EQ((int)k.value.intValue, 41);

		auto g = model.QueryConstant("Math::Green");
		TEST_ASSERT(g.Ok());
		TEST_ASSERT_EQ((int)g.value.intValue, 5);
		TEST_PASSED();
	}

	void ModelParityTest::OperatorsDispatchToTheDeclaringSide()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Plain { int32 v; }\n"
			"struct HasAdd { int32 v; }\n"
			"[operator(\"add\")] HasAdd Add(const HasAdd& o) extends HasAdd { return o; }\n");

		auto hasAdd = model.QueryType("HasAdd");
		auto plain = model.QueryType("Plain");
		TEST_ASSERT(hasAdd.Ok() && plain.Ok());

		// "add" is declared on HasAdd only: it binds to the left receiver when HasAdd is the LHS.
		auto l = model.QueryOperator("add", hasAdd.value, hasAdd.value);
		TEST_ASSERT(l.Ok());
		TEST_ASSERT(l.value.method->mName == "Add");

		// Same operator, HasAdd as the RHS: the left operand is always the receiver (D-25), so nothing binds.
		auto r = model.QueryOperator("add", plain.value, hasAdd.value);
		TEST_ASSERT(r.error == C4Compiler::QueryError::NotFound);

		// No operand declares "multiply": a clean NotFound, not a wrong pick.
		auto none = model.QueryOperator("multiply", hasAdd.value, plain.value);
		TEST_ASSERT(none.error == C4Compiler::QueryError::NotFound);
		TEST_PASSED();
	}

	void ModelParityTest::TemplateInstantiationHasConcreteLayout()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> class TBox { public T v; }\n"
			"TBox<int32> g;\n");

		const C4Compiler::QualTypeNode* spec = nullptr;
		std::function<void(const C4Compiler::AstNode&)> walk = [&](const C4Compiler::AstNode& n) {
			if (spec) return;
			if (const auto* v = dynamic_cast<const C4Compiler::VariableDefinitionNode*>(&n)) {
				const auto& t = v->GetType();
				if (t && t->HasTemplateArgs()) { spec = t.Get(); return; }
			}
			for (const auto& child : n.Children())
				walk(*child.Get());
		};
		auto unit = compiler.GetCompilationUnit(1);
		if (unit) walk(*unit->GetRootAST().Get());
		TEST_ASSERT(spec != nullptr);

		auto r = model.QueryInstantiation(*spec);
		TEST_ASSERT(r.Ok());
		// TBox<int32> is a single int32 member: size 4, align 4.
		TEST_ASSERT_EQ((int)model.SizeOf(r.value).value, 4);
		TEST_ASSERT_EQ((int)model.AlignOf(r.value).value, 4);

		auto again = model.QueryInstantiation(*spec);
		TEST_ASSERT(again.Ok());
		TEST_ASSERT(again.value == r.value); // cached: the same instance, not a second instantiation
		TEST_PASSED();
	}

	// Find the parsed type node of the file-scope variable named `varName`.
	static const C4Compiler::QualTypeNode* FindVarType(C4Compiler::CompilerInstance& compiler, const char* varName)
	{
		const C4Compiler::QualTypeNode* found = nullptr;
		std::function<void(const C4Compiler::AstNode&)> walk = [&](const C4Compiler::AstNode& n) {
			if (found) return;
			if (const auto* v = dynamic_cast<const C4Compiler::VariableDefinitionNode*>(&n)) {
				const auto& t = v->GetType();
				if (t && v->GetNameToken().GetText() == varName) { found = t.Get(); return; }
			}
			for (const auto& child : n.Children())
				walk(*child.Get());
		};
		auto unit = compiler.GetCompilationUnit(1);
		if (unit) walk(*unit->GetRootAST().Get());
		return found;
	}

	void ModelParityTest::ForwardDeclaredTypeIsIncompleteButUsable()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Fwd;\n"
			"struct H { Fwd* p; }\n");

		// A forward-declared type resolves (the shell is a real symbol) but has no layout yet.
		auto fwd = model.QueryType("Fwd");
		TEST_ASSERT(fwd.Ok());
		TEST_ASSERT_EQ((int)model.SizeOf(fwd.value).value, 0); // incomplete: size not computed

		// A pointer to it needs no layout, so the containing struct is fully sized (8 on x64).
		auto h = model.QueryType("H");
		TEST_ASSERT(h.Ok());
		TEST_ASSERT_EQ((int)model.SizeOf(h.value).value, 8);
		const C4Compiler::ObjectType* obj = dynamic_cast<const C4Compiler::ObjectType*>(h.value);
		TEST_ASSERT(obj != nullptr);
		auto field = model.QueryMember(h.value, "p");
		TEST_ASSERT(field.Ok());
		TEST_ASSERT(field.value.IsField);
		TEST_PASSED();
	}

	void ModelParityTest::OpenNamespaceBlocksMergeWithoutConflict()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace Math { const int32 A = 10; }\n"
			"namespace Math { const int32 B = 20; }\n");

		// Two `namespace Math` blocks merge into one: both members are queryable, no conflict.
		auto a = model.QueryConstant("Math::A");
		auto b = model.QueryConstant("Math::B");
		TEST_ASSERT(a.Ok());
		TEST_ASSERT_EQ((int)a.value.intValue, 10);
		TEST_ASSERT(b.Ok());
		TEST_ASSERT_EQ((int)b.value.intValue, 20);
		TEST_PASSED();
	}

	void ModelParityTest::ScopedAliasInTemplateSubstitutesToConcreteMember()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> class P { alias Type = T; Type val; }\n"
			"P<float> g1;\n");

		const C4Compiler::QualTypeNode* spec = FindVarType(compiler, "g1");
		TEST_ASSERT(spec != nullptr);
		auto inst = model.QueryInstantiation(*spec);
		TEST_ASSERT(inst.Ok());

		// The scoped alias `Type` is substituted: member `val` has the CONCRETE type (float), not the param.
		auto field = model.QueryMember(inst.value, "val");
		TEST_ASSERT(field.Ok());
		TEST_ASSERT(field.value.IsField);
		TEST_ASSERT_EQ((int)field.value.field->m_Type->GetSize(), 4); // sizeof(float)
		TEST_PASSED();
	}

	void ModelParityTest::NonTypeParameterFoldsIntoArraySize()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<int N> struct Arr { int32[N] x; }\n"
			"Arr<4> g2;\n");

		const C4Compiler::QualTypeNode* spec = FindVarType(compiler, "g2");
		TEST_ASSERT(spec != nullptr);
		auto inst = model.QueryInstantiation(*spec);
		TEST_ASSERT(inst.Ok());

		// The non-type arg N=4 folds into the array size: Arr<4> is 4 int32s = 16 bytes.
		TEST_ASSERT_EQ((int)model.SizeOf(inst.value).value, 16);
		auto field = model.QueryMember(inst.value, "x");
		TEST_ASSERT(field.Ok());
		TEST_ASSERT(field.value.IsField);
		const C4Compiler::Type* elem = model.ElementOf(field.value.field->m_Type);
		TEST_ASSERT(elem != nullptr);
		TEST_ASSERT_EQ((int)elem->GetSize(), 4); // int32 element
		TEST_PASSED();
	}

	void ModelParityTest::AbstractTypeIsMarkedAbstract()
	{
		C4Compiler::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"abstract class Shape { virtual float Area() = 0; }\n");

		auto s = model.QueryType("Shape");
		TEST_ASSERT(s.Ok());
		const C4Compiler::ObjectType* obj = dynamic_cast<const C4Compiler::ObjectType*>(s.value);
		TEST_ASSERT(obj != nullptr);
		TEST_ASSERT(obj->IsAbstract()); // the abstract flag is queryable - "cannot new" reads one flag (E4)
		TEST_PASSED();
	}

	// Shared interned primitive per kind, so "exact match" means pointer identity (mirrors the
	// production TypeTable singletons). Distinct from OverloadResolutionTest's copy (file-static).
	static const C4Compiler::Type* ParityPrim(C4Compiler::TypeKind kind)
	{
		static Core::Vector<C4Compiler::PrimitiveType*> table(64, nullptr);
		const int32 idx = (int32)kind;
		if (!table[idx])
			table[idx] = new C4Compiler::PrimitiveType(kind);
		return table[idx];
	}

	static const C4Compiler::FunctionType* ParityFn(const Core::Vector<C4Compiler::TypeKind>& paramKinds)
	{
		const auto* ret = ParityPrim(C4Compiler::TypeKind::Int32);
		Core::Vector<C4Compiler::FunctionType::ParameterInfo> params;
		for (uint32 i = 0; i < paramKinds.Count(); ++i) {
			C4Compiler::FunctionType::ParameterInfo p;
			p.mName = "a";
			p.m_Type = ParityPrim(paramKinds[i]);
			params.Push(p);
		}
		return new C4Compiler::FunctionType(ret, params);
	}

	void ModelParityTest::ResolveOverloadPicksBestAndReportsErrors()
	{
		// Overload resolution is an internal brick (doc 08): there is no public candidate-list verb
		// on the model. The ranking mechanism itself is OverloadResolver::Resolve, which
		// QueryFunction and QueryOperator call internally; this test locks its behavior directly.

		const auto* exact = ParityFn({ C4Compiler::TypeKind::Int32 });   // int32 arg -> cost 0
		const auto* conv  = ParityFn({ C4Compiler::TypeKind::Float32 });  // int32 arg -> cost 1 (numeric)
		Core::Vector<C4Compiler::OverloadCandidate> cands;
		cands.Push({ conv, nullptr, 1 });
		cands.Push({ exact, nullptr, 1 });
		Core::Vector<const C4Compiler::Type*> args;
		args.Push(ParityPrim(C4Compiler::TypeKind::Int32));

		// Resolved: the best signature wins (exact beats numeric conversion).
		auto r = C4Compiler::OverloadResolver::Resolve(cands, args);
		TEST_ASSERT(r.status == C4Compiler::OverloadResolution::Status::Resolved);
		TEST_ASSERT(r.chosen == exact);

		// Ambiguous: two distinct same-category NUMERIC param types both convert from an int32 arg (cost 1
		// each) - a true tie, so it must be reported as a value error, never a silent wrong pick.
		const auto* s1 = ParityFn({ C4Compiler::TypeKind::Int16 });    // int32 -> int16 : cost 1
		const auto* s2 = ParityFn({ C4Compiler::TypeKind::Int64 });    // int32 -> int64 : cost 1
		Core::Vector<C4Compiler::OverloadCandidate> tie;
		tie.Push({ s1, nullptr, 1 });
		tie.Push({ s2, nullptr, 1 });
		Core::Vector<const C4Compiler::Type*> tieArgs;
		tieArgs.Push(ParityPrim(C4Compiler::TypeKind::Int32));
		auto amb = C4Compiler::OverloadResolver::Resolve(tie, tieArgs);
		TEST_ASSERT(amb.status == C4Compiler::OverloadResolution::Status::Ambiguous);

		// Same-category beats cross-category: int32 -> int64 (cost 1) wins over int32 -> float32 (cost 2).
		const auto* crossCat = ParityFn({ C4Compiler::TypeKind::Float32 });
		const auto* sameCat = ParityFn({ C4Compiler::TypeKind::Int64 });
		Core::Vector<C4Compiler::OverloadCandidate> mixed;
		mixed.Push({ crossCat, nullptr, 1 });
		mixed.Push({ sameCat, nullptr, 1 });
		auto picked = C4Compiler::OverloadResolver::Resolve(mixed, tieArgs);
		TEST_ASSERT(picked.status == C4Compiler::OverloadResolution::Status::Resolved);
		TEST_ASSERT(picked.chosen == sameCat);

		// NoMatch: empty candidate set.
		Core::Vector<C4Compiler::OverloadCandidate> none;
		auto no = C4Compiler::OverloadResolver::Resolve(none, args);
		TEST_ASSERT(no.status == C4Compiler::OverloadResolution::Status::NoMatch);
		TEST_PASSED();
	}
} // namespace Testing
