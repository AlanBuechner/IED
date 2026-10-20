#include "ProgramModelTest.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include <functional>

namespace Testing
{
	// Build the program model for one in-memory unit and hand back a reference to it (the model is
	// move-only - it owns its data, doc 03 - so phases hold a reference to the one shared model). The
	// caller keeps the compiler alive for as long as it uses the returned reference.
	static const Scripting::ProgramModel& MakeModel(Scripting::CompilerInstance& compiler, const char* source)
	{
		uint32 id = compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();   // parsing is per-unit threaded; the model build reads parsed ASTs
		compiler.AnalyzeProgram();
		auto unit = compiler.GetCompilationUnit(id);
		return unit->GetProgramModel();
	}

	void ProgramModelTest::TestQueryTypeSuccessAndAliasUnwrap()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Point { int32 x; int32 y; }\nalias Pt = Point;\n");
		TEST_ASSERT(model.IsValid());

		auto t = model.QueryType("Point");
		TEST_ASSERT(t.Ok());
		TEST_ASSERT(t.value != nullptr);
		TEST_ASSERT(t.value->GetKind() == Scripting::TypeKind::Object);

		// An alias is transparent: QueryType("Pt") unwraps to the SAME Point type.
		auto a = model.QueryType("Pt");
		TEST_ASSERT(a.Ok());
		TEST_ASSERT(a.value == t.value); // identical after alias unwrap
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryTypeNotFound()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Point { int32 x; }\nint32 Fa() { return 1; }\n");

		auto missing = model.QueryType("DoesNotExist");
		TEST_ASSERT(missing.error == Scripting::QueryError::NotFound);

		// A function name is not a type: it exists, but QueryType reports NotFound (not NotCallable).
		auto fnAsType = model.QueryType("Fa");
		TEST_ASSERT(fnAsType.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQuerySymbolKind()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Point { int32 x; }\nint32 Fa() { return 1; }\n");

		auto ty = model.QuerySymbol("Point");
		TEST_ASSERT(ty.Ok());
		TEST_ASSERT(ty.value->kind == Scripting::SymbolKind::Type);

		auto fn = model.QuerySymbol("Fa");
		TEST_ASSERT(fn.Ok());
		TEST_ASSERT(fn.value->kind == Scripting::SymbolKind::Function);

		auto missing = model.QuerySymbol("Nope");
		TEST_ASSERT(missing.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryFunctionSuccess()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "int32 Fa(int32 a) { return a; }\n");
		auto f = model.QueryFunction("Fa");
		TEST_ASSERT(f.Ok());
		TEST_ASSERT(f.value != nullptr);
		TEST_ASSERT_EQ((int)f.value->GetParameterCount(), 1);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryFunctionNotCallableWhenNameIsAType()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Point { int32 x; }\n");
		auto f = model.QueryFunction("Point");
		TEST_ASSERT(f.error == Scripting::QueryError::NotCallable); // exists, but is a type
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryFunctionNotFound()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Point { int32 x; }\n");
		auto f = model.QueryFunction("Nope");
		TEST_ASSERT(f.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryConstantEnumMemberAndConstVar()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"enum Color { Red = 1, Green = 5 };\n"
			"const int32 K = 40;\n");

		auto green = model.QueryConstant("Green");
		TEST_ASSERT(green.Ok());
		TEST_ASSERT(green.value.kind == Scripting::ConstantValue::Kind::Int64);
		TEST_ASSERT_EQ((int)green.value.intValue, 5);

		auto k = model.QueryConstant("K");
		TEST_ASSERT(k.Ok());
		TEST_ASSERT(k.value.kind == Scripting::ConstantValue::Kind::Int64);
		TEST_ASSERT_EQ((int)k.value.intValue, 40);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryConstantNotAValueForNonConst()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"int32 notConst = 7;\n"
			"struct Point { int32 x; }\n");

		// A mutable variable is not a compile-time constant.
		auto v = model.QueryConstant("notConst");
		TEST_ASSERT(v.error == Scripting::QueryError::NotAValue);

