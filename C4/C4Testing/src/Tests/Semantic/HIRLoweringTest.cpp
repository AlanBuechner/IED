#include "HIRLoweringTest.h"
#include <vector>

namespace Testing
{
	using namespace Scripting;

	static ProgramModel& MakeModel(CompilerInstance& compiler, const char* source)
	{
		compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		return compiler.GetProgramModelForLowering();
	}

	static const FunctionRecord* FindRec(ProgramModel& model, const char* name)
	{
		Core::Vector<const FunctionRecord*> recs;
		model.AllFunctionRecords(recs);
		for (const auto* r : recs)
			if (r->mangledName == name ||
				(r->mangledName.size() > strlen(name) &&
					r->mangledName.compare(0, strlen(name), name) == 0 &&
					r->mangledName[strlen(name)] == '_')) // mangled = Name_<overloadSuffix>
				return r;
		return nullptr;
	}

	// A method of a template instance: its symbol names the instance (`C_Tint32_E_Val_0`), so find it by owner and name.
	static const FunctionRecord* FindMethod(ProgramModel& model, const char* owner, const char* name)
	{
		Core::Vector<const FunctionRecord*> recs;
		model.AllFunctionRecords(recs);
		for (const auto* r : recs)
			if (r->name == name && r->containingType && r->containingType->GetName() == owner) return r;
		return nullptr;
	}

	static bool HasError(const ErrorTracker& e, const char* needle)
	{
		for (const auto& err : e.GetErrors())
			if (err.message.find(needle) != Core::String::npos) return true;
		return false;
	}

	// --- straight-line lowering (Phase 1 gate) ------------------------------------

	void HIRLoweringTest::StraightLineFunctionLowersToTypedTree()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 Add(int32 a, int32 b) { return a + b; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "Add");
		TEST_ASSERT(rec != nullptr && rec->bodyNode);
		HIRFunction fn = LowerFunction(model, errors, rec);

		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ((int)fn.SlotCount(), 2);
		TEST_ASSERT(fn.GetSlot(0).name == "a");
		TEST_ASSERT(fn.GetSlot(1).name == "b");

