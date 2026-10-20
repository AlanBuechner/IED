#include "ProgramModelEdgeCasesTest.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include <functional>

namespace Testing
{
	// Build the program model for one in-memory unit (mirrors ProgramModelTest's harness). The model
	// is move-only and shared, so the caller keeps the compiler alive for as long as it uses the ref.
	static const Scripting::ProgramModel& MakeModel(Scripting::CompilerInstance& compiler, const char* source)
	{
		uint32 id = compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto unit = compiler.GetCompilationUnit(id);
		return unit->GetProgramModel();
	}

	// First AST node matching the predicate (depth-first). Used to pull a concrete type/call node out
	// of a parsed body so a test can query it by its source-level spelling.
	template <typename Pred>
	static const Scripting::AstNode* FindNode(Scripting::CompilerInstance& compiler, uint32 unitId, Pred pred)
	{
		auto unit = compiler.GetCompilationUnit(unitId);
		if (!unit || !unit->GetRootAST()) return nullptr;
		const Scripting::AstNode* found = nullptr;
		std::function<void(const Scripting::AstNode&)> walk = [&](const Scripting::AstNode& n) {
			if (found) return;
			if (pred(n)) { found = &n; return; }
			for (const auto& child : n.Children())
				walk(*child.Get());
		};
		walk(*unit->GetRootAST().Get());
		return found;
	}

	// --- Templates -------------------------------------------------------------

