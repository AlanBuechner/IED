#include "SimulatedLoweringTest.h"
#include "Model/SymbolTable.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/IfStatementNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"
#include <functional>

namespace Testing
{
	// Build the program model for one in-memory unit and hand back a NON-CONST reference: lowering
	// mutates the scope chain (PushScope / RegisterLocal), so the simulated lowerer holds this.
	static Scripting::ProgramModel& MakeLoweringModel(Scripting::CompilerInstance& compiler, uint32 id, const char* source)
	{
		uint32 sid = compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto unit = compiler.GetCompilationUnit(id ? id : sid);
		return unit->GetProgramModelForLowering();
	}

	// Find the file-scope definition of a function by name, walking the unit's root AST.
	static const Scripting::FunctionDefinitionNode* FindFunction(Scripting::CompilerInstance& compiler, uint32 id, const char* name)
	{
		auto unit = compiler.GetCompilationUnit(id);
		const Scripting::FunctionDefinitionNode* found = nullptr;
		std::function<void(const Scripting::AstNode&)> walk = [&](const Scripting::AstNode& n) {
			if (found) return;
			if (const auto* fn = dynamic_cast<const Scripting::FunctionDefinitionNode*>(&n))
				if (fn->GetName() && fn->GetName()->GetFirstIdentifierToken().GetText() == name)
					found = fn;
			for (const auto& c : n.Children()) walk(*c.Get());
		};
		if (unit) walk(*unit->GetRootAST().Get());
		return found;
	}

	// The first ObjectDefinitionNode in a unit's root AST (a local class declared at file scope).
	static const Scripting::ObjectDefinitionNode* FindFirstClass(Scripting::CompilerInstance& compiler, uint32 id)
	{
		auto unit = compiler.GetCompilationUnit(id);
		const Scripting::ObjectDefinitionNode* found = nullptr;
		std::function<void(const Scripting::AstNode&)> walk = [&](const Scripting::AstNode& n) {
			if (found) return;
			if (auto* o = dynamic_cast<const Scripting::ObjectDefinitionNode*>(&n)) found = o;
			for (const auto& c : n.Children()) walk(*c.Get());
		};
		if (unit) walk(*unit->GetRootAST().Get());
		return found;
	}

	// ---------------------------------------------------------------------------
	// Scope mechanics (doc 06 RAII)
	// ---------------------------------------------------------------------------

	void SimulatedLoweringTest::ScopePushPopAndLocalShadowsGlobal()
	{
		Scripting::CompilerInstance compiler;
		auto& model = MakeLoweringModel(compiler, 1, "int32 g_x = 5;\n");
		TEST_ASSERT(model.IsValid());

		// Outside any lowering scope the name is the GLOBAL.
		auto g = model.QueryVariable("g_x");
		TEST_ASSERT(g.Ok());
		TEST_ASSERT_EQ((int)g.value.type->GetSize(), 4); // int32 global
		TEST_ASSERT(model.FindLocal("g_x") == nullptr);

		{
			// Pushing a function scope and registering a local shadows the global for every query.
			auto fn = model.PushScope(Scripting::Scope::Kind::Function, "fn");
			Scripting::SymbolInfo info;
			info.kind = Scripting::SymbolKind::Variable;
			auto t = model.QueryType("int32");
			TEST_ASSERT(t.Ok());
			info.type = t.value;
			TEST_ASSERT(model.RegisterLocal("g_x", info));

			auto local = model.QueryVariable("g_x");
			TEST_ASSERT(local.Ok());
			TEST_ASSERT(model.FindLocal("g_x") != nullptr); // the LOCAL answered, not the global

			// A different-kind local also shadows: a local TYPE named g_x hides the variable for
			// variable queries (NotCallable) - it does not silently fall through to the global.
			auto blk = model.PushScope(Scripting::Scope::Kind::Block, "blk");
			Scripting::SymbolInfo ty;
			ty.kind = Scripting::SymbolKind::Type;
			ty.type = t.value;
			TEST_ASSERT(model.RegisterLocal("g_x", ty));
			auto shadowed = model.QueryVariable("g_x");
			TEST_ASSERT(shadowed.error == Scripting::QueryError::NotCallable);
		}

		// The scopes popped: the name is the global again (RAII, no manual accounting).
		TEST_ASSERT(model.FindLocal("g_x") == nullptr);
		auto again = model.QueryVariable("g_x");
		TEST_ASSERT(again.Ok());
		TEST_PASSED();
	}