		// A type name is certainly not a constant either.
		auto t = model.QueryConstant("Point");
		TEST_ASSERT(t.error == Scripting::QueryError::NotAValue);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryConstantNotFound()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Point { int32 x; }\n");
		auto v = model.QueryConstant("Nope");
		TEST_ASSERT(v.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestInvalidModelReportsNotFound()
	{
		// A model with no data reports errors and never crashes (doc 03: queries return a value or an
		// error, never a bare nullptr). The default-constructed model is the "not built" state.
		Scripting::ProgramModel empty;
		TEST_ASSERT(!empty.IsValid());
		auto r1 = empty.QueryType("Point");
		TEST_ASSERT(r1.error == Scripting::QueryError::NotFound);
		auto r2 = empty.QueryFunction("Fa");
		TEST_ASSERT(r2.error == Scripting::QueryError::NotCallable); // invalid model, function query
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryMemberFieldAndMethod()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct P { int32 x; int32 GetX() { return x; } }\n");
		const Scripting::Type* p = nullptr;
		{
			auto t = model.QueryType("P");
			TEST_ASSERT(t.Ok());
			p = t.value;
		}

		auto f = model.QueryMember(p, "x");
		TEST_ASSERT(f.Ok());
		TEST_ASSERT(f.value.IsField);
		TEST_ASSERT(f.value.field != nullptr);
		TEST_ASSERT_EQ((int)f.value.field->m_Offset, 0);

		auto m = model.QueryMember(p, "GetX");
		TEST_ASSERT(m.Ok());
		TEST_ASSERT(!m.value.IsField);
		TEST_ASSERT(m.value.method != nullptr);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryMemberInheritedMostDerivedFirst()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Base { int32 v; int32 GetV() { return v; } }\n"
			"struct Derived : Base { int32 v; int32 GetV() { return v + 1; } }\n");
		const Scripting::Type* base = nullptr;
		const Scripting::Type* derived = nullptr;
		{
			auto b = model.QueryType("Base");
			auto d = model.QueryType("Derived");
			TEST_ASSERT(b.Ok() && d.Ok());
			base = b.value;
			derived = d.value;
		}

		// The same name on both base and derived: queried through the DERIVED type it must be the
		// derived member (most-derived first), not the inherited one.
		auto df = model.QueryMember(derived, "v");
		TEST_ASSERT(df.Ok() && df.value.IsField);
		TEST_ASSERT_EQ((int)df.value.field->m_Offset, 4); // derived's own v sits after the base subobject

		auto bf = model.QueryMember(base, "v");
		TEST_ASSERT(bf.Ok() && bf.value.IsField);
		TEST_ASSERT_EQ((int)bf.value.field->m_Offset, 0);

		auto dm = model.QueryMember(derived, "GetV");
		TEST_ASSERT(dm.Ok() && !dm.value.IsField);
		TEST_ASSERT(dm.value.method != nullptr);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryMemberNotAnObjectType()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "int32 Fa(int32 a) { return a; }\n");

		// A function's type is a FunctionType, not an object type: members cannot be asked of it.
		auto f = model.QueryFunction("Fa");
		TEST_ASSERT(f.Ok() && f.value != nullptr);
		auto fnAsBase = model.QueryMember(static_cast<const Scripting::Type*>(f.value), "x");
		TEST_ASSERT(fnAsBase.error == Scripting::QueryError::NotCallable);

		// A null base is also rejected, not a crash.
		auto nullBase = model.QueryMember(nullptr, "x");
		TEST_ASSERT(nullBase.error == Scripting::QueryError::NotCallable);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryMemberNotFound()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct P { int32 x; }\n");
		const Scripting::Type* p = nullptr;
		{
			auto t = model.QueryType("P");
			TEST_ASSERT(t.Ok());
			p = t.value;
		}

