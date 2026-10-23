#include "LifecycleTest.h"
#include "Model/TypeHandle.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>

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

	static const ObjectType* Object(const ProgramModel& model, const char* name)
	{
		auto t = model.QueryType(name);
		if (!t.Ok()) return nullptr;
		return dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(t.value));
	}

	static const FunctionRecord* Record(ProgramModel& model, const char* mangledPrefix)
	{
		Core::Vector<const FunctionRecord*> recs;
		model.AllFunctionRecords(recs);
		const size_t n = strlen(mangledPrefix);
		for (const auto* r : recs)
			if (r->mangledName == mangledPrefix || (r->mangledName.size() > n && r->mangledName.compare(0, n, mangledPrefix) == 0 && r->mangledName[n] == '_'))
				return r;
		return nullptr;
	}

	static int CallsTo(const HIRFunction& fn, const FunctionRecord* callee)
	{
		int calls = 0;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::Call && static_cast<const CallNode*>(node)->callee == callee) ++calls;
		return calls;
	}

	static const char* kResource =
		"class Res {\n"
		"\tpublic int32 id;\n"
		"\t[operator(\"create\")] public void Init(int32 i) { id = i; }\n"
		"\t[operator(\"copy\")] public void CopyFrom(const Res& other) { id = other.id; }\n"
		"\t[operator(\"delete\")] public void Delete() { id = 0; }\n"
		"}\n"
		"Res Make(int32 i) { return Res(i); }\n";

	// --- facts --------------------------------------------------------------------------

	void LifecycleTest::PlainTypesAreTrivial()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Pod { int32 a; float b; int32[4] c; }\n"
			"class Lanes { public union { packed float x, y, z; }; }\n");
		ModelQueries q(&model);
		for (const char* name : { "Pod", "Lanes" })
		{
			const ObjectType* t = Object(model, name);
			TEST_ASSERT(t != nullptr);
			TEST_ASSERT(!LifecycleFacts::NeedsConstruct(q, t));
			TEST_ASSERT(!LifecycleFacts::NeedsDestroy(q, t));
			TEST_ASSERT(!LifecycleFacts::NeedsCopy(q, t));
			TEST_ASSERT(LifecycleFacts::CheckCopyable(q, t).Ok());
		}
	}

	void LifecycleTest::NeedsComeFromMembersAndBases()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class Made { public int32 n; [operator(\"create\")] public void Init() { n = 1; } }\n"
			"class Freed { [operator(\"delete\")] public void Delete() { } }\n"
			"class Copied { [operator(\"copy\")] public void CopyFrom(const Copied& o) { } }\n"
			"class HasFreed { public int32 n; public Freed f; }\n"
			"class FromCopied : Copied { }\n"
			"class ArrayOfMade { public Made[3] all; }\n");
		ModelQueries q(&model);
		const ObjectType* hasFreed = Object(model, "HasFreed");
		const ObjectType* fromCopied = Object(model, "FromCopied");
		const ObjectType* arrayOfMade = Object(model, "ArrayOfMade");
		TEST_ASSERT(hasFreed && fromCopied && arrayOfMade);
		TEST_ASSERT(LifecycleFacts::NeedsDestroy(q, hasFreed));
		TEST_ASSERT(!LifecycleFacts::NeedsConstruct(q, hasFreed));
		TEST_ASSERT(LifecycleFacts::NeedsCopy(q, fromCopied));
		TEST_ASSERT(!LifecycleFacts::NeedsDestroy(q, fromCopied));
		TEST_ASSERT(LifecycleFacts::NeedsConstruct(q, arrayOfMade));
		TEST_ASSERT(LifecycleFacts::ClassBase(fromCopied) == Object(model, "Copied"));
	}

	// `Init() {}` adds nothing to the zeroing every object gets, so nothing calls it; a pure packed vector's
	// create runs like any class's (BUG-188) though it copies as a register value.
	void LifecycleTest::EmptyCreatesRunNothing()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class Empty { [operator(\"create\")] public void Init() { } }\n"
			"class EmptyWithField { public int32 n = 2; [operator(\"create\")] public void Init() { } }\n"
			"class Defaulted { [operator(\"create\")] public void Init(int32 x = 3) { } }\n"
			"class HoldsEmpty { public Empty[4] all; }\n"
			"class Lanes { public union { packed float x, y; packed float[2] data; }; [operator(\"create\")] public void Init() { data = { 1.0f, 2.0f }; } }\n");
		ModelQueries q(&model);
		const ObjectType* empty = Object(model, "Empty");
		const ObjectType* withField = Object(model, "EmptyWithField");
		const ObjectType* defaulted = Object(model, "Defaulted");
		const ObjectType* holds = Object(model, "HoldsEmpty");
		const ObjectType* lanes = Object(model, "Lanes");
		TEST_ASSERT(empty && withField && defaulted && holds && lanes);
		TEST_ASSERT(LifecycleFacts::Own(q, empty).defaultCreate != nullptr);
		TEST_ASSERT(!LifecycleFacts::NeedsConstruct(q, empty));
		TEST_ASSERT(!LifecycleFacts::NeedsConstruct(q, holds));
		TEST_ASSERT(LifecycleFacts::NeedsConstruct(q, withField));
		TEST_ASSERT(LifecycleFacts::NeedsConstruct(q, defaulted));
		TEST_ASSERT(lanes->IsPurePackedVector());
		TEST_ASSERT(LifecycleFacts::NeedsConstruct(q, lanes));
		TEST_ASSERT(!LifecycleFacts::NeedsCopy(q, lanes));
		TEST_ASSERT(!LifecycleFacts::NeedsDestroy(q, lanes));
	}

	void LifecycleTest::OwnMethodsAreFound()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class T {\n"
			"\t[operator(\"create\")] public void Init() { }\n"
			"\t[operator(\"create\")] public void Init(int32 x) { }\n"
			"\t[operator(\"=\")] public void Assign(const T& o) { }\n"
			"\t[operator(\"delete\")] public void Delete() { }\n"
			"}\n"
			"class E { public int32 v; }\n"
			"[operator(\"copy\")] void CopyE(const E& o) extends E { v = o.v; }\n");
		ModelQueries q(&model);
		const ObjectType* t = Object(model, "T");
		TEST_ASSERT(t != nullptr);
		LifecycleFacts::OwnMethods own = LifecycleFacts::Own(q, t);
		TEST_ASSERT_EQ((int)own.creates.size(), 2);
		TEST_ASSERT(own.defaultCreate != nullptr && own.defaultCreate->type->GetParameterCount() == 0);
		TEST_ASSERT(own.destroy != nullptr);
		TEST_ASSERT(own.assign != nullptr);
		TEST_ASSERT(own.copy == nullptr);
		const ObjectType* e = Object(model, "E");
		TEST_ASSERT(e != nullptr);
		TEST_ASSERT(LifecycleFacts::Own(q, e).copy != nullptr); // an extension method counts as the type's own
	}

	void LifecycleTest::CopyabilityNamesTheFix()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class Owner { [operator(\"delete\")] public void Delete() { } }\n"
			"class Shared { [operator(\"delete\")] public void Delete() { } [operator(\"copy\")] public void CopyFrom(const Shared& o) { } }\n"
			"class HoldsOwner { public Owner o; }\n"
			"class HoldsOwners { public Owner[2] o; }\n");
		ModelQueries q(&model);
		auto owner = LifecycleFacts::CheckCopyable(q, Object(model, "Owner"));
		TEST_ASSERT(!owner.Ok());
		TEST_ASSERT(owner.message.find("add a copy operator, or move the value with take(x)") != std::string::npos);
		TEST_ASSERT(LifecycleFacts::CheckCopyable(q, Object(model, "Shared")).Ok());
		TEST_ASSERT(!LifecycleFacts::CheckCopyable(q, Object(model, "HoldsOwner")).Ok());
		TEST_ASSERT(!LifecycleFacts::CheckCopyable(q, Object(model, "HoldsOwners")).Ok());
	}

	// --- compiler-written functions -----------------------------------------------------------

	void LifecycleTest::SynthesizedFunctionsAreSharedPerType()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, (std::string(kResource) + "class Pair { public Res a; public Res b; }\n").c_str());
		const ObjectType* pair = Object(model, "Pair");
		TEST_ASSERT(pair != nullptr);
		const FunctionRecord* copy = model.EnsureSynthesizedLifecycle(pair, LifecycleOp::Copy);
		TEST_ASSERT(copy != nullptr);
		TEST_ASSERT(model.EnsureSynthesizedLifecycle(pair, LifecycleOp::Copy) == copy);
		TEST_ASSERT(model.SynthesizedLifecycle(pair, LifecycleOp::Copy) == copy);
		TEST_ASSERT(copy->synthesized == LifecycleOp::Copy);
		TEST_ASSERT(copy->containingType == pair);
		TEST_ASSERT(copy->mangledName.compare(0, 10, "__c4_copy_") == 0);
		TEST_ASSERT(copy->type && copy->type->GetParameterCount() == 1);
		TEST_ASSERT(TypeHandle::UnwrapToValue(copy->type->GetParameters()[0].m_Type) == pair);
		const FunctionRecord* destroy = model.EnsureSynthesizedLifecycle(pair, LifecycleOp::Delete);
		TEST_ASSERT(destroy != nullptr && destroy != copy && destroy->type->GetParameterCount() == 0);
		TEST_ASSERT(HasBody(*destroy));
	}

	// --- lowering --------------------------------------------------------------------------------

	void LifecycleTest::CopyInitializationCallsTheCopyOperator()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, (std::string(kResource) + "int32 F(Res a) { Res b = a; Res c = a; return b.id + c.id; }\n").c_str());
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "F"));
		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ(CallsTo(fn, Record(model, "Res_CopyFrom")), 2);
		TEST_ASSERT(CallsTo(fn, Record(model, "Res_Delete")) >= 3); // b, c and the by-value parameter
	}

	void LifecycleTest::FreshValuesRelocateWithoutCopying()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, (std::string(kResource) +
			"int32 Use(Res r) { return r.id; }\n"
			"Res Pass(Res r) { return r; }\n"
			"int32 F() { Res a = Make(1); Res b = Res(2); Res c = take(a); return Use(Make(3)) + Pass(take(b)).id + c.id; }\n").c_str());
		ErrorTracker errors;
		const FunctionRecord* copy = Record(model, "Res_CopyFrom");
		for (const char* name : { "F", "Pass", "Make" })
		{
			HIRFunction fn = LowerFunction(model, errors, Record(model, name));
			TEST_ASSERT(fn.complete);
			TEST_ASSERT_EQ(CallsTo(fn, copy), 0);
		}
	}

	void LifecycleTest::DeclarationCleanupOutlivesItsTemporaries()
	{
		// `Res v = Id(Make(1));`: the temporary bound to `const Res&` lives in the statement's own block; v's
		// destruction is registered after that block, in the function body.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, (std::string(kResource) +
			"Res Id(const Res& r) { return r; }\n"
			"int32 F() { Res v = Id(Make(1)); return v.id; }\n").c_str());
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "F"));
		TEST_ASSERT(fn.complete);
		const auto& stmts = fn.root->stmts;
		size_t statementBlock = stmts.size();
		for (size_t i = 0; i < stmts.size(); ++i)
			if (stmts[i]->kind == HIRNode::Kind::Block) { statementBlock = i; break; }
		TEST_ASSERT(statementBlock < stmts.size());
		const auto* block = static_cast<const BlockNode*>(stmts[statementBlock]);
		bool temporaryCleanup = false;
		for (const HIRNode* s : block->stmts) temporaryCleanup = temporaryCleanup || s->kind == HIRNode::Kind::Defer;
		TEST_ASSERT(temporaryCleanup);
		TEST_ASSERT(statementBlock + 1 < stmts.size() && stmts[statementBlock + 1]->kind == HIRNode::Kind::Defer);
	}

	void LifecycleTest::ConditionTemporariesEndBeforeTheBranch()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, (std::string(kResource) +
			"int32 F() { if (Make(1).id == 1) { return 1; } return 0; }\n").c_str());
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "F"));
		TEST_ASSERT(fn.complete);
		const IfNode* branch = nullptr;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::If) branch = static_cast<const IfNode*>(node);
		TEST_ASSERT(branch != nullptr && branch->cond && branch->cond->kind == HIRNode::Kind::Sequence);
		// The condition is a block of its own that destroys the temporary as it ends, on every path; nothing else does.
		const auto* seq = static_cast<const SequenceNode*>(branch->cond);
		TEST_ASSERT(!seq->steps.empty() && seq->steps.front()->kind == HIRNode::Kind::Block);
		bool destroyedInCondition = false;
		for (const HIRNode* s : static_cast<const BlockNode*>(seq->steps.front())->stmts)
			destroyedInCondition = destroyedInCondition || s->kind == HIRNode::Kind::Defer;
		TEST_ASSERT(destroyedInCondition);
		size_t destroys = 0;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::Call && static_cast<const CallNode*>(node)->callee == Record(model, "Res_Delete")) ++destroys;
		TEST_ASSERT_EQ(destroys, size_t(1));
	}

	void LifecycleTest::NoexceptCalleesAreNotInvoked()
	{
		// A noexcept callee never unwinds into its caller, nor does one that cannot (UnwindInference): the caller calls
		// them without an unwind edge.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class Res { public int32 id; [operator(\"delete\")] public void Delete() { id = 0; } }\n"
			"int32 Quiet() noexcept { return 1; }\n"
			"int32 Plain() { return 3; }\n"
			"int32 Loud(int32 v) { if (v > 1) throw v; return v; }\n"
			"int32 main() { Res r; return Quiet() + Plain() + Loud(2); }\n");
		TEST_ASSERT(Record(model, "Quiet")->isNoexcept);
		TEST_ASSERT(!Record(model, "Loud")->isNoexcept);
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		std::string error;
		TEST_ASSERT(driver.Lower(model, "main", &error));
		bool quietInvoked = false, plainInvoked = false, loudInvoked = false;
		for (llvm::Function& f : *driver.module())
			for (llvm::BasicBlock& bb : f)
				for (llvm::Instruction& inst : bb)
					if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&inst))
					{
						if (!invoke->getCalledFunction()) continue;
						llvm::StringRef callee = invoke->getCalledFunction()->getName();
						quietInvoked = quietInvoked || callee.starts_with("Quiet");
						plainInvoked = plainInvoked || callee.starts_with("Plain");
						loudInvoked = loudInvoked || callee.starts_with("Loud");
					}
		TEST_ASSERT(!quietInvoked && !plainInvoked);
		TEST_ASSERT(loudInvoked); // r's destructor must run if Loud throws
	}
} // namespace Testing