	void SimulatedLoweringTest::NestedBlockDoesNotLeakIntoSibling()
	{
		Scripting::CompilerInstance compiler;
		auto& model = MakeLoweringModel(compiler, 1, "int32 g;\n");

		auto fn = model.PushScope(Scripting::Scope::Kind::Function, "fn");
		{
			auto first = model.PushScope(Scripting::Scope::Kind::Block, "first");
			Scripting::SymbolInfo info;
			info.kind = Scripting::SymbolKind::Variable;
			auto t = model.QueryType("int32");
			TEST_ASSERT(t.Ok());
			info.type = t.value;
			TEST_ASSERT(model.RegisterLocal("x", info));
			TEST_ASSERT(model.FindLocal("x") != nullptr);
		} // first pops - `x` must be gone

		// Sibling scope: the previous block's local did not leak in.
		auto second = model.PushScope(Scripting::Scope::Kind::Block, "second");
		TEST_ASSERT(model.FindLocal("x") == nullptr);
		auto v = model.QueryVariable("x");
		TEST_ASSERT(v.error == Scripting::QueryError::NotFound); // no global x either
		TEST_PASSED();
	}

	void SimulatedLoweringTest::InnermostScopeWinsThenOuterRestored()
	{
		Scripting::CompilerInstance compiler;
		auto& model = MakeLoweringModel(compiler, 1, "int32 g;\n");

		auto fn = model.PushScope(Scripting::Scope::Kind::Function, "fn");
		auto t = model.QueryType("int32");
		TEST_ASSERT(t.Ok());
		Scripting::SymbolInfo outer;
		outer.kind = Scripting::SymbolKind::Variable;
		outer.type = t.value;
		TEST_ASSERT(model.RegisterLocal("x", outer));

		{
			auto inner = model.PushScope(Scripting::Scope::Kind::Block, "inner");
			Scripting::SymbolInfo in;
			in.kind = outer.kind;
			in.type = outer.type;
			TEST_ASSERT(model.RegisterLocal("x", in));
			// Innermost wins: the query is answered by the inner entry.
			auto q = model.QueryVariable("x");
			TEST_ASSERT(q.Ok());
			TEST_ASSERT(model.FindLocal("x") != nullptr);
		}

		// After the inner pops, the OUTER local is visible again (not null, not an error).
		auto restored = model.FindLocal("x");
		TEST_ASSERT(restored != nullptr);
		auto q2 = model.QueryVariable("x");
		TEST_ASSERT(q2.Ok());
		TEST_PASSED();
	}

	void SimulatedLoweringTest::RedeclarationInSameBlockIsRejected()
	{
		Scripting::CompilerInstance compiler;
		auto& model = MakeLoweringModel(compiler, 1, "int32 g;\n");

		auto fn = model.PushScope(Scripting::Scope::Kind::Function, "fn");
		Scripting::SymbolInfo info;
		info.kind = Scripting::SymbolKind::Variable;
		auto t = model.QueryType("int32");
		TEST_ASSERT(t.Ok());
		info.type = t.value;
		TEST_ASSERT(model.RegisterLocal("x", info));

		// A second `int32 x;` in the SAME block is a redefinition: RegisterLocal reports it.
		TEST_ASSERT(!model.RegisterLocal("x", info));
		TEST_PASSED();
	}

	// ---------------------------------------------------------------------------
	// Function-local declarations (doc 06: dynamic registration)
	// ---------------------------------------------------------------------------