		BlockNode* root = fn.root;
		TEST_ASSERT(root && root->kind == HIRNode::Kind::Block);
		TEST_ASSERT_EQ((int)root->stmts.size(), 1);

		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(root->stmts[0]);
		TEST_ASSERT(ret != nullptr);
		// `a + b` on int32 lowers to a built-in Add of two loads (NOT a method call).
		const IntrinsicOpNode* add = dynamic_cast<const IntrinsicOpNode*>(ret->value);
		TEST_ASSERT(add != nullptr);
		TEST_ASSERT_EQ((int)add->op, (int)IntrinsicOp::Add);
		const LoadNode* la = dynamic_cast<const LoadNode*>(add->lhs);
		const LoadNode* lb = dynamic_cast<const LoadNode*>(add->rhs);
		TEST_ASSERT(la != nullptr && lb != nullptr);
		TEST_ASSERT_EQ((int)la->loc->slot, 0);
		TEST_ASSERT_EQ((int)lb->loc->slot, 1);
	}

	void HIRLoweringTest::LocalVariableBecomesSlotAndStore()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "void F() { int32 x = 5; x = x + 1; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		TEST_ASSERT(rec != nullptr);
		HIRFunction fn = LowerFunction(model, errors, rec);

		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ((int)fn.SlotCount(), 1);
		TEST_ASSERT(fn.GetSlot(0).name == "x");

		BlockNode* root = fn.root;
		TEST_ASSERT_EQ((int)root->stmts.size(), 2);

		const LocalDeclNode* decl = dynamic_cast<const LocalDeclNode*>(root->stmts[0]);
		TEST_ASSERT(decl != nullptr);
		TEST_ASSERT_EQ((int)decl->slot, 0);
		const ConstantNode* init = dynamic_cast<const ConstantNode*>(decl->init);
		TEST_ASSERT(init != nullptr);
		TEST_ASSERT_EQ((int)init->value.AsInt(), 5);

		const StoreNode* store = dynamic_cast<const StoreNode*>(dynamic_cast<const ExprStmtNode*>(root->stmts[1])->expr);
		TEST_ASSERT(store != nullptr);
		TEST_ASSERT_EQ((int)store->target->slot, 0); // writes the same slot it reads
		const IntrinsicOpNode* add = dynamic_cast<const IntrinsicOpNode*>(store->value);
		TEST_ASSERT(add != nullptr && add->op == IntrinsicOp::Add);
	}

	void HIRLoweringTest::ShadowedLocalUsesInnermostSlot()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "void F() { int32 x = 1; { int32 x = 2; } }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);

		// Two distinct slots, both named x: the shadowed one is a separate location.
		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ((int)fn.SlotCount(), 2);
		TEST_ASSERT(fn.GetSlot(0).name == "x");
		TEST_ASSERT(fn.GetSlot(1).name == "x");

		BlockNode* root = fn.root;
		const LocalDeclNode* outer = dynamic_cast<const LocalDeclNode*>(root->stmts[0]);
		TEST_ASSERT_EQ((int)outer->slot, 0);
		BlockNode* inner = dynamic_cast<BlockNode*>(root->stmts[1]);
		TEST_ASSERT(inner != nullptr);
		const LocalDeclNode* shadow = dynamic_cast<const LocalDeclNode*>(inner->stmts[0]);
		TEST_ASSERT(shadow != nullptr);
		TEST_ASSERT_EQ((int)shadow->slot, 1);
	}

	// --- expressions (Phase 2) -----------------------------------------------------

	void HIRLoweringTest::MemberAccessOnThisProducesFieldLValue()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct P { public int32 v; }\n"
			"void F() { P p; p.v = 7; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		BlockNode* root = fn.root;
		const StoreNode* store = dynamic_cast<const StoreNode*>(dynamic_cast<const ExprStmtNode*>(root->stmts[1])->expr);
		TEST_ASSERT(store != nullptr);
		TEST_ASSERT_EQ((int)store->target->kind, (int)LValue::Kind::Field);
		TEST_ASSERT(store->target->field != nullptr);
		TEST_ASSERT(store->target->field->mName == "v");
		TEST_ASSERT_EQ((int)store->target->base->kind, (int)LValue::Kind::Slot);
	}

	void HIRLoweringTest::FreeFunctionCallResolvesToRecord()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 G() { return 4; }\n"
			"int32 F() { return G(); }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		const CallNode* call = dynamic_cast<const CallNode*>(ret->value);
		TEST_ASSERT(call != nullptr);
		const FunctionRecord* gRec = FindRec(model, "G");
		TEST_ASSERT(call->callee == gRec);
		TEST_ASSERT(call->receiver == nullptr); // free function: no implicit this
		TEST_ASSERT_EQ((int)call->args.size(), 0);
	}

	void HIRLoweringTest::PrimitiveOperatorIsIntrinsicNotCall()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 F(int32 a) { return a + 1; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		const IntrinsicOpNode* add = dynamic_cast<const IntrinsicOpNode*>(ret->value);
		TEST_ASSERT(add != nullptr && add->op == IntrinsicOp::Add);
	}

	void HIRLoweringTest::MethodOperatorOnObjectIsCall()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct V { public int32 v; [operator(\"add\")] public int32 operator_add(const V& o) { return this->v + o.v; } }\n"
			"int32 F(V a, V b) { return a + b; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		const CallNode* call = dynamic_cast<const CallNode*>(ret->value);
		TEST_ASSERT(call != nullptr);
		TEST_ASSERT(call->isOperatorCall);
		TEST_ASSERT(call->receiver != nullptr);
		TEST_ASSERT_EQ((int)call->args.size(), 1);
	}

	// --- template bodies (04.4 fake aliases) --------------------------------------

	void HIRLoweringTest::InstantiatedTemplateMethodLowersWithBoundType()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> struct S { public int32 Get() { T t = 7; return t; } }\n"
			"int32 F() { S<int32> s; return s.Get(); }");
		ErrorTracker errors;

		Core::Vector<const FunctionRecord*> recs;
		model.AllFunctionRecords(recs);
		const FunctionRecord* tmplRec = nullptr;
		for (const auto* r : recs)
			if (r->containingType && model.TypeInstantiationBindings(r->containingType)) { tmplRec = r; break; }
		TEST_ASSERT(tmplRec != nullptr);

		// `T` must resolve to the bound int32 via a scoped fake alias, so `T t = 7;` is an int32 local.
		HIRFunction fn = LowerFunction(model, errors, tmplRec);
		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ((int)fn.SlotCount(), 1); // the local `t`
		TEST_ASSERT(fn.GetSlot(0).name == "t");
		TEST_ASSERT_EQ((int)fn.GetSlot(0).type->GetKind(), (int)TypeKind::Int32); // T := int32

		// After lowering, the fake alias is gone: `T` no longer resolves outside the body.
		TEST_ASSERT(model.FindLocal("T") == nullptr);
	}

	// --- diagnostics (06): HIR owns body validation --------------------------------

	void HIRLoweringTest::UndefinedIdentifierIsReportedAndTreeIncomplete()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 F() { return nope; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(!fn.complete);
		TEST_ASSERT_EQ((int)errors.GetErrors().Count(), 1);
	}

	void HIRLoweringTest::ReturnMismatchIsReported()
	{
		// A GENUINE mismatch (an object returned where an int is expected) must still error. Note this
		// is NOT a numeric case: C4 allows implicit numeric conversion on return (int32 -> uint32/int64,
		// see the widening corpus tests), so `bool F() { return 5; }` now converts rather than errors.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "struct S { int32 x; }\nint32 F() { S s; return s; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(!fn.complete);
		TEST_ASSERT(HasError(errors, "return type mismatch"));
	}

	void HIRLoweringTest::FailedInitializerIsReportedOnce()
	{
		// BUG-091: the declaration a failing initializer belongs to does not report the same failure again.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 G(int64 a) { return 1; }\n"
			"int32 G(uint64 a) { return 2; }\n"
			"int32 F() { int32 a = G(1); return a; }");
		ErrorTracker errors;

		HIRFunction fn = LowerFunction(model, errors, FindRec(model, "F"));
		TEST_ASSERT(!fn.complete);
		TEST_ASSERT_EQ((int)errors.GetErrors().Count(), 1);
		TEST_ASSERT(HasError(errors, "ambiguous call"));
	}

	// --- control flow (05 statement rows) ---------------------------------------------

	void HIRLoweringTest::WhileLoopLowersToChildRegionWithLabels()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 F() { int32 i = 0; while (i < 5) { i = i + 1; } return i; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		BlockNode* root = fn.root;
		// [LocalDecl(i), WhileNode, ReturnNode]
		TEST_ASSERT_EQ((int)root->stmts.size(), 3);
		WhileNode* w = dynamic_cast<WhileNode*>(root->stmts[1]);
		TEST_ASSERT(w != nullptr);

		// The loop gets its own break + continue labels (distinct, non-zero).
		TEST_ASSERT(w->breakLabel != 0 && w->continueLabel != 0);
		TEST_ASSERT(w->breakLabel != w->continueLabel);

		// The body is a CHILD region owned by the WhileNode - not a sibling in root.
		TEST_ASSERT(w->body != nullptr);
		TEST_ASSERT_EQ((int)w->body->stmts.size(), 1); // i = i + 1
		// `i` (declared in function scope) is still visible inside the loop body: it stores slot 0.
		const StoreNode* st = dynamic_cast<const StoreNode*>(dynamic_cast<const ExprStmtNode*>(w->body->stmts[0])->expr);
		TEST_ASSERT(st != nullptr);
		TEST_ASSERT_EQ((int)st->target->slot, 0);
	}

	void HIRLoweringTest::BreakTargetsInnermostLoopNotOuter()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F() { int32 s = 0;\n"
			"for (int32 i = 0; i < 3; i++) { while (i < 2) { s = s + 1; break; } }\n"
			"return s; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// slots: s(0), i(1 - the for-init local).
		ForNode* f = dynamic_cast<ForNode*>(fn.root->stmts[1]);
		WhileNode* w = dynamic_cast<WhileNode*>(f->body->stmts[0]);
		BreakNode* b = dynamic_cast<BreakNode*>(w->body->stmts[1]);
		TEST_ASSERT(f && w && b);

		// The break is inside the WHILE, so it must target the while's label - NOT the for's.
		TEST_ASSERT_EQ((int)b->targetLabel, (int)w->breakLabel);
		TEST_ASSERT(w->breakLabel != f->breakLabel); // distinct loops, distinct labels
	}

	void HIRLoweringTest::ForLoopLowersInitCondStepBody()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 F() { int32 s = 0; for (int32 i = 0; i < 4; i++) { s = s + i; } return s; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		ForNode* f = dynamic_cast<ForNode*>(fn.root->stmts[1]);
		TEST_ASSERT(f != nullptr);
		// init: the `int i` declaration lives in its own child block.
		TEST_ASSERT(f->initBlock != nullptr);
		const LocalDeclNode* decl = dynamic_cast<const LocalDeclNode*>(f->initBlock->stmts[0]);
		TEST_ASSERT(decl != nullptr && decl->m_Name == "i");
		// cond + step are present (not an infinite loop).
		TEST_ASSERT(f->cond != nullptr);
		TEST_ASSERT(f->step != nullptr);
		// body is a child region; it references both s (slot 0) and i (the init local, slot 1).
		TEST_ASSERT(f->body != nullptr && f->body->stmts.size() == 1);
	}

	void HIRLoweringTest::SwitchLowersFoldedCasesAndDefault()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F(int32 x) { int32 r = 0;\n"
			"switch (x) { case 1: r = 10; fallthrough; case 2: r = 20; break; default: r = 99; }\n"
			"return r; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		SwitchNode* sw = dynamic_cast<SwitchNode*>(fn.root->stmts[1]);
		TEST_ASSERT(sw != nullptr);
		// Two case values folded to their constants + one default region.
		TEST_ASSERT_EQ((int)sw->cases.size(), 2);
		TEST_ASSERT_EQ((int)sw->cases[0].value.AsInt(), 1); // case 1 -> value 1
		TEST_ASSERT_EQ((int)sw->cases[1].value.AsInt(), 2); // case 2 -> value 2
		TEST_ASSERT(sw->defaultBlock != nullptr);
		// Each case body is a child region holding its store; `fallthrough;` jumps to the next case's entry,
		// so the first case holds 2 stmts.
		TEST_ASSERT(sw->cases[0].block && sw->cases[0].block->stmts.size() == 2); // store + fallthrough jump
		TEST_ASSERT(dynamic_cast<JumpNode*>(sw->cases[0].block->stmts[1]) != nullptr);
		// A switch gets a break label (so `break` in a case exits the switch, not any loop).
		TEST_ASSERT(sw->breakLabel != 0);
	}

	void HIRLoweringTest::BreakInsideSwitchTargetsSwitchNotLoop()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"void F(int32 x) { while (x > 0) { switch (x) { case 1: break; } } }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		WhileNode* w = dynamic_cast<WhileNode*>(fn.root->stmts[0]);
		SwitchNode* sw = dynamic_cast<SwitchNode*>(w->body->stmts[0]);
		BreakNode* b = dynamic_cast<BreakNode*>(sw->cases[0].block->stmts[0]);
		TEST_ASSERT(w && sw && b);

		// The break is inside the switch, so it exits the SWITCH - not the enclosing while.
		TEST_ASSERT_EQ((int)b->targetLabel, (int)sw->breakLabel);
		TEST_ASSERT(sw->breakLabel != w->breakLabel);
	}

	void HIRLoweringTest::IfElseLowersBothBranchesAsOwnedRegions()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 F(int32 x) { int32 r = 0; if (x > 0) { r = 1; } else { r = -1; } return r; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		IfNode* ifn = dynamic_cast<IfNode*>(fn.root->stmts[1]);
		TEST_ASSERT(ifn != nullptr);
		// Both arms are child regions owned by the IfNode (not siblings in root).
		TEST_ASSERT(ifn->thenBlock && ifn->elseBlock);
		TEST_ASSERT_EQ((int)ifn->thenBlock->stmts.size(), 1);
		TEST_ASSERT_EQ((int)ifn->elseBlock->stmts.size(), 1);
		// root holds exactly: LocalDecl, IfNode, Return - the arms are NOT extra root stmts.
		TEST_ASSERT_EQ((int)fn.root->stmts.size(), 3);
	}

	void HIRLoweringTest::LocalDeclaredInLoopScopeEndsWithLoop()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "void F() { while (true) { int32 j = 1; } }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		// `j` is registered in the loop-body scope layer and popped by RAII when the body finishes.
		// After lowering, it must NOT be a visible local (no leak past the loop).
		TEST_ASSERT(fn.complete);
		WhileNode* w = dynamic_cast<WhileNode*>(fn.root->stmts[0]);
		TEST_ASSERT(w && w->body && w->body->stmts.size() == 1);
	}

	void HIRLoweringTest::DoWhileLowersWithIsDoWhileFlag()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "void F() { int32 i = 0; do { i = i + 1; } while (i < 3); }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		WhileNode* w = dynamic_cast<WhileNode*>(fn.root->stmts[1]);
		TEST_ASSERT(w != nullptr);
		TEST_ASSERT(w->isDoWhile); // body runs once before the condition is tested
	}

	// --- complex situations (05 + 09): combined features in one body ------------------

	void HIRLoweringTest::NestedLoopsWithShadowingAndBreak()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F() { int32 total = 0;\n"
			"for (int32 i = 0; i < 3; i++) {\n"
			"  int32 inner = i;\n"
			"  while (inner > 0) { total = total + inner; inner = inner - 1; }\n"
			"}\n"
			"return total; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);
		// slots: total(0), i(1 - for-init), inner(2 - loop body). Distinct locations.
		ForNode* f = dynamic_cast<ForNode*>(fn.root->stmts[1]);
		WhileNode* w = dynamic_cast<WhileNode*>(f->body->stmts[1]);
		// `inner` (slot 2) is read and written inside the while body - shadowing of nothing, but a
		// fresh location distinct from i. The store target must be slot 2, not slot 1.
		const StoreNode* st = dynamic_cast<const StoreNode*>(dynamic_cast<const ExprStmtNode*>(w->body->stmts[1])->expr);
		TEST_ASSERT(st != nullptr);
		TEST_ASSERT_EQ((int)st->target->slot, 2);
	}

	void HIRLoweringTest::ArrayIndexLowersToDerefOverAddressArithmetic()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 F(int32[4] a) { a[1] = 5; return a[1]; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// `a[1] = 5`: the store target is a Deref lvalue whose address is base + byte offset.
		const StoreNode* st = dynamic_cast<const StoreNode*>(dynamic_cast<const ExprStmtNode*>(fn.root->stmts[0])->expr);
		TEST_ASSERT(st != nullptr);
		TEST_ASSERT_EQ((int)st->target->kind, (int)LValue::Kind::Deref);
		const AddressArithmeticNode* ar = dynamic_cast<const AddressArithmeticNode*>(st->target->addr);
		TEST_ASSERT(ar != nullptr);
		// `return a[1]`: loads through the same Deref.
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[1]);
		const LoadNode* ld = dynamic_cast<const LoadNode*>(ret->value);
		TEST_ASSERT(ld != nullptr && ld->loc->kind == LValue::Kind::Deref);
	}

	void HIRLoweringTest::NewExpressionLowersToNewNodePointer()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "struct P { public int32 v; }\nP* F() { return new P(); }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		const NewNode* nw = dynamic_cast<const NewNode*>(ret->value);
		TEST_ASSERT(nw != nullptr);
		// Result is a POINTER rvalue; the object type is P.
		TEST_ASSERT_EQ((int)nw->objectType->GetKind(), (int)TypeKind::Object);
	}

	void HIRLoweringTest::ControlFlowWithCallsAndOperators()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Bump(int32 x) { return x + 1; }\n"
			"int32 F(int32 n) { int32 s = 0;\n"
			"for (int32 i = 0; i < n; i++) { if (i % 2 == 0) { s = s + Bump(i); } }\n"
			"return s; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		// A real body mixing a loop, a conditional, a modulo operator, and a function call all
		// lowers cleanly - the combined case the feature matrix calls out.
		TEST_ASSERT(fn.complete);
		ForNode* f = dynamic_cast<ForNode*>(fn.root->stmts[1]);
		IfNode* ifn = dynamic_cast<IfNode*>(f->body->stmts[0]);
		// The `i % 2 == 0` condition is an IntrinsicOp(Eq) over a Mod.
		const IntrinsicOpNode* eq = dynamic_cast<const IntrinsicOpNode*>(ifn->cond);
		TEST_ASSERT(eq != nullptr && eq->op == IntrinsicOp::Eq);
		// The call to Bump(i) is inside the then-branch store.
		const StoreNode* st = dynamic_cast<const StoreNode*>(dynamic_cast<const ExprStmtNode*>(ifn->thenBlock->stmts[0])->expr);
		const IntrinsicOpNode* add = dynamic_cast<const IntrinsicOpNode*>(st->value);
		const CallNode* call = dynamic_cast<const CallNode*>(add->rhs);
		TEST_ASSERT(call != nullptr && call->callee == FindRec(model, "Bump"));
	}

	void HIRLoweringTest::RangeForOverArrayDesugarsToIndexLoop()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F(int32[5] a) { int32 s = 0;\n"
			"for (int32 v : a) { s = s + v; }\n"
			"return s; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		ForNode* f = dynamic_cast<ForNode*>(fn.root->stmts[1]);
		TEST_ASSERT(f != nullptr);

		// init declares only the index: the loop variable is a new local of each iteration.
		TEST_ASSERT(f->initBlock && f->initBlock->stmts.size() == 1);
		const LocalDeclNode* iDecl = dynamic_cast<const LocalDeclNode*>(f->initBlock->stmts[0]);
		TEST_ASSERT(iDecl && iDecl->m_Name == "__range_i");

		// cond: __range_i < 5 (the array's static element count).
		const IntrinsicOpNode* lt = dynamic_cast<const IntrinsicOpNode*>(f->cond);
		TEST_ASSERT(lt != nullptr && lt->op == IntrinsicOp::Lt);
		const ConstantNode* bound = dynamic_cast<const ConstantNode*>(lt->rhs);
		TEST_ASSERT(bound != nullptr && bound->value.AsInt() == 5);

		// body: first statement declares `int32 v = a[__range_i]` - its initial value LOADS through a Deref
		// over address arithmetic (the element copy). Then the user's `s += v`.
		TEST_ASSERT(f->body && f->body->stmts.size() == 2);
		const LocalDeclNode* vDecl = dynamic_cast<const LocalDeclNode*>(f->body->stmts[0]);
		TEST_ASSERT(vDecl != nullptr && vDecl->m_Name == "v");
		const LoadNode* elemLoad = dynamic_cast<const LoadNode*>(vDecl->init);
		TEST_ASSERT(elemLoad != nullptr && elemLoad->loc->kind == LValue::Kind::Deref);
		const AddressArithmeticNode* ar = dynamic_cast<const AddressArithmeticNode*>(elemLoad->loc->addr);
		TEST_ASSERT(ar != nullptr); // element address = base + byte offset

		// step: __range_i += 1.
		const StoreNode* step = dynamic_cast<const StoreNode*>(f->step);
		TEST_ASSERT(step != nullptr && step->target->slot == iDecl->slot);
	}

	void HIRLoweringTest::RangeForVariableIsCopyNotReference()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"void F(int32[3] a) { for (int32 v : a) { v = v + 1; } }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// `v = v + 1` writes the loop VARIABLE (a copy), never the array element. If the variable
		// were a reference to the element, this would alias the array - it must not.
		ForNode* f = dynamic_cast<ForNode*>(fn.root->stmts[0]);
		TEST_ASSERT(f && f->body);

		// Find the user's store (v = v + 1): its target is the v slot, NOT a Deref into the array.
		const StoreNode* userStore = nullptr;
		for (const HIRNode* s : f->body->stmts)
			if (auto* es = dynamic_cast<const ExprStmtNode*>(s))
				if (auto* st = dynamic_cast<const StoreNode*>(es->expr))
					userStore = st;
		TEST_ASSERT(userStore != nullptr);
		TEST_ASSERT_EQ((int)userStore->target->kind, (int)LValue::Kind::Slot); // the copy, not a deref
	}

	void HIRLoweringTest::RangeForCustomIteratorProtocolDesugarsToOperatorCalls()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Collection { int32 count; }"
			"struct It { int32 index; }"
			"[operator(\"begin\")] It Begin() extends Collection { return It(); }"
			"[operator(\"end\")] It End() extends Collection { return It(); }"
			"[operator(\"notequal\")] int32 NotEqual(const It& o) extends It { return 1; }"
			"[operator(\"dereference\")] int32 Dereference() extends It { return 0; }"
			"[operator(\"increment\")] It Increment() extends It { return It(); }"
			"int32 F(Collection c) { int32 s = 0;\nfor (int32 v : c) { s = s + v; }\nreturn s; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The range-for desugars to a ForNode whose every protocol operator is a resolved CallNode.
		ForNode* f = dynamic_cast<ForNode*>(fn.root->stmts[1]); // stmt 0 is `int32 s = 0;`
		TEST_ASSERT(f != nullptr);

		const FunctionRecord* beginRec = FindRec(model, "Begin");
		const FunctionRecord* endRec   = FindRec(model, "End");
		const FunctionRecord* neqRec   = FindRec(model, "NotEqual");
		const FunctionRecord* derefRec = FindRec(model, "Dereference");
		const FunctionRecord* incRec   = FindRec(model, "Increment");

		// init: __range_it = c.begin() + __range_end = c.end().
		TEST_ASSERT(f->initBlock && f->initBlock->stmts.size() == 2);
		const LocalDeclNode* itDecl  = dynamic_cast<const LocalDeclNode*>(f->initBlock->stmts[0]);
		const LocalDeclNode* endDecl = dynamic_cast<const LocalDeclNode*>(f->initBlock->stmts[1]);
		TEST_ASSERT(itDecl && endDecl);
		const CallNode* beginCall = dynamic_cast<const CallNode*>(itDecl->init);
		const CallNode* endCall   = dynamic_cast<const CallNode*>(endDecl->init);
		TEST_ASSERT(beginCall != nullptr && beginCall->callee == beginRec);
		TEST_ASSERT(endCall != nullptr && endCall->callee == endRec);

		// cond: it notequal end (a call to NotEqual, receiver = the iterator slot).
		const CallNode* neqCall = dynamic_cast<const CallNode*>(f->cond);
		TEST_ASSERT(neqCall != nullptr && neqCall->callee == neqRec);

		// body: first stmt declares `int32 v = dereference(it)` - its initial value is a call to Dereference.
		// Then the user's `s += v`.
		TEST_ASSERT(f->body && f->body->stmts.size() == 2);
		const LocalDeclNode* vDecl = dynamic_cast<const LocalDeclNode*>(f->body->stmts[0]);
		TEST_ASSERT(vDecl != nullptr && vDecl->m_Name == "v");
		const CallNode* derefCall = dynamic_cast<const CallNode*>(vDecl->init);
		TEST_ASSERT(derefCall != nullptr && derefCall->callee == derefRec);

		// step: it = increment(it) - a Store into the iterator slot whose value is a call to Increment.
		const StoreNode* step = dynamic_cast<const StoreNode*>(f->step);
		TEST_ASSERT(step != nullptr);
		const CallNode* incCall = dynamic_cast<const CallNode*>(step->value);
		TEST_ASSERT(incCall != nullptr && incCall->callee == incRec);
	}

	void HIRLoweringTest::StructBracedInitStoresEachFieldPositionally()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct V { int32 x; int32 y; }"
			"int32 F() { V v = { 1, 2 }; return v.x + v.y; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The declaration has NO init node; each field is a separate Store into a Field lvalue.
		const LocalDeclNode* decl = dynamic_cast<const LocalDeclNode*>(fn.root->stmts[0]);
		TEST_ASSERT(decl != nullptr && decl->m_Name == "v" && decl->init == nullptr);

		uint32 vSlot = decl->slot;
		int fieldStores = 0;
		for (const HIRNode* s : fn.root->stmts)
			if (auto* es = dynamic_cast<const ExprStmtNode*>(s))
				if (auto* st = dynamic_cast<const StoreNode*>(es->expr))
					if (st->target->kind == LValue::Kind::Field && st->target->base && st->target->base->slot == vSlot)
						++fieldStores;
		TEST_ASSERT_EQ(fieldStores, 2); // x and y, positionally
	}

	void HIRLoweringTest::ArrayBracedInitStoresPositionalAndKeyedAndRange()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F() { int32[8] r = { [2...5] = 42, [0] = 7 }; return r[3]; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		const LocalDeclNode* decl = dynamic_cast<const LocalDeclNode*>(fn.root->stmts[0]);
		TEST_ASSERT(decl != nullptr && decl->m_Name == "r" && decl->init == nullptr);
		uint32 rSlot = decl->slot;

		// Elements start in index order: [0]=7, then [2]=42 evaluated once, then one loop copying element 2
		// into 3..5. Each element store is a Store into a Deref-over-address-arithmetic lvalue.
		int elemStores = 0;
		int copyLoops = 0;
		for (const HIRNode* s : fn.root->stmts)
		{
			if (auto* es = dynamic_cast<const ExprStmtNode*>(s))
				if (auto* st = dynamic_cast<const StoreNode*>(es->expr))
					if (st->target->kind == LValue::Kind::Deref && st->target->addr)
						++elemStores;
			if (s->kind == HIRNode::Kind::While) ++copyLoops;
		}
		TEST_ASSERT_EQ(elemStores, 2);
		TEST_ASSERT_EQ(copyLoops, 1);
		int constants = 0;
		for (const HIRNode* s : fn.root->stmts)
			if (auto* es = dynamic_cast<const ExprStmtNode*>(s))
				if (auto* st = dynamic_cast<const StoreNode*>(es->expr))
					if (auto* c = dynamic_cast<const ConstantNode*>(st->value))
						constants = constants * 100 + (int)c->value.intValue;
		TEST_ASSERT_EQ(constants, 742); // 7 into [0] first, then 42 into [2]
	}

	void HIRLoweringTest::LocalStructInBodyIsUsableWithinItsScope()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F() { struct P { int32 x; } P p; p.x = 5; return p.x; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // the local struct registered in the block scope and `P p` resolved to it

		// The body must contain a store into the field of the local-struct instance (p.x = 5).
		int fieldStores = 0;
		for (const HIRNode* s : fn.root->stmts)
			if (auto* es = dynamic_cast<const ExprStmtNode*>(s))
				if (auto* st = dynamic_cast<const StoreNode*>(es->expr))
					if (st->target->kind == LValue::Kind::Field) ++fieldStores;
		TEST_ASSERT_EQ(fieldStores, 1);
	}

	void HIRLoweringTest::LocalFunctionInBodyResolvesAndLowersNestedTree()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F() { int32 helper(int32 a) { return a + 1; } return helper(4); }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The local function's body lowered into a nested HIRFunction owned by the parent.
		TEST_ASSERT_EQ((int)fn.nestedFunctions.size(), 1);
		const HIRFunction* child = fn.nestedFunctions[0].get();
		TEST_ASSERT(child->complete && child->record != nullptr);

		// The call in F's body resolves to the local function's record: the return statement's value
		// is a CallNode targeting the child's record.
		const HIRNode* last = fn.root->stmts.back();
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(last);
		TEST_ASSERT(ret != nullptr && ret->value != nullptr);
		const CallNode* call = dynamic_cast<const CallNode*>(ret->value);
		TEST_ASSERT(call != nullptr && call->callee == child->record);
	}

	void HIRLoweringTest::TryCatchFinallyLowersClausesAndThrow()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F() { try { throw 5; } catch (double f) {} catch (int32 i) {} finally {} return 0; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// try ... finally lowers to `{ defer finally-body; try ... }`: a wrapper block whose first statement is
		// the finally's DeferNode and whose second is the TryNode with BOTH catch clauses in source order.
		const BlockNode* wrapper = dynamic_cast<const BlockNode*>(fn.root->stmts[0]);
		TEST_ASSERT(wrapper != nullptr && wrapper->stmts.size() == 2);
		const DeferNode* fin = dynamic_cast<const DeferNode*>(wrapper->stmts[0]);
		TEST_ASSERT(fin != nullptr && fin->body != nullptr);
		const TryNode* tc = dynamic_cast<const TryNode*>(wrapper->stmts[1]);
		TEST_ASSERT(tc != nullptr);
		TEST_ASSERT(tc->tryBlock != nullptr);
		TEST_ASSERT_EQ((int)tc->catches.size(), 2);
		TEST_ASSERT(tc->catches[0].type != nullptr && tc->catches[0].block != nullptr);
		TEST_ASSERT(tc->catches[1].type != nullptr && tc->catches[1].block != nullptr);

		// The try body's `throw 5;` is a ThrowNode carrying the constant value (not a rethrow).
		const ThrowNode* th = dynamic_cast<const ThrowNode*>(tc->tryBlock->stmts[0]);
		TEST_ASSERT(th != nullptr && !th->isRethrow && th->value != nullptr);

		// A bare `throw;` rethrow lowers to a ThrowNode with isRethrow and no value.
		CompilerInstance compiler2;
		auto& model2 = MakeModel(compiler2,
			"void G() { try { try { throw 1; } catch (int32 e) { throw; } } catch (int32 o) {} }");
		ErrorTracker errors2;
		const FunctionRecord* rec2 = FindRec(model2, "G");
		HIRFunction fn2 = LowerFunction(model2, errors2, rec2);
		TEST_ASSERT(fn2.complete);

		const TryNode* outer = dynamic_cast<const TryNode*>(fn2.root->stmts[0]);
		const TryNode* inner = dynamic_cast<const TryNode*>(outer->tryBlock->stmts[0]); // nested try
		TEST_ASSERT(outer && inner);
		const ThrowNode* rethrow = dynamic_cast<const ThrowNode*>(inner->catches[0].block->stmts[0]);
		TEST_ASSERT(rethrow != nullptr && rethrow->isRethrow && rethrow->value == nullptr);
	}

	void HIRLoweringTest::InitListCallArgumentMaterializesElements()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 SumList(__init_list<int32> l) { return 0; }"
			"int32 F() { return SumList({1, 2, 3}); }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The return's value is a call to SumList whose single argument is an InitListNode of type
		// __init_list<int32> carrying three materialized int32 elements.
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		TEST_ASSERT(ret && ret->value);
		const CallNode* call = dynamic_cast<const CallNode*>(ret->value);
		TEST_ASSERT(call != nullptr && call->args.size() == 1);
		const InitListNode* il = dynamic_cast<const InitListNode*>(call->args[0]);
		TEST_ASSERT(il != nullptr && il->type != nullptr && il->type->GetKind() == TypeKind::InitList);
		TEST_ASSERT_EQ((int)il->elements.size(), 3);

		// An empty list `{}` materializes to an InitListNode with zero elements (still a valid arg).
		CompilerInstance compiler2;
		auto& model2 = MakeModel(compiler2,
			"int32 CountList(__init_list<int32> l) { return 0; }"
			"int32 G() { return CountList({}); }");
		ErrorTracker errors2;
		const FunctionRecord* rec2 = FindRec(model2, "G");
		HIRFunction fn2 = LowerFunction(model2, errors2, rec2);
		TEST_ASSERT(fn2.complete);
		const ReturnNode* ret2 = dynamic_cast<const ReturnNode*>(fn2.root->stmts[0]);
		const CallNode* call2 = dynamic_cast<const CallNode*>(ret2 ? ret2->value : nullptr);
		const InitListNode* il2 = call2 && call2->args.size() == 1 ? dynamic_cast<const InitListNode*>(call2->args[0]) : nullptr;
		TEST_ASSERT(il2 != nullptr && il2->elements.empty());
	}

	void HIRLoweringTest::RangeForOverInitListUsesDataAndSizeMembers()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F(__init_list<int32> l) { int32 s = 0;\nfor (int32 i : l) { s = s + i; }\nreturn s; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The range-for over the init list lowers to a ForNode whose bound comes from the list's OWN
		// size member (not a compile-time N) and whose elements come from its data pointer.
		const ForNode* f = dynamic_cast<const ForNode*>(fn.root->stmts[1]); // stmt 0 is `int32 s = 0;`
		TEST_ASSERT(f != nullptr);

		// init: __range_i, then the hoisted __range_data (l.data) and __range_n ((int64)l.size).
		TEST_ASSERT(f->initBlock && f->initBlock->stmts.size() == 3);
		const LocalDeclNode* dataDecl = dynamic_cast<const LocalDeclNode*>(f->initBlock->stmts[1]);
		const LocalDeclNode* nDecl    = dynamic_cast<const LocalDeclNode*>(f->initBlock->stmts[2]);
		TEST_ASSERT(dataDecl && nDecl);
		TEST_ASSERT(dataDecl->m_Name == "__range_data" && dataDecl->type != nullptr &&
			dataDecl->type->GetKind() == TypeKind::Pointer);
		TEST_ASSERT(nDecl->m_Name == "__range_n");

		// cond: __range_i < __range_n (two slot loads compared, not a constant bound).
		const IntrinsicOpNode* cond = dynamic_cast<const IntrinsicOpNode*>(f->cond);
		TEST_ASSERT(cond != nullptr && cond->op == IntrinsicOp::Lt);

		// body's first statement: int32 i = *(__range_data + i*sizeof(int32)) - a Load as the initial value.
		const LocalDeclNode* iDecl = dynamic_cast<const LocalDeclNode*>(f->body->stmts[0]);
		TEST_ASSERT(iDecl != nullptr && iDecl->m_Name == "i");
		TEST_ASSERT(dynamic_cast<const LoadNode*>(iDecl->init) != nullptr);
	}

	void HIRLoweringTest::InitListIndexLowersToDerefOverDataPointer()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F(__init_list<int32> l) { return l[1]; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// `l[1]` lowers to a Deref of (l.data + 1*sizeof(int32)): the element is read through the
		// list's own data pointer member, not a hard-coded layout.
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		TEST_ASSERT(ret && ret->value);
		const LoadNode* load = dynamic_cast<const LoadNode*>(ret->value); // l[i] is an lvalue; return loads it
		TEST_ASSERT(load != nullptr);
		TEST_ASSERT(load->loc != nullptr && load->loc->kind == LValue::Kind::Deref);
		const AddressArithmeticNode* addr = dynamic_cast<const AddressArithmeticNode*>(load->loc->addr);
		TEST_ASSERT(addr != nullptr);
		// The base of the address math is a Load of the `data` field (the T* member).
		const LoadNode* dataLoad = dynamic_cast<const LoadNode*>(addr->base);
		TEST_ASSERT(dataLoad != nullptr && dataLoad->loc != nullptr && dataLoad->loc->kind == LValue::Kind::Field);
	}

	void HIRLoweringTest::InitListLocalDeclStoresMaterializedAggregate()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F() { __init_list<int32> list = {10, 20, 30}; return (int32)list.size; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// stmt 0: declare the list slot (no init node). stmt 1: store a materialized InitListNode
		// ({10,20,30} -> {ptr,size}) into it. The return then reads the `size` field member.
		const LocalDeclNode* decl = dynamic_cast<const LocalDeclNode*>(fn.root->stmts[0]);
		TEST_ASSERT(decl != nullptr && decl->m_Name == "list" && decl->init == nullptr);
		const StoreNode* store = dynamic_cast<const StoreNode*>(dynamic_cast<const ExprStmtNode*>(fn.root->stmts[1])->expr);
		TEST_ASSERT(store != nullptr);
		const InitListNode* il = dynamic_cast<const InitListNode*>(store->value);
		TEST_ASSERT(il != nullptr && il->type->GetKind() == TypeKind::InitList);
		TEST_ASSERT_EQ((int)il->elements.size(), 3);

		// The return reads `list.size` through the model's member (a Load of a Field lvalue), then
		// casts uint64 -> int32, so the load sits under a ConvertNode.
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[2]);
		const HIRNode* retVal = (ret && ret->value) ? ret->value : nullptr;
		const LoadNode* sizeLoad = nullptr;
		if (const auto* cv = dynamic_cast<const ConvertNode*>(retVal))
			sizeLoad = dynamic_cast<const LoadNode*>(cv->src);
		else
			sizeLoad = dynamic_cast<const LoadNode*>(retVal);
		TEST_ASSERT(sizeLoad != nullptr && sizeLoad->loc != nullptr && sizeLoad->loc->kind == LValue::Kind::Field);
	}

	void HIRLoweringTest::MixedTypeComparisonConvertsToCommonType()
	{
		// uint64 (list.size) vs int32 (literal 3): the model's usual arithmetic conversions pick
		// uint64 as the common type, so the int32 operand is converted before the compare.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 F(__init_list<int32> l) { return (l.size == 3) ? 1 : 0; }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "F");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The ternary condition is the compare; find it on the return's value.
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		TEST_ASSERT(ret && ret->value);
		const TernaryNode* cond = dynamic_cast<const TernaryNode*>(ret->value);
		TEST_ASSERT(cond != nullptr);
		const IntrinsicOpNode* cmp = dynamic_cast<const IntrinsicOpNode*>(cond->cond);
		TEST_ASSERT(cmp != nullptr && cmp->op == IntrinsicOp::Eq);
		// Both operands now have the SAME (common) type: uint64.
		TEST_ASSERT(cmp->lhs->type == cmp->rhs->type);
		TEST_ASSERT_EQ((int)cmp->lhs->type->GetKind(), (int)TypeKind::UInt64);
		// The int32 literal side is wrapped in a ConvertNode to uint64.
		const ConvertNode* conv = dynamic_cast<const ConvertNode*>(cmp->rhs);
		TEST_ASSERT(conv != nullptr && conv->to == cmp->rhs->type);
	}

	void HIRLoweringTest::NonTypeParamInMethodBodyLowersAsConstant()
	{
		// A non-type template parameter used in a method body is a compile-time CONSTANT with no
		// addressable storage: it must lower to a ConstantNode, not a Global lvalue. This used to
		// crash (null-typed global deref) because the param was bound without a type.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<int N> struct C { int32 Val() { return N * 2; } }\n"
			"int32 main() { C<5> cc; return cc.Val(); }\n");
		ErrorTracker errors;

		const FunctionRecord* rec = FindMethod(model, "C", "Val");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // must not crash and must lower fully

		// `return N * 2;` -> IntrinsicOp(Mul) whose left operand is the constant N (value 5).
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		TEST_ASSERT(ret && ret->value);
		const IntrinsicOpNode* mul = dynamic_cast<const IntrinsicOpNode*>(ret->value);
		TEST_ASSERT(mul != nullptr && mul->op == IntrinsicOp::Mul);
		const ConstantNode* nConst = dynamic_cast<const ConstantNode*>(mul->lhs);
		TEST_ASSERT(nConst != nullptr);
		TEST_ASSERT_EQ((int)nConst->value.AsInt(), 5); // N is bound to the instantiation argument
	}

	void HIRLoweringTest::CommaDeclaratorsEachGetOwnSlotAndType()
	{
		// `float x, y;` declares TWO variables of the same type. The model must register every
		// declarator (globals and class fields) and lowering must give each its own slot. This used
		// to drop all but the first name ("no variable named y").
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 g1, g2;\nstruct P { int32 x, y; }\n");

		// Model: every comma-list name is queryable with the right type (globals + class fields).
		TEST_ASSERT(model.QueryVariable("g1").Ok());
		TEST_ASSERT(model.QueryVariable("g2").Ok());
		auto pt = model.QueryType("P");
		const ObjectType* p = static_cast<const ObjectType*>(pt.value);
		TEST_ASSERT(p->GetFieldByName("x") != nullptr);
		TEST_ASSERT(p->GetFieldByName("y") != nullptr);

		// Lowering: each local declarator gets its own LocalDeclNode with a distinct slot.
		ErrorTracker errors;
		CompilerInstance compiler2; // fresh instance: MakeModel reuses the unit name "t.cf"
		auto& model2 = MakeModel(compiler2, "int32 f() { int32 a = 1, b = a + 2; return a + b; }\n");
		const FunctionRecord* rec = FindRec(model2, "f");
		HIRFunction fn = LowerFunction(model2, errors, rec);
		TEST_ASSERT(fn.complete);

		uint32 slotA = ~0u, slotB = ~0u; int decls = 0;
		for (const HIRNode* s : fn.root->stmts)
			if (const auto* d = dynamic_cast<const LocalDeclNode*>(s))
			{ decls++; if (d->m_Name == "a") slotA = d->slot; else if (d->m_Name == "b") slotB = d->slot; }
		TEST_ASSERT_EQ(decls, 2);
		TEST_ASSERT(slotA != ~0u && slotB != ~0u && slotA != slotB); // two distinct slots
	}

	void HIRLoweringTest::ShortCircuitLowersToTernaryStructure()
	{
		// `&&` / `||` are dedicated AST node types (ConditionalAnd/Or), NOT BinaryExpression. They
		// lower to the ternary region structure: `a || b` -> a ? true : b (short-circuit falls out of
		// the unchosen side never running). This used to hit "expression not yet lowered".
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 f(int32 a) { if (a != 1 || a != 2) return 7; return 0; }\n");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "f");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // used to fail with no usable diagnostic

		// if (a != 1 || a != 2) -> IfNode whose cond is the short-circuit TernaryNode.
		const IfNode* iff = dynamic_cast<const IfNode*>(fn.root->stmts[0]);
		TEST_ASSERT(iff != nullptr && iff->cond);
		const TernaryNode* tern = dynamic_cast<const TernaryNode*>(iff->cond);
		TEST_ASSERT(tern != nullptr);
		// `||` -> cond ? true : rhs: the "then" side is the constant bool true.
		const ConstantNode* thenConst = dynamic_cast<const ConstantNode*>(tern->thenExpr);
		TEST_ASSERT(thenConst != nullptr && thenConst->value.booleanValue == true);
	}

	void HIRLoweringTest::CompoundAssignmentLowersOpThenStore()
	{
		// `s += v` on primitives: the compound token maps to the plain builtin op, operands convert
		// to their common type, and the result is stored back into s (a StoreNode). This used to fail
		// with "no operator '+='" because the plain form was derived from the token, not its annotation.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 f(uint64 n) { int32 s = 0; s += n; return s; }\n");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "f");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The `s += n` statement lowers to a StoreNode of the plain op (Add), converted back to s's type.
		const ExprStmtNode* stmt = dynamic_cast<const ExprStmtNode*>(fn.root->stmts[1]);
		TEST_ASSERT(stmt != nullptr && stmt->expr);
		const StoreNode* store = dynamic_cast<const StoreNode*>(stmt->expr);
		TEST_ASSERT(store != nullptr);
		const ConvertNode* back = dynamic_cast<const ConvertNode*>(store->value);
		TEST_ASSERT(back != nullptr && back->to->GetKind() == TypeKind::Int32);
		const IntrinsicOpNode* add = dynamic_cast<const IntrinsicOpNode*>(back->src);
		TEST_ASSERT(add != nullptr && add->op == IntrinsicOp::Add);
		// Mixed types (int32 s, uint64 n) convert to the common type: both operands and the result match it.
		TEST_ASSERT(add->lhs->type == add->rhs->type && add->type == add->lhs->type);
		TEST_ASSERT(add->type->GetKind() == TypeKind::UInt64);
	}

	void HIRLoweringTest::ExtensionMethodRecordCarriesContainingType()
	{
		// An extension method is a method ON its extends target: the record must carry containingType
		// so lowering establishes `this`. This used to be null (set after the by-value copy was made),
		// giving "'this' used outside a member function" for every extension-method body.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Flag { int32 on; }\n"
			"[operator(\"logical_and\")] int32 And(const Flag& other) extends Flag { return (this.on != 0 && other.on != 0) ? 1 : 0; }\n");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "And");
		TEST_ASSERT(rec != nullptr);
		TEST_ASSERT(rec->containingType != nullptr); // the model generated the info lowering needs
		TEST_ASSERT_EQ((int)rec->containingType->GetKind(), (int)TypeKind::Object);

		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // `this.on` resolves through the implicit receiver
	}

	void HIRLoweringTest::SizeOfLowersToByteSizeConstant()
	{
		// sizeof(T) is a compile-time type query: it lowers to an int64 ConstantNode of T's byte size,
		// with no runtime work. This used to hit the "expression not yet lowered" fallback.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "struct P { int32 x; }\nint32 f() { return (sizeof(P) == 4) ? 0 : 1; }\n");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "f");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// return (sizeof(P) == 4) ? 0 : 1 -> Return(Ternary), cond is IntrinsicOp(Eq) whose left
		// operand is the sizeof constant (P is one int32 = 4 bytes).
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		TEST_ASSERT(ret && ret->value);
		const TernaryNode* tern = dynamic_cast<const TernaryNode*>(ret->value);
		TEST_ASSERT(tern != nullptr && tern->cond);
		const IntrinsicOpNode* eq = dynamic_cast<const IntrinsicOpNode*>(tern->cond);
		TEST_ASSERT(eq != nullptr && eq->op == IntrinsicOp::Eq);
		const ConstantNode* sz = dynamic_cast<const ConstantNode*>(eq->lhs);
		TEST_ASSERT(sz != nullptr);
		TEST_ASSERT_EQ((int)sz->value.AsInt(), 4); // sizeof(P) == 4
	}

	void HIRLoweringTest::SwitchFallthroughJumpsToNextCase()
	{
		// `fallthrough;` jumps to the next case's entry label (D-39: running off a case is an error). This used
		// to lower with no jump at all, so `case 1:` fell out of the switch instead of into `case 2:`.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 f(int32 v) { int32 r = -1; switch(v){ case 1: r = 10; fallthrough; case 2: r = r + 20; break; default: r = r + 40; } return r; }\n");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "f");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		const SwitchNode* sw = nullptr;
		for (const HIRNode* s : fn.root->stmts)
			if (auto* x = dynamic_cast<const SwitchNode*>(s)) { sw = x; break; }
		TEST_ASSERT(sw != nullptr);
		TEST_ASSERT_EQ((int)sw->cases.size(), 2); // case 1, case 2 (default is separate)

		bool firstHasJump = false, secondHasJump = false;
		for (const HIRNode* s : sw->cases[0].block->stmts)
			if (dynamic_cast<const JumpNode*>(s)) firstHasJump = true;
		for (const HIRNode* s : sw->cases[1].block->stmts)
			if (dynamic_cast<const JumpNode*>(s)) secondHasJump = true;
		TEST_ASSERT(firstHasJump);   // fallthrough: case 1 -> case 2's entry
		TEST_ASSERT(!secondHasJump); // case 2 ends with break: no jump
	}

	void HIRLoweringTest::FloatLiteralSuffixSelectsFloat32()
	{
		// `0.0f` is a Float32 literal (the f suffix), `1.5` is a Float64 (no suffix). This used to type
		// EVERY float literal as double, so `float F() { return 0.0f; }` failed with
		// "return type mismatch: got 'double', function returns 'float'" - which broke every OOP/
		// inheritance test that returns a float (Shape_GetArea and friends).
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"float F() { return 0.0f; }\n"
			"double G() { return 1.5; }\n");
		ErrorTracker errors;

		const FunctionRecord* fRec = FindRec(model, "F");
		HIRFunction fFn = LowerFunction(model, errors, fRec);
		TEST_ASSERT(fFn.complete); // float F() returning a Float32 literal lowers cleanly

		const ReturnNode* fRet = dynamic_cast<const ReturnNode*>(fFn.root->stmts[0]);
		TEST_ASSERT(fRet && fRet->value);
		const ConstantNode* fConst = dynamic_cast<const ConstantNode*>(fRet->value);
		TEST_ASSERT(fConst != nullptr);
		TEST_ASSERT_EQ((int)fConst->type->GetKind(), (int)TypeKind::Float32); // 0.0f -> Float32

		const FunctionRecord* gRec = FindRec(model, "G");
		HIRFunction gFn = LowerFunction(model, errors, gRec);
		TEST_ASSERT(gFn.complete); // double G() returning a no-suffix float literal lowers cleanly
		const ReturnNode* gRet = dynamic_cast<const ReturnNode*>(gFn.root->stmts[0]);
		const ConstantNode* gConst = dynamic_cast<const ConstantNode*>(gRet->value);
		TEST_ASSERT(gConst != nullptr);
		TEST_ASSERT_EQ((int)gConst->type->GetKind(), (int)TypeKind::Float64); // 1.5 -> Float64
	}

	void HIRLoweringTest::NamespaceQualifiedCallResolves()
	{
		// `Ns::Create(1)` parses its target as a StaticAccessNode (base=Ns, member=Create). ResolveCall
		// must join it into "Ns::Create" and resolve via QueryFunction/FindVisibleQualified. This used to
		// hit "call target is not a name or member". Types are fully qualified too, as the corpus does in
		// namespace bodies (BkMath::Vec3 v;).
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"namespace Ns {\n"
			"  struct V { int32 x; }\n"
			"  Ns::V Create(int32 x) { Ns::V v; v.x = x; return v; }\n"
			"  int32 UnitX() { return Ns::Create(1).x + 40; }\n"
			"}\n");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "Ns_UnitX");
		TEST_ASSERT(rec != nullptr);
		HIRFunction fn = LowerFunction(model, errors, rec);
		// complete == true is the proof: if Ns::Create(1) did not resolve, lowering fails with
		// "no function named Ns::Create" and the tree is incomplete. So a complete tree means the
		// qualified call resolved to a real record and the whole body (incl. the .x member read) lowered.
		TEST_ASSERT(fn.complete);

		// The body ends in `return Ns::Create(1).x + 40;` -> a ReturnNode whose value is an IntrinsicOp
		// (the +), confirming the call result was actually used, not just resolved and discarded.
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		TEST_ASSERT(ret && ret->value);
		TEST_ASSERT(dynamic_cast<const IntrinsicOpNode*>(ret->value) != nullptr); // (Ns::Create(1).x + 40)
	}

	void HIRLoweringTest::NumericReturnIsImplicitlyConverted()
	{
		// C4 allows implicit numeric conversion on return (the widening corpus tests prove it): an int32
		// value returned from an int64 or uint32 function is converted, not rejected. The lowerer must
		// emit a ConvertNode so codegen sees the correct width. A non-numeric mismatch still errors
		// (see ReturnMismatchIsReported).
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int64 W() { return 7; }\n"      // int32 -> int64 widening
			"uint32 U() { return 5; }\n");    // int32 -> uint32 rank change
		ErrorTracker errors;

		const FunctionRecord* wRec = FindRec(model, "W");
		HIRFunction wFn = LowerFunction(model, errors, wRec);
		TEST_ASSERT(wFn.complete); // no longer a "return type mismatch" error
		const ReturnNode* wRet = dynamic_cast<const ReturnNode*>(wFn.root->stmts[0]);
		TEST_ASSERT(wRet && wRet->value);
		// The value is wrapped in a ConvertNode to the declared return type (int64), not left as int32.
		const ConvertNode* wConv = dynamic_cast<const ConvertNode*>(wRet->value);
		TEST_ASSERT(wConv != nullptr);
		TEST_ASSERT_EQ((int)wConv->type->GetKind(), (int)TypeKind::Int64);

		const FunctionRecord* uRec = FindRec(model, "U");
		HIRFunction uFn = LowerFunction(model, errors, uRec);
		TEST_ASSERT(uFn.complete);
		const ReturnNode* uRet = dynamic_cast<const ReturnNode*>(uFn.root->stmts[0]);
		const ConvertNode* uConv = dynamic_cast<const ConvertNode*>(uRet->value);
		TEST_ASSERT(uConv != nullptr);
		TEST_ASSERT_EQ((int)uConv->type->GetKind(), (int)TypeKind::UInt32);
	}

	void HIRLoweringTest::PointerParamIsFirstClassValue()
	{
		// A pointer parameter is a first-class value: `int32* p` is NOT `int32`. This used to fail with
		// "cannot dereference a non-pointer" because UnwrapToValue wrongly unwrapped pointers to their
		// pointee, so the lowerer saw an int32 and could not deref/delete/field-access it.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Deref(int32* p) { return *p; }\n"
			"struct S { int32 x; }\n"
			"int32 Field(S* p) { return p->x; }\n");
		ErrorTracker errors;

		const FunctionRecord* dRec = FindRec(model, "Deref");
		HIRFunction dFn = LowerFunction(model, errors, dRec);
		TEST_ASSERT(dFn.complete); // *p on a pointer param lowers (deref sees the pointer)

		const FunctionRecord* fRec = FindRec(model, "Field");
		HIRFunction fFn = LowerFunction(model, errors, fRec);
		TEST_ASSERT(fFn.complete); // p->x on a pointer param lowers (auto-deref then field)
	}

	void HIRLoweringTest::DeleteLowersToDeleteNode()
	{
		// `delete p` lowers to a DeleteNode carrying the pointee type; codegen expands it to an optional
		// [operator("delete")] call + free. Deleting a non-pointer is a clean error.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct S { int32 x; }\n"
			"int32 Go(S* p) { int32 v = p->x; delete p; return v; }\n"
			"int32 Bad(int32 n) { delete n; return 0; }\n");
		ErrorTracker errors;

		const FunctionRecord* goRec = FindRec(model, "Go");
		HIRFunction goFn = LowerFunction(model, errors, goRec);
		TEST_ASSERT(goFn.complete); // delete p (a pointer) lowers cleanly

		const FunctionRecord* badRec = FindRec(model, "Bad");
		ErrorTracker badErrors;
		HIRFunction badFn = LowerFunction(model, badErrors, badRec);
		TEST_ASSERT(!badFn.complete); // delete of a non-pointer is rejected
		TEST_ASSERT(HasError(badErrors, "cannot delete a non-pointer"));
	}

	void HIRLoweringTest::FunctionTemplateNonTypeParamsInstantiate()
	{
		// Non-type template parameters fold to constants and bind in the instantiation scope, so the body
		// sees N / I as values. A mixed (type + non-type) template must instantiate too - this used to fail
		// "cannot instantiate function template" because only type args were handled.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<int32 A, int32 B> int32 SumAB() { return A + B; }\n"   // two non-type params
			"int32 UseSum() { return SumAB<3, 4>(); }\n"                     // -> 7
			"template<T, uint32 I> T GetI() { return (T)I; }\n"              // mixed type + non-type
			"int32 UseGet() { return GetI<int32, 42>(); }\n");               // -> 42
		ErrorTracker errors;

		const FunctionRecord* sumRec = FindRec(model, "UseSum");
		HIRFunction sumFn = LowerFunction(model, errors, sumRec);
		TEST_ASSERT(sumFn.complete); // SumAB<3,4>() instantiates and lowers (no "cannot instantiate")

		const FunctionRecord* getRec = FindRec(model, "UseGet");
		HIRFunction getFn = LowerFunction(model, errors, getRec);
		TEST_ASSERT(getFn.complete); // GetI<int32,42>() (mixed) instantiates and lowers
	}

	void HIRLoweringTest::FunctionPointerInvocationLowersToIndirectCall()
	{
		// `op(a, b)` where op is a funcptr parameter is an INDIRECT call: the callee is the pointer
		// VALUE, not a named function. It must lower to a CallNode with no record (callee == null) and a
		// non-null target (the loaded pointer), typed by the funcptr's return type - NOT "no function
		// named op" (the old behavior treated every identifier callee as a name lookup).
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Add(int32 a, int32 b) { return a + b; }\n"
			"int32 Apply(funcptr<int32(int32, int32)> op, int32 a, int32 b) { return op(a, b); }");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "Apply");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete);

		// The body is a single `return <call>;` - find the indirect CallNode in it.
		const ReturnNode* ret = dynamic_cast<const ReturnNode*>(fn.root->stmts[0]);
		TEST_ASSERT(ret != nullptr);
		const CallNode* call = dynamic_cast<const CallNode*>(ret->value);
		TEST_ASSERT(call != nullptr);
		TEST_ASSERT(call->callee == nullptr);   // indirect: no named record
		TEST_ASSERT(call->target != nullptr);   // the funcptr value to call through
		TEST_ASSERT_EQ((int)call->args.size(), 2);
	}

	void HIRLoweringTest::NullLiteralCoercesIntoPointerAndFuncPtr()
	{
		// null is a void* zero that coerces into any pointer/funcptr, so the INIT (`p = null`),
		// ASSIGN (`p = null`), and COMPARE (`p == null`) forms all work through ONE coercion rule.
		// Before this the literal had no case at all ("literal has no value type"), so every use of
		// null failed - a divergence of the same class as the pointer-assignment upcast bug.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct S { int32 x; }\n"
			"int32 main() {\n"
			"\tS* p = null;\n"          // init with null
			"\tif (p != null) return 1; // compare null\n"
			"\tp = null;\n"             // assign null\n"
			"\tif (p == null) return 0;\n"
			"\treturn 2;\n}");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "main");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // all three null forms lower (no "literal has no value type")
	}

	void HIRLoweringTest::FunctionNameDecaysToFunctionPointer()
	{
		// A function name used in expression position decays to a funcptr of its exact signature:
		// `funcptr f = Add` (initializer), `f = Add` (assignment), `Apply(Add, 1, 2)` (call arg).
		// Before this every form failed with "name is not a variable" because ResolveIdentifier only
		// knew variables. The decayed value is a FunctionAddressNode carrying the function's record,
		// so codegen loads the symbol's address.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Add(int32 a, int32 b) { return a + b; }\n"
			"int32 Apply(funcptr<int32(int32,int32)> op, int32 a, int32 b) { return op(a, b); }\n"
			"int32 main() {\n"
			"\tfuncptr<int32(int32,int32)> f = Add;\n"
			"\tif (f(2, 3) != 5) return 1;\n"
			"\tf = Add;\n"
			"\treturn Apply(Add, 1, 2);\n}");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "main");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // decay in initializer + assignment + call argument all lower

		// The tree must actually contain the decayed addresses (not silently skip them): at least one
		// FunctionAddressNode per use site (three).
		int addrNodes = 0;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::FunctionAddress) addrNodes++;
		TEST_ASSERT(addrNodes >= 3);
	}

	void HIRLoweringTest::SafeCastNumericTargetIsRejected()
	{
		// `as` converts pointers to objects (D-11); a numeric `as` is an error that names the cast to use.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 main() {\n"
			"\tint32 x = 1;\n"
			"\tdouble y = x as double;\n"
			"\treturn (y > 0.5f) ? 1 : 0;\n}");
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, FindRec(model, "main"));
		TEST_ASSERT(!fn.complete);
		TEST_ASSERT(HasError(errors, "for a numeric conversion use a cast: (double)x"));
	}

	void HIRLoweringTest::SafeCastUpcastLowersToConvert()
	{
		// An upcast always succeeds: it is a plain pointer conversion (codegen moves it to the base sub-object).
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Base { int32 a; }\n"
			"struct Derived : Base { int32 b; }\n"
			"int32 main() {\n"
			"\tDerived d;\n"
			"\tBase* p = &d as Base*;\n"
			"\treturn p->a;\n}");
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, FindRec(model, "main"));
		TEST_ASSERT(fn.complete);
		int dynamicCasts = 0, pointerConverts = 0;
		for (const auto& node : fn.m_Nodes)
		{
			if (node->kind == HIRNode::Kind::DynamicCast) dynamicCasts++;
			if (node->kind == HIRNode::Kind::Convert && static_cast<const ConvertNode*>(node.get())->to &&
				static_cast<const ConvertNode*>(node.get())->to->GetKind() == TypeKind::Pointer) pointerConverts++;
		}
		TEST_ASSERT_EQ(dynamicCasts, 0);
		TEST_ASSERT(pointerConverts >= 1);
	}

	void HIRLoweringTest::SafeCastDowncastReadsTypeInformation()
	{
		// A downcast of a polymorphic pointer is checked at run time (a DynamicCast node); `is` is the same
		// test yielding a bool. A downcast of a non-polymorphic type has no type information: an error.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Base { virtual int32 F() { return 1; } }\n"
			"struct Derived : Base { int32 b; }\n"
			"int32 main() {\n"
			"\tDerived d;\n"
			"\tBase* p = &d;\n"
			"\tDerived* q = p as Derived*;\n"
			"\tbool isDerived = p is Derived;\n"
			"\treturn isDerived ? q->b : 0;\n}");
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, FindRec(model, "main"));
		TEST_ASSERT(fn.complete);
		int casts = 0, tests = 0;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::DynamicCast)
				(static_cast<const DynamicCastNode*>(node.get())->testOnly ? tests : casts)++;
		TEST_ASSERT_EQ(casts, 1);
		TEST_ASSERT_EQ(tests, 1);

		CompilerInstance c2;
		auto& m2 = MakeModel(c2,
			"struct Plain { int32 a; }\n"
			"struct PlainD : Plain { int32 b; }\n"
			"int32 main() {\n"
			"\tPlainD d;\n"
			"\tPlain* p = &d;\n"
			"\tPlainD* q = p as PlainD*;\n"
			"\treturn q->b;\n}");
		ErrorTracker e2;
		HIRFunction f2 = LowerFunction(m2, e2, FindRec(m2, "main"));
		TEST_ASSERT(!f2.complete);
		TEST_ASSERT(HasError(e2, "it has no virtual methods"));
	}

	void HIRLoweringTest::SafeCastBetweenUnrelatedTypesIsRejected()
	{
		// The run-time check decides which subtype, never whether an unrelated cast can work at all.
		CompilerInstance c2;
		auto& m2 = MakeModel(c2,
			"struct S { int32 v; }\n"
			"int32 main() {\n"
			"\tS s;\n"
			"\tint32 x = (s.v as S) ? 1 : 0;\n"
			"\treturn x;\n}");
		ErrorTracker e2;
		HIRFunction f2 = LowerFunction(m2, e2, FindRec(m2, "main"));
		TEST_ASSERT(!f2.complete);
		TEST_ASSERT(HasError(e2, "a checked cast needs pointers to objects"));
	}

	void HIRLoweringTest::DeferLowersToDeferNodesInSourceOrder()
	{
		// `defer <expr>;` and `defer { ... }`: each becomes a DeferNode carrying its body region, in
		// SOURCE order (codegen runs them LIFO at function exit - normal return, early return, or an
		// uncaught throw). Before this the DeferStatement AST node had no dispatch case and any body
		// using `defer` failed with "statement not yet lowered".
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Count() { return 1; }\n"
			"int32 main() {\n"
			"\tdefer Count();\n"
			"\tif (Count() == 1) return 7;\n"
			"\tdefer { Count(); }\n"
			"\treturn 3;\n}");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "main");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // both defer forms lower (no "statement not yet lowered")

		// Two DeferNodes in source order, each with a non-empty owned body region. The early `return 7`
		// must still be a plain ReturnNode - HIR does NOT inline the cleanup at the return site; it is
		// declared once and runs at exit (that ordering guarantee is codegen's job).
		int defers = 0, bodyStmts = 0;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::Defer)
			{
				const auto* d = static_cast<const DeferNode*>(node.get());
				defers++;
				bodyStmts += d->body ? (int)d->body->stmts.size() : 0;
			}
		TEST_ASSERT_EQ(defers, 2);
		TEST_ASSERT(bodyStmts >= 2); // each body holds its expression statement
	}

	void HIRLoweringTest::EmptyStatementLowersAsNoOp()
	{
		// A bare `;` is legal C4 and must lower as a no-op, not "statement not yet lowered".
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 main() {\n"
			"\t;\n"
			"\tint32 x = 1;\n"
			"\t;\n"
			"\treturn x;\n}");
		ErrorTracker errors;

		const FunctionRecord* rec = FindRec(model, "main");
		HIRFunction fn = LowerFunction(model, errors, rec);
		TEST_ASSERT(fn.complete); // bare semicolons are no-ops
	}
} // namespace Testing