		auto missing = model.QueryMember(p, "DoesNotExist");
		TEST_ASSERT(missing.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryOperatorBinaryLeftReceiver()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Plain { int32 v; }\n"
			"struct HasAdd { int32 v; }\n"
			"[operator(\"add\")] HasAdd Add(const HasAdd& o) extends HasAdd { return o; }\n");
		const Scripting::Type* hasAdd = nullptr;
		const Scripting::Type* plain = nullptr;
		{
			auto a = model.QueryType("HasAdd");
			auto p = model.QueryType("Plain");
			TEST_ASSERT(a.Ok() && p.Ok());
			hasAdd = a.value;
			plain = p.value;
		}

		// "add" is declared on HasAdd only: with HasAdd as the LHS (and an argument it takes) it resolves.
		auto r = model.QueryOperator("add", hasAdd, hasAdd);
		TEST_ASSERT(r.Ok());
		TEST_ASSERT(model.QueryOperator("add", hasAdd, plain).error == Scripting::QueryError::NotFound); // Add cannot take a Plain
		TEST_ASSERT(r.value.method != nullptr);
		TEST_ASSERT(r.value.record != nullptr);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryOperatorNeverSwapsOperands()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Plain { int32 v; }\n"
			"struct HasAdd { int32 v; }\n"
			"[operator(\"add\")] HasAdd Add(const HasAdd& o) extends HasAdd { return o; }\n");
		const Scripting::Type* hasAdd = nullptr;
		const Scripting::Type* plain = nullptr;
		{
			auto a = model.QueryType("HasAdd");
			auto p = model.QueryType("Plain");
			TEST_ASSERT(a.Ok() && p.Ok());
			hasAdd = a.value;
			plain = p.value;
		}

		// Same operator, but now HasAdd is the RHS: `plain + hasAdd` looks on Plain only (D-25).
		auto r = model.QueryOperator("add", plain, hasAdd);
		TEST_ASSERT(r.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryOperatorUnary()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Neg { int32 v; }\n"
			"[operator(\"negate\")] Neg DoNeg() extends Neg { return Neg(); }\n");
		const Scripting::Type* neg = nullptr;
		{
			auto t = model.QueryType("Neg");
			TEST_ASSERT(t.Ok());
			neg = t.value;
		}

		// A null rhs makes this a unary/postfix operator on the single operand.
		auto r = model.QueryOperator("negate", neg, nullptr);
		TEST_ASSERT(r.Ok());
		TEST_ASSERT(r.value.method != nullptr);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryOperatorNotFoundForPrimitives()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler, "struct Plain { int32 v; }\n");
		const Scripting::Type* plain = nullptr;
		{
			auto t = model.QueryType("Plain");
			TEST_ASSERT(t.Ok());
			plain = t.value;
		}

		// No operand declares "add": a clean NotFound, not a crash or a wrong pick.
		auto r = model.QueryOperator("add", plain, plain);
		TEST_ASSERT(r.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryOperatorCompoundInPlaceAndFallback()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct A { int32 v; }\n"
			"[operator(\"add_assign\")] void AddAssign(const A& o) extends A { }\n"
			"struct B { int32 v; }\n"
			"[operator(\"multiply\")] B Mul(const B& o) extends B { return o; }\n");

		const Scripting::Type* a = nullptr;
		const Scripting::Type* b = nullptr;
		const Scripting::Type* plain = nullptr;
		{
			auto ta = model.QueryType("A");
			auto tb = model.QueryType("B");
			TEST_ASSERT(ta.Ok() && tb.Ok());
			a = ta.value;
			b = tb.value;
			plain = a; // any non-matching object for the not-found case
		}

		// In-place form present: "add_assign" binds to the void in-place method on the LHS.
		auto ip = model.QueryOperator("add_assign", a, a);
		TEST_ASSERT(ip.Ok());
		TEST_ASSERT(ip.value.isAssignForm);   // it IS the in-place <op>_assign form
		TEST_ASSERT(ip.value.method->mName == "AddAssign");

		// No in-place "multiply_assign", but a plain "multiply" exists: fall back to the binary op.
		auto fb = model.QueryOperator("multiply_assign", b, b);
		TEST_ASSERT(fb.Ok());
		TEST_ASSERT(!fb.value.isAssignForm);  // it is the PLAIN fallback, not an in-place form
		TEST_ASSERT(fb.value.method->mName == "Mul");

		// Neither in-place nor fallback declared: a clean NotFound.
		auto none = model.QueryOperator("divide_assign", plain, plain);
		TEST_ASSERT(none.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryVariableConstAndStatic()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"const int32 K = 5;\n"
			"static float S = 1.0f;\n");

		auto k = model.QueryVariable("K");
		TEST_ASSERT(k.Ok());
		TEST_ASSERT(k.value.type != nullptr);
		TEST_ASSERT(k.value.isConst);
		TEST_ASSERT(!k.value.isStatic);

		auto s = model.QueryVariable("S");
		TEST_ASSERT(s.Ok());
		TEST_ASSERT(s.value.type != nullptr);
		TEST_ASSERT(s.value.isStatic);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryVariableNotAVariable()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"int32 Fa(int32 a) { return a; }\n"
			"struct P { int32 x; }\n");

		// A function name is not a variable: NotCallable (exists, wrong kind).
		auto fn = model.QueryVariable("Fa");
		TEST_ASSERT(fn.error == Scripting::QueryError::NotCallable);

		// A type name is not a variable either.
		auto ty = model.QueryVariable("P");
		TEST_ASSERT(ty.error == Scripting::QueryError::NotCallable);

		// A missing name is NotFound.
		auto none = model.QueryVariable("Nope");
		TEST_ASSERT(none.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestLayoutSizeOfAndAlignOf()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct P { int32 x; float y; }\n"   // size 8, align 4
			"struct Fwd;\n");                     // forward-declared: incomplete

		const Scripting::Type* p = nullptr;
		const Scripting::Type* fwd = nullptr;
		{
			auto tp = model.QueryType("P");
			auto tf = model.QueryType("Fwd");
			TEST_ASSERT(tp.Ok() && tf.Ok());
			p = tp.value;
			fwd = tf.value;
		}

		auto sz = model.SizeOf(p);
		TEST_ASSERT(sz.Ok());
		TEST_ASSERT_EQ((int)sz.value, 8);

		auto al = model.AlignOf(p);
		TEST_ASSERT(al.Ok());
		TEST_ASSERT_EQ((int)al.value, 4);

		// A forward-declared type is a complete shell with no layout yet: size 0 (order-independence).
		// It is NOT an error - the two-pass model resolves its real size later; here it simply has none.
		auto fwdSz = model.SizeOf(fwd);
		TEST_ASSERT(fwdSz.Ok());
		TEST_ASSERT_EQ((int)fwdSz.value, 0);
		TEST_PASSED();
	}

	void ProgramModelTest::TestElementOfArrayPointerAndScalar()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"int32[4] g_arr;\n"
			"int32* g_ptr;\n"
			"float g_s;\n");

		const Scripting::Type* arr = nullptr;
		const Scripting::Type* ptr = nullptr;
		const Scripting::Type* scalar = nullptr;
		{
			auto va = model.QueryVariable("g_arr");
			auto vp = model.QueryVariable("g_ptr");
			auto vs = model.QueryVariable("g_s");
			TEST_ASSERT(va.Ok() && vp.Ok() && vs.Ok());
			arr = va.value.type;
			ptr = vp.value.type;
			scalar = vs.value.type;
		}

		// An array's element is its element type (int32, size 4).
		const Scripting::Type* aelem = model.ElementOf(arr);
		TEST_ASSERT(aelem != nullptr);
		TEST_ASSERT_EQ((int)aelem->GetSize(), 4);

		// A pointer's element is its pointee type.
		const Scripting::Type* pelem = model.ElementOf(ptr);
		TEST_ASSERT(pelem != nullptr);
		TEST_ASSERT_EQ((int)pelem->GetSize(), 4);

		// A scalar has no element concept: nullptr, not an error or a wrong type.
		TEST_ASSERT(model.ElementOf(scalar) == nullptr);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQualifiedTypeAndFunction()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace Math {\n"
			"    struct Vec { int32 x; int32 y; }\n"
			"    int32 AddX(const Vec& a, const Vec& b) { return a.x + b.x; }\n"
			"}\n");

		// A qualified type name resolves to the same concrete type as its unqualified form would.
		auto q = model.QueryType("Math::Vec");
		TEST_ASSERT(q.Ok());
		TEST_ASSERT(q.value != nullptr);
		TEST_ASSERT(q.value->GetKind() == Scripting::TypeKind::Object);

		// A qualified function name resolves to its signature (2 params).
		auto f = model.QueryFunction("Math::AddX");
		TEST_ASSERT(f.Ok());
		TEST_ASSERT(f.value != nullptr);
		TEST_ASSERT_EQ((int)f.value->GetParameterCount(), 2);

		// QuerySymbol sees the qualified name too, and reports the right kind.
		auto s = model.QuerySymbol("Math::Vec");
		TEST_ASSERT(s.Ok());
		TEST_ASSERT(s.value->kind == Scripting::SymbolKind::Type);

		// A bogus qualified path is NotFound, not a crash or a wrong hit.
		auto none = model.QueryType("Math::Nope");
		TEST_ASSERT(none.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQualifiedConstantFolds()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"namespace Math {\n"
			"    const int32 K = 7;\n"
			"    enum Color { Red = 3, Green = 5 };\n"
			"}\n");

		// A qualified const variable folds to its value.
		auto k = model.QueryConstant("Math::K");
		TEST_ASSERT(k.Ok());
		TEST_ASSERT_EQ((int)k.value.intValue, 7);

		// A qualified enum member folds to its value (member matched by its final name segment).
		auto g = model.QueryConstant("Math::Green");
		TEST_ASSERT(g.Ok());
		TEST_ASSERT_EQ((int)g.value.intValue, 5);

		// A bogus qualified constant is NotFound, not a silent fold.
		auto none = model.QueryConstant("Math::Nope");
		TEST_ASSERT(none.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void ProgramModelTest::TestQueryInstantiationReturnsConcreteType()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"template<T> class TBox { public T v; }\n"
			"TBox<int32> g;\n"
			"int32 h;\n");

		const Scripting::QualTypeNode* spec = nullptr;   // the `TBox<int32>` type node
		const Scripting::QualTypeNode* plain = nullptr;  // the plain `int32` type node
		std::function<void(const Scripting::AstNode&)> walk = [&](const Scripting::AstNode& n) {
			if (spec && plain) return;
			if (const auto* v = dynamic_cast<const Scripting::VariableDefinitionNode*>(&n)) {
				const auto& t = v->GetType();
				if (t) {
					if (t->HasTemplateArgs() && !spec) spec = t.Get();
					else if (!t->HasTemplateArgs() && !plain) plain = t.Get();
				}
			}
			for (const auto& child : n.Children())
				walk(*child.Get());
		};
		auto unit = compiler.GetCompilationUnit(1);
		if (unit) walk(*unit->GetRootAST().Get());
		TEST_ASSERT(spec != nullptr);
		TEST_ASSERT(plain != nullptr);

		// Asking for the specialization returns the concrete instantiated type - the caller cannot tell
		// it is an instantiation (04 rule 2), and the same spelling yields the SAME cached instance.
		auto r = model.QueryInstantiation(*spec);
		TEST_ASSERT(r.Ok());
		TEST_ASSERT(r.value != nullptr);
		TEST_ASSERT(r.value->GetKind() == Scripting::TypeKind::Object);

		auto again = model.QueryInstantiation(*spec);
		TEST_ASSERT(again.Ok());
		TEST_ASSERT(again.value == r.value); // cached, not a second instantiation

		// A plain (non-specialization) type node is rejected, not treated as an instantiation.
		auto notSpec = model.QueryInstantiation(*plain);
		TEST_ASSERT(notSpec.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}
} // namespace Testing