	void SimulatedLoweringTest::LocalStructVisibleOnlyInsideItsScope()
	{
		// `Inner` is declared INSIDE main's body: the model builder registers only file-scope
		// declarations, so it is NOT a global. Its name lives solely in the lowering chain.
		Scripting::CompilerInstance compiler;
		uint32 id = compiler.RegisterInMemory("t.cf",
			"void main() {\n"
			"    class Inner { int32 x; }\n"
			"}\n");
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto& model = compiler.GetCompilationUnit(id)->GetProgramModelForLowering();

		auto fn = FindFunction(compiler, id, "main");
		TEST_ASSERT(fn != nullptr);
		const Scripting::AstNode* decl = nullptr;
		for (const auto& s : fn->GetBody().Get()->GetStatements())
			if (dynamic_cast<const Scripting::ObjectDefinitionNode*>(s.Get())) decl = s.Get();
		TEST_ASSERT(decl != nullptr);

		// Not visible before lowering reaches the declaration.
		auto early = model.QueryType("Inner");
		TEST_ASSERT(early.error == Scripting::QueryError::NotFound);

		const Scripting::Type* localType = nullptr;
		{
			auto guard = model.PushScope(Scripting::Scope::Kind::Block, "body");
			auto t = model.RegisterLocalType(*decl);
			TEST_ASSERT(t.Ok());
			TEST_ASSERT(t.value->GetKind() == Scripting::TypeKind::Object);
			localType = t.value;

			// Visible inside the scope: the name resolves to the SAME instance.
			auto vt = model.QueryType("Inner");
			TEST_ASSERT(vt.Ok());
			TEST_ASSERT(vt.value == localType);

			// A variable of the local type registers and queries normally.
			Scripting::SymbolInfo vi;
			vi.kind = Scripting::SymbolKind::Variable;
			vi.type = localType;
			TEST_ASSERT(model.RegisterLocal("v", vi));
			auto vq = model.QueryVariable("v");
			TEST_ASSERT(vq.Ok());
			TEST_ASSERT(vq.value.type == localType);

			// The local type's members are queryable like any other type.
			auto m = model.QueryMember(localType, "x");
			TEST_ASSERT(m.Ok() && m.value.IsField);
		} // guard pops: the NAME is gone...

		// ...but the concrete Type* stays valid in the store (doc 03 ownership invariant): a lowered
		// HIR node that captured it can still read its layout.
		TEST_ASSERT(model.FindLocal("Inner") == nullptr);
		auto after = model.QueryType("Inner");
		TEST_ASSERT(after.error == Scripting::QueryError::NotFound); // name is scoped, not global

		const Scripting::ObjectType* obj = dynamic_cast<const Scripting::ObjectType*>(localType);
		TEST_ASSERT(obj != nullptr);
		TEST_ASSERT_EQ((int)model.SizeOf(localType).value, 4); // int32 x survives the pop
		TEST_PASSED();
	}