	void ProgramModelEdgeCasesTest::NestedTemplateInstantiationIsConcrete()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> struct Box { public T v; }\n"
			"template<T> struct Vec { public T x; public Box<T> b; }\n"
			"Vec<int32> g;\n");

		// The `Vec<int32>` type node from the global variable.
		const auto* var = FindNode(compiler, 1, [](const Scripting::AstNode& n) {
			const auto* v = dynamic_cast<const Scripting::VariableDefinitionNode*>(&n);
			return v && v->GetType() && v->GetNameToken().GetText() == "g";
		});
		TEST_ASSERT(var != nullptr);

		auto r = model.QueryInstantiation(*dynamic_cast<const Scripting::VariableDefinitionNode*>(var)->GetType().Get());
		TEST_ASSERT(r.Ok());
		TEST_ASSERT(r.value != nullptr);
		TEST_ASSERT(r.value->GetKind() == Scripting::TypeKind::Object);
		// Vec<int32> = { int32 x; Box<int32> b; } = 4 + 4 = 8 bytes, align 4.
		TEST_ASSERT_EQ((int)model.SizeOf(r.value).value, 8);
		TEST_ASSERT_EQ((int)model.AlignOf(r.value).value, 4);

		// The nested Box<int32> field is itself a concrete instantiation (size 4), not a template shell.
		auto b = model.QueryMember(r.value, "b");
		TEST_ASSERT(b.Ok());
		TEST_ASSERT(b.value.IsField);
		TEST_ASSERT_EQ((int)model.SizeOf(b.value.field->m_Type).value, 4);

		// The same spelling yields the SAME cached instance (templates are indistinguishable + cached).
		auto again = model.QueryInstantiation(*dynamic_cast<const Scripting::VariableDefinitionNode*>(var)->GetType().Get());
		TEST_ASSERT(again.Ok());
		TEST_ASSERT(again.value == r.value);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::AliasTemplateYieldsSameInstantiation()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> struct TBox { public T v; }\n"
			"alias ABox = TBox<float>;\n"
			"alias BBox = TBox<float>;\n");

		// Two aliases to the SAME specialization must resolve to the SAME concrete instance: alias
		// transparency + template caching compose (a missed cache here would create two TBox<float>s).
		auto a = model.QueryType("ABox");
		auto b = model.QueryType("BBox");
		TEST_ASSERT(a.Ok());
		TEST_ASSERT(b.Ok());
		TEST_ASSERT(a.value != nullptr);
		TEST_ASSERT(a.value == b.value);          // one shared instantiation, not two
		TEST_ASSERT_EQ((int)model.SizeOf(a.value).value, 4);   // a single float member
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::NonTypeParameterDrivesArrayLayout()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<int N> struct Arr { int32[N] d; }\n"
			"Arr<4> g;\n");

		const auto* var = FindNode(compiler, 1, [](const Scripting::AstNode& n) {
			const auto* v = dynamic_cast<const Scripting::VariableDefinitionNode*>(&n);
			return v && v->GetType() && v->GetNameToken().GetText() == "g";
		});
		TEST_ASSERT(var != nullptr);

		auto r = model.QueryInstantiation(*dynamic_cast<const Scripting::VariableDefinitionNode*>(var)->GetType().Get());
		TEST_ASSERT(r.Ok());
		// Arr<4> holds int32 d[4] = 16 bytes, align 4. The non-type param N folded into the array size.
		TEST_ASSERT_EQ((int)model.SizeOf(r.value).value, 16);
		TEST_ASSERT_EQ((int)model.AlignOf(r.value).value, 4);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::OperatorOnInstantiatedTypeResolves()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> struct Num {\n"
			"    public T v;\n"
			"    [operator(\"add\")] public T Combine(const Num& o) { return this->v; }\n"
			"}\n"
			"Num<int32> g;\n");

		const auto* var = FindNode(compiler, 1, [](const Scripting::AstNode& n) {
			const auto* v = dynamic_cast<const Scripting::VariableDefinitionNode*>(&n);
			return v && v->GetType() && v->GetNameToken().GetText() == "g";
		});
		TEST_ASSERT(var != nullptr);

		auto r = model.QueryInstantiation(*dynamic_cast<const Scripting::VariableDefinitionNode*>(var)->GetType().Get());
		TEST_ASSERT(r.Ok());
		const Scripting::Type* numInt = r.value;

		// `a + b` on the instantiation: the [operator("add")] method registered on the template body
		// must be discoverable on the concrete Num<int32>. Left-receiver preference.
		auto op = model.QueryOperator("+", numInt, numInt);
		TEST_ASSERT(op.Ok());
		TEST_ASSERT(op.value.method != nullptr);
		TEST_ASSERT(op.value.record != nullptr);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::FunctionTemplateExplicitArgsInstantiateAndCache()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> int32 Id(T x) { return 1; }\n"
			"int32 r = Id<int32>(5);\n");

		// Pull the explicit `Id<int32>` template-argument list from the call site.
		const Scripting::TemplateArgumentListNode* tmplArgs = nullptr;
		FindNode(compiler, 1, [&](const Scripting::AstNode& n) {
			const auto* call = dynamic_cast<const Scripting::FunctionCallNode*>(&n);
			if (call && call->GetTemplateArgs()) tmplArgs = call->GetTemplateArgs().Get();
			return call != nullptr;
		});
		TEST_ASSERT(tmplArgs != nullptr);

		auto intType = model.QueryType("int32");
		TEST_ASSERT(intType.Ok());
		Core::Vector<const Scripting::Type*> args;
		args.Push(intType.value);

		// Explicit template args instantiate the function template transparently: one resolved signature.
		auto f = model.QueryFunction("Id", nullptr, tmplArgs, args);
		TEST_ASSERT(f.Ok());
		TEST_ASSERT(f.value != nullptr);
		TEST_ASSERT_EQ((int)f.value->GetParameterCount(), 1);

		// Every registered signature has exactly one lowering record (body + mangled symbol).
		const Scripting::FunctionRecord* rec = model.RecordFor(f.value);
		TEST_ASSERT(rec != nullptr);
		TEST_PASSED();
	}

	// --- Namespaces ------------------------------------------------------------

	void ProgramModelEdgeCasesTest::SameNameInTwoNamespacesIsAmbiguousUnqualified()
	{
		// A name shadowed by nesting: the SAME identifier at two depths of one namespace tree. The
		// qualified paths must each resolve to its own constant (no cross-depth bleed).
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace Outer { const int32 K = 5; }\n"
			"namespace Outer { namespace Inner { const int32 K = 9; } }\n");

		auto outerK = model.QueryConstant("Outer::K");
		TEST_ASSERT(outerK.Ok());
		TEST_ASSERT_EQ((int)outerK.value.AsInt(), 5);

		auto innerK = model.QueryConstant("Outer::Inner::K");
		TEST_ASSERT(innerK.Ok());
		TEST_ASSERT_EQ((int)innerK.value.AsInt(), 9);   // the deeper declaration, not shadowed by the outer
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::OpenNamespaceBlocksMergeWithoutConflict()
	{
		// Two `namespace N { ... }` blocks in one file merge into one scope: a constant from the first
		// block, a type and a function from the second all coexist and resolve by qualified name.
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace N { const int32 A = 1; }\n"
			"namespace N { struct S { int32 v; } void F() {} }\n");

		auto a = model.QueryConstant("N::A");
		TEST_ASSERT(a.Ok());
		TEST_ASSERT_EQ((int)a.value.AsInt(), 1);

		auto s = model.QueryType("N::S");
		TEST_ASSERT(s.Ok());
		TEST_ASSERT(s.value->GetKind() == Scripting::TypeKind::Object);

		auto f = model.QueryFunction("N::F");
		TEST_ASSERT(f.Ok());
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::CrossUnitOutOfLineQualifiedDefinition()
	{
		// A method DECLARED inside a namespace-qualified struct and DEFINED out-of-line as
		// `int32 N::C::Get() {...}`. The model defers the qualified definition to pass 2, resolves
		// the qualifier, matches the declared signature (interned pointer), and attaches the body.
		// (The cross-FILE form of this - declaration in one unit, definition in a unit that includes
		// it - is covered by the on-disk IsolatedTest NamespaceClassOutOfLine case; in-memory units
		// cannot include each other, and a unit that has NOT seen the type must not be able to define
		// its methods, which is exactly what visibility enforces.)
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace N { struct C { int32 Get(); } }\n"
			"int32 N::C::Get() { return 42; }\n");

		auto c = model.QueryType("N::C");
		TEST_ASSERT(c.Ok());
		const Scripting::Type* ct = c.value;

		// ONE function query, receiver = the type: finds the method.
		auto f = model.QueryFunction("Get", ct);
		TEST_ASSERT(f.Ok());
		TEST_ASSERT(f.value != nullptr);

		// The out-of-line body is attached to this signature's lowering record (not a shell).
		const Scripting::FunctionRecord* rec = model.RecordFor(f.value);
		TEST_ASSERT(rec != nullptr);
		TEST_ASSERT(rec->bodyNode != nullptr);
		TEST_PASSED();
	}

	// --- Aliases ---------------------------------------------------------------

	void ProgramModelEdgeCasesTest::AliasChainResolvesToSameObject()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Point { int32 x; }\n"
			"alias A1 = Point;\n"
			"alias A2 = A1;\n");

		auto p = model.QueryType("Point");
		auto a1 = model.QueryType("A1");
		auto a2 = model.QueryType("A2");
		TEST_ASSERT(p.Ok());
		TEST_ASSERT(a1.Ok());
		TEST_ASSERT(a2.Ok());
		// A two-link alias chain unwraps to the SAME Point instance (aliases are pure synonyms).
		TEST_ASSERT(p.value == a1.value);
		TEST_ASSERT(a1.value == a2.value);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::PointerToAliasUnwrapsForMemberAccess()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Point { int32 x; }\n"
			"alias Pt = Point;\n"
			"Pt* p;\n");

		// The `Pt*` type node from the pointer variable (find it by the variable's name).
		const auto* var = FindNode(compiler, 1, [](const Scripting::AstNode& n) {
			const auto* v = dynamic_cast<const Scripting::VariableDefinitionNode*>(&n);
			return v && v->GetType() && v->GetNameToken().GetText() == "p";
		});
		TEST_ASSERT(var != nullptr);
		auto ptrType = model.ResolveType(*dynamic_cast<const Scripting::VariableDefinitionNode*>(var)->GetType().Get());
		TEST_ASSERT(ptrType.Ok());

		// The pointee of Pt* is the alias Pt; a pointer is a first-class value (NOT unwrapped by
		// UnwrapToValue), so member access goes through GetPointedType to the pointee, then resolves the
		// alias to the concrete Point. This is how lowering does p->x: deref the pointer, then field.
		const Scripting::Type* pointee = Scripting::TypeHandle::GetPointedType(ptrType.value);
		TEST_ASSERT(pointee != nullptr);
		const Scripting::Type* base = Scripting::TypeHandle::UnwrapToValue(pointee); // alias -> Point
		TEST_ASSERT(base != nullptr);
		TEST_ASSERT(base->GetKind() == Scripting::TypeKind::Object);

		auto x = model.QueryMember(base, "x");
		TEST_ASSERT(x.Ok());
		TEST_ASSERT(x.value.IsField);
		TEST_PASSED();
	}

	// --- Overloading -----------------------------------------------------------

	void ProgramModelEdgeCasesTest::DefaultArgumentsFillAtResolution()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"int32 F(int32 a, int32 b = 7) { return a + b; }\n");

		auto intType = model.QueryType("int32");
		TEST_ASSERT(intType.Ok());
		Core::Vector<const Scripting::Type*> one;
		one.Push(intType.value);

		// One explicit arg: the default fills `b` at resolution. The caller gets ONE signature.
		auto f = model.QueryFunction("F", nullptr, nullptr, one);
		TEST_ASSERT(f.Ok());
		TEST_ASSERT_EQ((int)f.value->GetParameterCount(), 2);   // both params present in the signature

		// minArity = params minus trailing defaults = 1 (only `a` is required).
		const Scripting::FunctionRecord* rec = model.RecordFor(f.value);
		TEST_ASSERT(rec != nullptr);
		TEST_ASSERT_EQ((int)rec->minArity, 1);

		// No argTypes + exactly one overload: the documented "representative signature" path (LSP-style
		// name lookup) returns it Ok - but minArity stays 1, which is what a CALLER uses to know that
		// `a` is required. Overload resolution with real args is the separate, strict path.
		auto none = model.QueryFunction("F", nullptr, nullptr, {});
		TEST_ASSERT(none.Ok());
		const Scripting::FunctionRecord* recNone = model.RecordFor(none.value);
		TEST_ASSERT(recNone != nullptr);
		TEST_ASSERT_EQ((int)recNone->minArity, 1);
		TEST_PASSED();
	}

	// --- Inheritance / polymorphism --------------------------------------------

	void ProgramModelEdgeCasesTest::VirtualMethodIsInheritedAndOverridden()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Shape { virtual float GetArea(); }\n"
			"struct Circle : public Shape { float GetArea() override; }\n");

		auto shape = model.QueryType("Shape");
		auto circle = model.QueryType("Circle");
		TEST_ASSERT(shape.Ok());
		TEST_ASSERT(circle.Ok());

		const auto* st = static_cast<const Scripting::ObjectType*>(shape.value);
		const auto* ct = static_cast<const Scripting::ObjectType*>(circle.value);

		// The base declares a virtual; the derived hierarchy carries it.
		auto baseM = model.QueryMember(shape.value, "GetArea");
		TEST_ASSERT(baseM.Ok());
		TEST_ASSERT(!baseM.value.IsField);
		TEST_ASSERT(baseM.value.method->m_IsVirtual);

		// The derived overrides it (most-derived-first member lookup returns Circle's own method).
		auto derM = model.QueryMember(circle.value, "GetArea");
		TEST_ASSERT(derM.Ok());
		TEST_ASSERT(!derM.value.IsField);
		TEST_ASSERT(derM.value.method->m_IsOverride);
		TEST_ASSERT(ct->IsPolymorphic());
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::PolymorphicHierarchyHasSingleVptr()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Base { virtual int32 F(); int32 a; }\n"
			"struct Derived : public Base { int32 b; }\n");

		auto base = model.QueryType("Base");
		auto derived = model.QueryType("Derived");
		TEST_ASSERT(base.Ok());
		TEST_ASSERT(derived.Ok());
		const auto* bt = static_cast<const Scripting::ObjectType*>(base.value);
		const auto* dt = static_cast<const Scripting::ObjectType*>(derived.value);

		// Base: one vptr (8) + int32 a (4), padded to align 8 => size 16.
		TEST_ASSERT_EQ((int)model.SizeOf(base.value).value, 16);
		TEST_ASSERT_EQ((int)model.AlignOf(base.value).value, 8);

		// Derived inherits the SINGLE shared vptr (no second one): base sub-object (16) + int32 b (4),
		// padded to align 8 => size 24. A double vptr would make this 32 - the bug this locks out.
		TEST_ASSERT_EQ((int)model.SizeOf(derived.value).value, 24);

		// Base sub-object sits at offset 0 in Derived; fields line up after it.
		TEST_ASSERT_EQ((int)dt->GetBaseOffset(0), 0);
		TEST_ASSERT(dt->GetPrimaryBase() == bt);
		TEST_ASSERT(!dt->HasOwnVptr());
		TEST_ASSERT_EQ((int)dt->GetFieldOffset("a"), 8);    // vptr(8) + a
		TEST_ASSERT_EQ((int)dt->GetFieldOffset("b"), 16);   // base sub-object(16) + b
		TEST_PASSED();
	}

	// --- Test registry ---------------------------------------------------------

	void ProgramModelEdgeCasesTest::TestRegistryCollectsGroupsAndSymbols()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"[test(\"math.mul\")] void TestMul() {}\n"
			"[test(\"math.div\")] void TestDiv() {}\n"
			"[test] void TestBare() {}\n");

		const auto& tests = model.Tests().Tests();
		TEST_ASSERT_EQ((int)tests.size(), 3);
		bool sawMul = false, sawDiv = false, sawBare = false;
		for (const auto& t : tests)
		{
			TEST_ASSERT(t.owner == nullptr && t.function != nullptr);
			if (t.name == "math.mul") { sawMul = true; TEST_ASSERT(t.function->name == "TestMul"); }
			if (t.name == "math.div") { sawDiv = true; TEST_ASSERT(t.function->name == "TestDiv"); }
			if (t.name == "TestBare") sawBare = true; // a bare [test] is named by its function
		}
		TEST_ASSERT(sawMul && sawDiv && sawBare);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::ClassFixturesFromStaticSetupTeardown()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Counter {\n"
			"    [test_setup] public static void Setup() {}\n"
			"    [test_teardown] public static void Teardown() {}\n"
			"    [test(\"counter.inc\")] public static void TestInc() {}\n"
			"}\n");

		// The [test] method is a class test: its owner is the class and it binds the method's own record.
		const auto& tests = model.Tests().Tests();
		TEST_ASSERT_EQ((int)tests.size(), 1);
		TEST_ASSERT(tests[0].name == "counter.inc");
		TEST_ASSERT(tests[0].owner != nullptr && tests[0].function->name == "TestInc");

		// The static [test_setup]/[test_teardown] bracket that class's tests.
		const auto& fixtures = model.Tests().ClassFixtures(tests[0].owner);
		TEST_ASSERT(fixtures.setup && fixtures.setup->name == "Setup");
		TEST_ASSERT(fixtures.teardown && fixtures.teardown->name == "Teardown");
		TEST_ASSERT(!model.Tests().GlobalFixtures().setup && !model.Tests().GlobalFixtures().teardown);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::ClassTestGroupPrefixesItsMethods()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"[test(\"math.vector\")] struct VectorTests {\n"
			"    [test(\"addition\")] public static void TestAddition() {}\n"
			"    [test] public static void TestScale() {}\n"
			"}\n");

		const auto& tests = model.Tests().Tests();
		TEST_ASSERT_EQ((int)tests.size(), 2);
		bool sawAddition = false, sawScale = false;
		for (const auto& t : tests)
		{
			if (t.name == "math.vector.addition") sawAddition = true;
			if (t.name == "math.vector.TestScale") sawScale = true; // a bare method test is named by the function
		}
		TEST_ASSERT(sawAddition && sawScale);
		TEST_PASSED();
	}

	// --- Extension methods -----------------------------------------------------

	void ProgramModelEdgeCasesTest::ExtensionMethodOnNamespacedClassIsFound()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace NS { struct Box { int32 v; } }\n"
			"void SetBox(int32 x) extends NS::Box { this->v = x; }\n");

		auto box = model.QueryType("NS::Box");
		TEST_ASSERT(box.Ok());

		// The extension method is findable on the (namespaced) target type, most-derived-first lookup
		// falls through to the extension list after own + inherited methods.
		auto m = model.QueryMember(box.value, "SetBox");
		TEST_ASSERT(m.Ok());
		TEST_ASSERT(!m.value.IsField);
		TEST_ASSERT(m.value.method != nullptr);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::ExtensionMethodOnInstantiatedTypeIsFound()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> struct Stack { public T top; }\n"
			"void Push(int32 v) extends Stack<int32> {}\n"
			"Stack<int32> s;\n");

		// The concrete Stack<int32> type (from the global variable's spelling).
		const auto* var = FindNode(compiler, 1, [](const Scripting::AstNode& n) {
			const auto* v = dynamic_cast<const Scripting::VariableDefinitionNode*>(&n);
			return v && v->GetType() && v->GetNameToken().GetText() == "s";
		});
		TEST_ASSERT(var != nullptr);
		auto r = model.QueryInstantiation(*dynamic_cast<const Scripting::VariableDefinitionNode*>(var)->GetType().Get());
		TEST_ASSERT(r.Ok());

		// An extension method whose target is a TEMPLATE INSTANTIATION binds to the concrete type.
		auto m = model.QueryMember(r.value, "Push");
		TEST_ASSERT(m.Ok());
		TEST_ASSERT(!m.value.IsField);
		TEST_ASSERT(m.value.method != nullptr);
		TEST_PASSED();
	}

	// --- Combined stress: every concept at once --------------------------------

	void ProgramModelEdgeCasesTest::CombinedNamespaceTemplateInheritOperatorExtension()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace Geometry {\n"
			"    template<T> struct Shape {\n"
			"        public T s;\n"
			"        virtual T Measure();\n"
			"        [operator(\"add\")] T Combine(const Shape<T>& o) { return this->s; }\n"
			"    }\n"
			"    struct Circle : public Shape<float> { float r; virtual float Measure() override; }\n"
			"}\n"
			"void Describe(float x) extends Geometry::Circle {}\n");

		auto circle = model.QueryType("Geometry::Circle");
		TEST_ASSERT(circle.Ok());
		const auto* ct = static_cast<const Scripting::ObjectType*>(circle.value);

		// Inheritance from a template instantiation: Circle carries the base's virtual.
		TEST_ASSERT(ct->IsPolymorphic());

		// The [operator("add")] is declared on the TEMPLATE BASE (Shape<float>) and inherited by Circle:
		// `a + b` on two Circles must resolve to the inherited Combine through the hierarchy walk.
		auto op = model.QueryOperator("+", circle.value, circle.value);
		TEST_ASSERT(op.Ok());
		TEST_ASSERT(op.value.method != nullptr);

		// An extension method on the (namespaced, template-derived) concrete type is discoverable.
		auto m = model.QueryMember(circle.value, "Describe");
		TEST_ASSERT(m.Ok());
		TEST_ASSERT(!m.value.IsField);
		TEST_ASSERT(m.value.method != nullptr);

		// Layout is concrete: vptr + float s (inherited) + float r.
		TEST_ASSERT(model.SizeOf(circle.value).value >= 16);
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::InitListTypeExposesDataAndSizeMembers()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"__init_list<int32> g;\n");

		// The `__init_list<int32>` type node from the global variable.
		const auto* var = FindNode(compiler, 1, [](const Scripting::AstNode& n) {
			const auto* v = dynamic_cast<const Scripting::VariableDefinitionNode*>(&n);
			return v && v->GetType() && v->GetNameToken().GetText() == "g";
		});
		TEST_ASSERT(var != nullptr);

		auto t = model.ResolveType(*dynamic_cast<const Scripting::VariableDefinitionNode*>(var)->GetType().Get());
		TEST_ASSERT(t.Ok());
		TEST_ASSERT(t.value->GetKind() == Scripting::TypeKind::InitList);
		// The ABI is the 16-byte { T* data, uint64 size } aggregate.
		TEST_ASSERT_EQ((int)model.SizeOf(t.value).value, (int)(sizeof(void*) * 2));

		// Both built-in members resolve through the ONE member verb, as real fields.
		auto data = model.QueryMember(t.value, "data");
		TEST_ASSERT(data.Ok());
		TEST_ASSERT(data.value.IsField);
		TEST_ASSERT_EQ((int)data.value.field->m_Type->GetKind(), (int)Scripting::TypeKind::Pointer);
		TEST_ASSERT_EQ((int)data.value.field->m_Offset, 0);

		auto size = model.QueryMember(t.value, "size");
		TEST_ASSERT(size.Ok());
		TEST_ASSERT(size.value.IsField);
		TEST_ASSERT_EQ((int)size.value.field->m_Type->GetKind(), (int)Scripting::TypeKind::UInt64);
		TEST_ASSERT_EQ((int)size.value.field->m_Offset, (int)sizeof(void*));

		// An unknown name is still a clean miss.
		auto missing = model.QueryMember(t.value, "nope");
		TEST_ASSERT(!missing.Ok());
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::AnonymousUnionLanesFlattenIntoParentWithSharedLayout()
	{
		// An anonymous union (swizzle) inside a struct has no name; its lanes are flattened into the
		// PARENT so `v.x` / `v.r` resolve to the parent, and all views share ONE byte range. This used to
		// be dropped entirely: the parent had only `tag`, size was 4 (not 16), and x/y/z/r/g/b were
		// unresolvable - a core C4 SIMD feature.
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Vec3 {\n"
			"\tunion {\n"
			"\t\tstruct { float x; float y; float z; }\n"
			"\t\tstruct { float r; float g; float b; }\n"
			"\t}\n"
			"\tint32 tag;\n"
			"}\n");

		auto t = model.QueryType("Vec3");
		TEST_ASSERT(t.Ok());
		const Scripting::Type* base = Scripting::TypeHandle::UnwrapToValue(t.value);
		TEST_ASSERT(dynamic_cast<const Scripting::ObjectType*>(base) != nullptr);

		// 3 floats (12 bytes, shared by both views) + int32 tag = 16. NOT 4 (the old broken size).
		TEST_ASSERT_EQ((int)model.SizeOf(base).value, 16);

		// Every lane from BOTH views resolves as a real field of the parent.
		for (const char* nm : { "x", "y", "z", "r", "g", "b" })
		{
			auto q = model.QueryMember(base, nm);
			TEST_ASSERT(q.Ok());
			TEST_ASSERT(q.value.IsField);
		}
		// The non-union member still resolves, and sits after the union region (offset 12).
		auto tag = model.QueryMember(base, "tag");
		TEST_ASSERT(tag.Ok() && tag.value.IsField);
		TEST_ASSERT_EQ((int)tag.value.field->m_Offset, 12);

		// The two views ALIAS the same bytes: x and r are both at offset 0; z and b both at offset 8.
		auto x = model.QueryMember(base, "x");
		auto r = model.QueryMember(base, "r");
		TEST_ASSERT_EQ((int)x.value.field->m_Offset, (int)r.value.field->m_Offset); // both 0
		auto z = model.QueryMember(base, "z");
		auto b = model.QueryMember(base, "b");
		TEST_ASSERT_EQ((int)z.value.field->m_Offset, (int)b.value.field->m_Offset); // both 8
		TEST_PASSED();
	}

	void ProgramModelEdgeCasesTest::OperatorAnnotationsNormalizeToSpecCanonicalNames()
	{
		// The operator catalog must match the language spec's registry: a method declared with the SPEC
		// name ([operator("subtract")]) is found by QueryOperator for the token as written ("-"), and
		// every spelling of one operator unifies to its canonical name ([operator("[]")] == "index").
		// Before this the catalog used different names (sub/bitand/shl) and had no [] entry, so these
		// queries returned NotFound even though the method was declared.
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct V { int32 v; }\n"
			"[operator(\"subtract\")] V Sub(const V& o) extends V { return o; }\n"
			"[operator(\"bitwise_and\")] V Band(const V& o) extends V { return o; }\n"
			"[operator(\"shift_left\")] V Shl(const V& o) extends V { return o; }\n"
			"[operator(\"[]\")] V Bracket(const V& o) extends V { return o; }\n");

		auto vt = model.QueryType("V");
		TEST_ASSERT(vt.Ok());
		const Scripting::Type* v = vt.value;

		// Token as written -> the spec-named method (catalog maps token to canonical name).
		auto sub = model.QueryOperator("-", v, v);
		TEST_ASSERT(sub.Ok());
		TEST_ASSERT(sub.value.method->mName == "Sub");

		auto band = model.QueryOperator("&", v, v);
		TEST_ASSERT(band.Ok());
		TEST_ASSERT(band.value.method->mName == "Band");

		auto shl = model.QueryOperator("<<", v, v);
		TEST_ASSERT(shl.Ok());
		TEST_ASSERT(shl.value.method->mName == "Shl");

		// The bracket operator: the TOKEN spelling "[]" and the canonical spelling "index" both resolve.
		auto idx1 = model.QueryOperator("[]", v, v);
		TEST_ASSERT(idx1.Ok());
		TEST_ASSERT(idx1.value.method->mName == "Bracket");
		auto idx2 = model.QueryOperator("index", v, v);
		TEST_ASSERT(idx2.Ok());
		TEST_ASSERT(idx2.value.method->mName == "Bracket");
		TEST_PASSED();
	}
} // namespace Testing
