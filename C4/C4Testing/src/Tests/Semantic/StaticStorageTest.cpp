#include "StaticStorageTest.h"
#include "Backend/HIR/GlobalInit.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <algorithm>
#include <cstring>
#include <string>
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

	static int Count(const HIRFunction& fn, HIRNode::Kind kind)
	{
		int count = 0;
		for (const auto& node : fn.m_Nodes) count += node->kind == kind ? 1 : 0;
		return count;
	}

	// The creates `fn` calls, in the order lowering made the calls.
	static std::vector<const FunctionRecord*> CallsInOrder(const HIRFunction& fn)
	{
		std::vector<const FunctionRecord*> callees;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::Call) callees.push_back(static_cast<const CallNode*>(node)->callee);
		return callees;
	}

	static std::vector<std::string> CalleesOf(const llvm::Function& fn)
	{
		std::vector<std::string> names;
		for (const llvm::BasicBlock& block : fn)
			for (const llvm::Instruction& inst : block)
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst))
					if (const llvm::Function* callee = call->getCalledFunction()) names.push_back(callee->getName().str());
		return names;
	}

	static bool Contains(const std::vector<std::string>& names, const char* name)
	{
		return std::find(names.begin(), names.end(), name) != names.end();
	}

	static const llvm::Function* FunctionStartingWith(const llvm::Module& m, const char* prefix)
	{
		for (const llvm::Function& f : m)
			if (f.getName().starts_with(prefix) && !f.isDeclaration()) return &f;
		return nullptr;
	}

	static const char* kTracked =
		"class Tracked {\n"
		"\tpublic int32 id;\n"
		"\t[operator(\"create\")] public void Init() { id = 1; }\n"
		"\t[operator(\"delete\")] public void Delete() { id = 0; }\n"
		"}\n";

	void StaticStorageTest::ConstantStaticLocalNeedsNoGuard()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 F() { static int32 n = 10; n++; return n; }\nint32 main() { return F(); }\n");
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "F"));
		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ(Count(fn, HIRNode::Kind::Once), 0);
		bool constantStart = false;
		for (const auto& slot : fn.slots)
			constantStart = constantStart || (slot.cls == SlotClass::Static && slot.initial && slot.initial->value.intValue == 10);
		TEST_ASSERT(constantStart);

		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		bool startsAtTen = false;
		for (const llvm::GlobalVariable& g : driver.module()->globals())
			if (g.getName().starts_with("static.") && g.hasInitializer())
				if (const auto* value = llvm::dyn_cast<llvm::ConstantInt>(g.getInitializer())) startsAtTen = value->getSExtValue() == 10;
		TEST_ASSERT(startsAtTen);
		TEST_ASSERT(!Contains(CalleesOf(*FunctionStartingWith(*driver.module(), "F_")), "c4_once_begin"));
	}

	void StaticStorageTest::ObjectStaticLocalStartsOnceAndEndsAtExit()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, (std::string(kTracked) +
			"int32 F() { static Tracked t; return t.id; }\nint32 main() { return F(); }\n").c_str());
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "F"));
		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ(Count(fn, HIRNode::Kind::Once), 1);
		TEST_ASSERT_EQ(Count(fn, HIRNode::Kind::AtExit), 1);
		TEST_ASSERT_EQ(Count(fn, HIRNode::Kind::Defer), 1); // the reset an abandoned start runs, no scope-exit destroy

		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		const std::vector<std::string> callees = CalleesOf(*FunctionStartingWith(*driver.module(), "F_"));
		TEST_ASSERT(Contains(callees, "c4_once_begin") && Contains(callees, "c4_once_end") && Contains(callees, "c4_at_exit"));
		TEST_ASSERT(FunctionStartingWith(*driver.module(), "__c4_exit_") != nullptr);
	}

	void StaticStorageTest::AbandonedStartZeroesTheStorage()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, (std::string(kTracked) +
			"int32 F() { static Tracked t; return t.id; }\nint32 main() { return F(); }\n").c_str());
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "F"));
		const OnceNode* once = nullptr;
		for (const auto& node : fn.m_Nodes)
			if (node->kind == HIRNode::Kind::Once) once = static_cast<const OnceNode*>(node);
		TEST_ASSERT(once && !once->body->stmts.empty() && once->body->stmts[0]->kind == HIRNode::Kind::Defer);
		const auto* reset = static_cast<const DeferNode*>(once->body->stmts[0]);
		TEST_ASSERT(reset->exceptionalOnly);
		TEST_ASSERT(reset->body->stmts.size() == 1 && reset->body->stmts[0]->kind == HIRNode::Kind::Store);
		TEST_ASSERT(static_cast<const StoreNode*>(reset->body->stmts[0])->value->kind == HIRNode::Kind::Constant);
	}

	void StaticStorageTest::GlobalsStartInDeclarationOrder()
	{
		// `zebra` is declared first: order follows the source, not the names.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class Zebra { public int32 v; [operator(\"create\")] public void Init() { v = 1; } }\n"
			"class Apple { public int32 v; [operator(\"create\")] public void Init() { v = 2; }\n"
			"\t[operator(\"delete\")] public void Delete() { v = 0; } }\n"
			"Zebra zebra;\n"
			"Apple apple;\n"
			"int32 counted = zebra.v + 1;\n"
			"int32 constant = 4;\n"
			"int32 main() { return 0; }\n");
		ErrorTracker errors;
		HIRFunction init;
		StartedGlobals started;
		std::vector<ConstantStart> constants;
		TEST_ASSERT(LowerGlobalInit(model, errors, init, started, constants));
		TEST_ASSERT(errors.GetErrors().Empty());
		const std::vector<const FunctionRecord*> calls = CallsInOrder(init);
		TEST_ASSERT(calls.size() == 2 && calls[0] == Record(model, "Zebra_Init") && calls[1] == Record(model, "Apple_Init"));
		TEST_ASSERT_EQ(Count(init, HIRNode::Kind::AtExit), 1);
		TEST_ASSERT_EQ((int)init.root->stmts.size(), 3); // zebra, apple with its exit registration, counted
		TEST_ASSERT(constants.empty());

		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		const llvm::GlobalVariable* ctors = driver.module()->getGlobalVariable("llvm.global_ctors");
		TEST_ASSERT(ctors && driver.module()->getFunction("__c4_global_init") && !driver.module()->getFunction("__c4_global_init")->isDeclaration());
	}

	void StaticStorageTest::ConstantGlobalsNeedNoStartup()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 g_a = 4;\nconst float g_b = 1.5f;\nint32[3] g_c;\nint32 main() { return g_a; }\n");
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		TEST_ASSERT(driver.module()->getFunction("__c4_global_init") == nullptr);
		// The one static constructor registers reflection; nothing starts at run time.
		const llvm::GlobalVariable* ctors = driver.module()->getGlobalVariable("llvm.global_ctors");
		const auto* entries = ctors ? llvm::dyn_cast<llvm::ConstantArray>(ctors->getInitializer()) : nullptr;
		TEST_ASSERT(entries && entries->getNumOperands() == 1);
		TEST_ASSERT(llvm::cast<llvm::ConstantStruct>(entries->getOperand(0))->getOperand(1)->getName() == "__c4_reflect_init");
		const llvm::Function* threadStart = driver.module()->getFunction("__c4_thread_start");
		TEST_ASSERT(threadStart && !threadStart->isDeclaration() && CalleesOf(*threadStart).empty()); // the runtime always calls it
	}

	// A braced list of constants is the global's data, not code: numbers, null and string literals, in a struct or an
	// array, const or not; a start that calls anything stays code.
	void StaticStorageTest::ConstantTablesStartAsData()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Pair { public int16 a; public const char* name; }\n"
			"int32 Three() { return 3; }\n"
			"const int32[3] g_table = { 1, -2, 3 };\n"
			"Pair[2] g_pairs = { { 7, \"seven\" }, { -8, null } };\n"
			"int32[2] g_called = { Three(), 4 };\n"
			"int32 main() { return g_table[0] + g_pairs[0].a + g_called[1]; }\n");
		ErrorTracker errors;
		HIRFunction init;
		StartedGlobals started;
		std::vector<ConstantStart> constants;
		TEST_ASSERT(LowerGlobalInit(model, errors, init, started, constants));
		TEST_ASSERT(errors.GetErrors().Empty());
		TEST_ASSERT_EQ((int)constants.size(), 2);
		TEST_ASSERT_EQ((int)init.root->stmts.size(), 1); // g_called calls Three
		const ConstantData& table = constants[0].data;
		TEST_ASSERT(constants[0].variable->plainName == "g_table" && table.bytes.size() == 12 && table.strings.empty());
		TEST_ASSERT(table.bytes[0] == 1 && table.bytes[4] == 0xFE && table.bytes[7] == 0xFF && table.bytes[8] == 3);
		const ConstantData& pairs = constants[1].data;
		TEST_ASSERT(pairs.bytes.size() == 32 && pairs.bytes[0] == 7 && pairs.bytes[16] == 0xF8 && pairs.bytes[17] == 0xFF);
		TEST_ASSERT(pairs.strings.size() == 1 && pairs.strings.count(8) == 1 && pairs.strings.at(8).stringValue == "seven");

		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		const llvm::GlobalVariable* tableGlobal = driver.module()->getNamedGlobal("g_table");
		TEST_ASSERT(tableGlobal && tableGlobal->isConstant() && !tableGlobal->getInitializer()->isNullValue());
		const llvm::GlobalVariable* pairsGlobal = driver.module()->getNamedGlobal("g_pairs");
		TEST_ASSERT(pairsGlobal && !pairsGlobal->isConstant() && !pairsGlobal->getInitializer()->isNullValue());
		TEST_ASSERT(driver.module()->getFunction("__c4_global_init") != nullptr); // g_called still starts in code
	}

	void StaticStorageTest::ThreadLocalsStartInThreadRounds()
	{
		const char* source =
			"struct C { public int32 v; [operator(\"create\")] public void Init() { v = 1; } [operator(\"delete\")] public void End() { } }\n"
			"C g_shared;\n"
			"thread_local C t_own;\n"
			"int32 main() { return t_own.v + g_shared.v; }\n";
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, source);
		ErrorTracker errors;
		StartedGlobals started;
		HIRFunction process, thread;
		std::vector<ConstantStart> constants;
		TEST_ASSERT(LowerGlobalInit(model, errors, process, started, constants));
		TEST_ASSERT(LowerThreadInit(model, errors, thread, started, constants));
		TEST_ASSERT(errors.GetErrors().Empty());
		TEST_ASSERT_EQ((int)CallsInOrder(process).size(), 1);
		TEST_ASSERT_EQ((int)CallsInOrder(thread).size(), 1);
		const auto perThread = [](const HIRFunction& fn) {
			for (const auto& node : fn.m_Nodes)
				if (node->kind == HIRNode::Kind::AtExit) return static_cast<const AtExitNode*>(node)->perThread;
			return false;
		};
		TEST_ASSERT(!perThread(process) && perThread(thread));

		CompilerInstance again;
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(MakeModel(again, source), "main"));
		const llvm::Function* threadStart = driver.module()->getFunction("__c4_thread_start");
		const llvm::Function* init = driver.module()->getFunction("__c4_global_init");
		TEST_ASSERT(threadStart && init);
		TEST_ASSERT(CalleesOf(*threadStart) == std::vector<std::string>({ "__c4_thread_init_round0" }));
		const std::vector<std::string> initCalls = CalleesOf(*init);
		TEST_ASSERT(initCalls == std::vector<std::string>({ "__c4_global_init_round0", "c4_thread_attach" })); // the main thread attaches last
		TEST_ASSERT(Contains(CalleesOf(*driver.module()->getFunction("__c4_thread_init_round0")), "c4_thread_at_exit"));
	}

	void StaticStorageTest::ThreadLocalLocalStartsOnEachThread()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct C { public int32 v; [operator(\"create\")] public void Init() { v = 1; } [operator(\"delete\")] public void End() { } }\n"
			"int32 F() { thread_local C c; return c.v; }\n"
			"int32 main() { return F(); }\n");
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		const std::vector<std::string> callees = CalleesOf(*FunctionStartingWith(*driver.module(), "F_"));
		TEST_ASSERT(!Contains(callees, "c4_once_begin") && !Contains(callees, "c4_at_exit")); // no lock, no process exit
		TEST_ASSERT(Contains(callees, "c4_thread_at_exit") && Contains(callees, "c4_thread_once_reentered"));
		bool threadGuard = false;
		for (const llvm::GlobalVariable& g : driver.module()->globals())
			threadGuard = threadGuard || (g.getName().starts_with("thread.once.") && g.isThreadLocal());
		TEST_ASSERT(threadGuard);
	}

	// The global whose name contains `part` (namespace members are mangled).
	static const llvm::GlobalVariable* GlobalNamed(const llvm::Module& module, const char* part)
	{
		for (const llvm::GlobalVariable& g : module.globals())
			if (g.getName().contains(part)) return &g;
		return nullptr;
	}

	static bool LoadsFrom(const llvm::Function& fn, const llvm::GlobalVariable* global)
	{
		for (const llvm::BasicBlock& block : fn)
			for (const llvm::Instruction& inst : block)
				if (const auto* load = llvm::dyn_cast<llvm::LoadInst>(&inst))
					if (load->getPointerOperand() == global) return true;
		return false;
	}

	// BUG-190: reading `kCount` loaded it from a writable global, so nothing folded, even at -O2.
	void StaticStorageTest::FoldedConstantsAreReadAsValues()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"const int32 kCount = 4096;\n"
			"const float kHalf = 0.5f;\n"
			"namespace N { const int64 kBig = 5000000000; }\n"
			"int32 g_mutable = 3;\n"
			"int32 main() { const int32* p = &kCount; return kCount + (int32)(kHalf * 2.0f) + (int32)(N::kBig / 1000000000) + g_mutable + *p; }\n");
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		const llvm::Module& module = *driver.module();
		const llvm::GlobalVariable* count = GlobalNamed(module, "kCount");
		const llvm::GlobalVariable* half = GlobalNamed(module, "kHalf");
		const llvm::GlobalVariable* big = GlobalNamed(module, "kBig");
		const llvm::GlobalVariable* mutableGlobal = GlobalNamed(module, "g_mutable");
		TEST_ASSERT(count && half && big && mutableGlobal);
		TEST_ASSERT(count->isConstant() && half->isConstant() && big->isConstant());
		TEST_ASSERT(!mutableGlobal->isConstant());
		const llvm::Function* body = nullptr;
		for (const llvm::Function& fn : module)
			if (fn.getName().starts_with("main_")) body = &fn;
		TEST_ASSERT(body != nullptr);
		TEST_ASSERT(!LoadsFrom(*body, count) && !LoadsFrom(*body, half) && !LoadsFrom(*body, big));
		TEST_ASSERT(LoadsFrom(*body, mutableGlobal));
	}

	void StaticStorageTest::FailedGlobalInitializerIsAnError()
	{
		// The old global initialization skipped an initializer that did not lower, leaving the global zero.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 g_value = Missing();\nint32 main() { return g_value; }\n");
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(!driver.Lower(model, "main"));
		TEST_ASSERT(!driver.LoweringErrors().Empty());
	}

	void StaticStorageTest::InstanceStaticsStartInLaterRounds()
	{
		// BUG-115: `Counter<int16>` is first made while the global initialization is compiled (Touch<int16>'s
		// body), so its static starts in a second round, still before main.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Compute() { return 41; }\n"
			"template<T> struct Counter { static int32 start = Compute(); }\n"
			"template<T> int32 Touch() { return Counter<T>::start; }\n"
			"int32 g_touched = Touch<int16>();\n"
			"int32 main() { return Counter<int32>::start; }\n");
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TEST_ASSERT(driver.Lower(model, "main"));
		const llvm::Function* init = driver.module()->getFunction("__c4_global_init");
		TEST_ASSERT(init && !init->isDeclaration());
		TEST_ASSERT(driver.module()->getFunction("__c4_global_init_round0") && driver.module()->getFunction("__c4_global_init_round1"));
		int rounds = 0;
		for (const auto& block : *init)
			for (const auto& inst : block)
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst))
					rounds += call->getCalledFunction() && call->getCalledFunction()->getName().starts_with("__c4_global_init_round") ? 1 : 0;
		TEST_ASSERT_EQ(rounds, 2);
	}
} // namespace Testing