	void SimulatedLoweringTest::LocalFunctionQueryableOnlyInsideItsScope()
	{
		Scripting::CompilerInstance compiler;
		uint32 id = compiler.RegisterInMemory("t.cf",
			"void main() {\n"
			"    int32 LocalFn(int32 a) { return a * 2; }\n"
			"}\n");
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto& model = compiler.GetCompilationUnit(id)->GetProgramModelForLowering();

		auto fn = FindFunction(compiler, id, "main");
		TEST_ASSERT(fn != nullptr);
		const auto& body = fn->GetBody().Get();

		{
			auto guard = model.PushScope(Scripting::Scope::Kind::Block, "body");
			const Scripting::AstNode* localFn = nullptr;
			for (const auto& s : body->GetStatements())
				if (dynamic_cast<const Scripting::FunctionDefinitionNode*>(s.Get())) localFn = s.Get();
			TEST_ASSERT(localFn != nullptr);

			auto r = model.RegisterLocalFunction(dynamic_cast<const Scripting::FunctionDefinitionNode&>(*localFn));
			TEST_ASSERT(r.Ok());
			TEST_ASSERT(r.value->type != nullptr);
			TEST_ASSERT_EQ((int)r.value->type->GetParameterCount(), 1);

			// Queryable by name from inside the scope - one signature, resolved.
			auto q = model.QueryFunction("LocalFn");
			TEST_ASSERT(q.Ok());
			TEST_ASSERT(q.value == r.value->type);

			// The body node is recorded for emission (what a lower system needs).
			TEST_ASSERT(r.value->bodyNode != nullptr);
		} // pops

		// Outside the scope the name does not exist.
		auto after = model.QueryFunction("LocalFn");
		TEST_ASSERT(after.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	void SimulatedLoweringTest::LocalTypeUsedBeforeDeclaredIsNotFound()
	{
		// `Inner` exists ONLY inside main's body - the model builder registers file-scope
		// declarations, so it is NOT a global. That is what makes "used before declared" testable.
		Scripting::CompilerInstance compiler;
		uint32 id = compiler.RegisterInMemory("t.cf",
			"void main() {\n"
			"    class Inner { int32 x; }\n"
			"    Inner v;\n"
			"}\n");
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto& model = compiler.GetCompilationUnit(id)->GetProgramModelForLowering();

		auto fn = FindFunction(compiler, id, "main");
		TEST_ASSERT(fn != nullptr);
		const Scripting::AstNode* decl = nullptr;
		for (const auto& s : fn->GetBody().Get()->GetStatements())
			if (dynamic_cast<const Scripting::ObjectDefinitionNode*>(s.Get())) decl = s.Get();
		TEST_ASSERT(decl != nullptr);

		const Scripting::Type* localType = nullptr;
		{
			auto guard = model.PushScope(Scripting::Scope::Kind::Block, "body");
			// Lowering walks the body IN ORDER. `Inner v;` is reached BEFORE `class Inner` is
			// registered, so resolving the type must be a clean NotFound - no silent nullptr, crash.
			auto early = model.QueryType("Inner");
			TEST_ASSERT(early.error == Scripting::QueryError::NotFound);

			// Once lowering reaches the declaration and registers it, the SAME spelling resolves.
			auto t = model.RegisterLocalType(*decl);
			TEST_ASSERT(t.Ok());
			localType = t.value;
			auto late = model.QueryType("Inner");
			TEST_ASSERT(late.Ok());
			TEST_ASSERT(late.value == localType);

			// And a variable of it now registers and queries normally.
			Scripting::SymbolInfo vi;
			vi.kind = Scripting::SymbolKind::Variable;
			vi.type = localType;
			TEST_ASSERT(model.RegisterLocal("v", vi));
			auto vq = model.QueryVariable("v");
			TEST_ASSERT(vq.Ok() && vq.value.type == localType);
		} // guard pops here: the name is gone (it was never a global)

		auto after = model.QueryType("Inner");
		TEST_ASSERT(after.error == Scripting::QueryError::NotFound);
		// The concrete type survives the pop with its layout intact (doc 03 ownership invariant).
		TEST_ASSERT_EQ((int)model.SizeOf(localType).value, 4); // int32 x
		TEST_PASSED();
	}

	// ---------------------------------------------------------------------------
	// AST-driven: lower a real body statement by statement
	// ---------------------------------------------------------------------------

	void SimulatedLoweringTest::LowerRealBodyTracksLocalsAcrossBlocks()
	{
		Scripting::CompilerInstance compiler;
		uint32 id = compiler.RegisterInMemory("t.cf",
			"int32 g;\n"
			"void main() {\n"
			"    int32 a = 1;\n"
			"    if (a > 0) {\n"
			"        int32 b = 2;\n"
			"    }\n"
			"    for (int32 i = 0; i < a; i++) {\n"
			"        float f = 1.0f;\n"
			"    }\n"
			"}\n");
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto& model = compiler.GetCompilationUnit(id)->GetProgramModelForLowering();

		auto fn = FindFunction(compiler, id, "main");
		TEST_ASSERT(fn != nullptr);

		// Lower the real body: every declaration registers; structured constructs push/pop a layer.
		auto fnScope = model.PushScope(Scripting::Scope::Kind::Function, "main");
		std::function<bool(const Scripting::AstNode&)> lowerStmt = [&](const Scripting::AstNode& stmt) -> bool {
			switch (stmt.Type())
			{
			case Scripting::NodeType::VariableDefinition: {
				const auto& v = dynamic_cast<const Scripting::VariableDefinitionNode&>(stmt);
				auto t = model.ResolveType(*v.GetType());
				if (!t.Ok()) return false;
				Scripting::SymbolInfo info;
				info.kind = Scripting::SymbolKind::Variable;
				info.type = t.value;
				return model.RegisterLocal(v.GetNameToken().GetText(), info);
			}
			case Scripting::NodeType::IfStatement: {
				const auto& ifn = dynamic_cast<const Scripting::IfStatementNode&>(stmt);
				if (ifn.GetThenBody())
				{
					auto guard = model.PushScope(Scripting::Scope::Kind::Block, "if");
					for (const auto& s : ifn.GetThenBody()->GetStatements())
						if (!lowerStmt(*s.Get())) return false;
				}
				return true;
			}
			case Scripting::NodeType::ForLoop: {
				const auto& f = dynamic_cast<const Scripting::ForLoopNode&>(stmt);
				auto guard = model.PushScope(Scripting::Scope::Kind::Block, "for");
				if (f.GetBody())
					for (const auto& s : f.GetBody()->GetStatements())
						if (!lowerStmt(*s.Get())) return false;
				return true;
			}
			default:
				return true;
			}
		};

		bool ok = true;
		for (const auto& s : fn->GetBody().Get()->GetStatements())
			if (!lowerStmt(*s.Get())) { ok = false; break; }
		TEST_ASSERT(ok); // every declaration registered cleanly

		// After the if/for blocks popped: only the function-level local `a` remains.
		auto a = model.QueryVariable("a");
		TEST_ASSERT(a.Ok());
		auto b = model.QueryVariable("b"); // was in the if block - popped
		TEST_ASSERT(b.error == Scripting::QueryError::NotFound);
		auto f = model.QueryVariable("f"); // was in the for block - popped
		TEST_ASSERT(f.error == Scripting::QueryError::NotFound);
		TEST_PASSED();
	}

	// ---------------------------------------------------------------------------
	// Completeness: every query a lower system needs succeeds on one body
	// ---------------------------------------------------------------------------

	void SimulatedLoweringTest::LoweringGetsEveryFactItNeeds()
	{
		Scripting::CompilerInstance compiler;
		uint32 id = compiler.RegisterInMemory("t.cf",
			"const int32 K = 40;\n"
			"enum Color { Red = 1, Green = 5 };\n"
			"class Point { float x; float y; }\n"
			"int32 Add(int32 a, int32 b) { return a + b; }\n"
			"void main() {\n"
			"    int32 n = 1;\n"
			"}\n");
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		auto& model = compiler.GetCompilationUnit(id)->GetProgramModelForLowering();

		auto fnScope = model.PushScope(Scripting::Scope::Kind::Function, "main");
		Scripting::SymbolInfo info;
		info.kind = Scripting::SymbolKind::Variable;
		auto nt = model.QueryType("int32");
		TEST_ASSERT(nt.Ok());
		info.type = nt.value;
		TEST_ASSERT(model.RegisterLocal("n", info));

		// A lower system lowering `main` needs, and gets:
		// 1. Its own local (just registered).
		auto v = model.QueryVariable("n");
		TEST_ASSERT(v.Ok() && v.value.type == nt.value);

		// 2. A type by name.
		auto g = model.QueryType("Point");
		TEST_ASSERT(g.Ok());

		// 3. A function signature + its lowering record (body node for emission).
		auto f = model.QueryFunction("Add");
		TEST_ASSERT(f.Ok());
		TEST_ASSERT_EQ((int)f.value->GetParameterCount(), 2);
		auto rec = model.RecordFor(f.value);
		TEST_ASSERT(rec != nullptr && rec->bodyNode != nullptr);

		// 4. A constant (const variable) and an enum member.
		auto k = model.QueryConstant("K");
		TEST_ASSERT(k.Ok());
		TEST_ASSERT_EQ((int)k.value.intValue, 40);
		auto gm = model.QueryConstant("Green");
		TEST_ASSERT(gm.Ok());
		TEST_ASSERT_EQ((int)gm.value.intValue, 5);

		// 5. Layout facts for a type (what codegen needs).
		auto sz = model.SizeOf(g.value);
		TEST_ASSERT(sz.Ok());
		TEST_ASSERT_EQ((int)sz.value, 8); // two floats

		// 6. A member binding on the type.
		auto m = model.QueryMember(g.value, "x");
		TEST_ASSERT(m.Ok() && m.value.IsField);

		// 7. A parameter type resolves through ResolveType (what an argument needs).
		auto fn = FindFunction(compiler, id, "Add");
		TEST_ASSERT(fn != nullptr);
		auto pt = model.ResolveType(*fn->GetParameters()[0]->GetType());
		TEST_ASSERT(pt.Ok());
		TEST_ASSERT_EQ((int)pt.value->GetSize(), 4); // int32
		TEST_PASSED();
	}
} // namespace Testing
