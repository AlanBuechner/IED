#include "CodeGenTest.h"
#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Compiler/Toolchain.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/LLVMContext.h>

namespace Testing
{
	using namespace Scripting;

	// As C4.exe compiles a program: the source, then the default library it includes and the prelude.
	static ProgramModel& MakeModel(CompilerInstance& compiler, const char* source)
	{
		compiler.RegisterInMemory("t.cf", source);
		compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		return compiler.GetProgramModelForLowering();
	}

	// Lowers the whole model (verifying the module) and hands the module to `inspect` while it is alive.
	template <typename Inspect>
	static bool LowerAndInspect(CompilerInstance& compiler, const char* source, Inspect inspect, bool runTests = false)
	{
		auto& model = MakeModel(compiler, source);
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		TestHarnessOptions tests;
		if (!driver.Lower(model, "main", nullptr, runTests ? &tests : nullptr)) return false;
		inspect(*driver.module());
		return true;
	}

	static bool LowerAndVerify(CompilerInstance& compiler, const char* source)
	{
		return LowerAndInspect(compiler, source, [](llvm::Module&) {});
	}

	// The callees `fn` calls directly.
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

	// The globals __c4_gc_roots lists, by name, without the terminator.
	static std::vector<std::string> GcRootNames(llvm::Module& m)
	{
		std::vector<std::string> names;
		const llvm::GlobalVariable* table = m.getGlobalVariable("__c4_gc_roots");
		if (!table || !table->hasInitializer()) return names;
		const auto* entries = llvm::cast<llvm::ConstantArray>(table->getInitializer());
		for (unsigned i = 0; i + 1 < entries->getNumOperands(); ++i)
		{
			const auto* range = llvm::cast<llvm::ConstantStruct>(entries->getOperand(i));
			names.push_back(range->getOperand(0)->stripPointerCasts()->getName().str());
		}
		return names;
	}

	static bool AnyStartsWith(const std::vector<std::string>& names, const char* prefix)
	{
		for (const std::string& n : names)
			if (n.rfind(prefix, 0) == 0) return true;
		return false;
	}

	void CodeGenTest::StraightLineEmitsVerifiedModule()
	{
		// BARE LLVM SMOKE TEST (no C4 model): replicate exactly what CreateSlotAllocas does. Documents the
		// empty-function gotcha: Function::Create leaves the function with NO blocks, so getEntryBlock()
		// (front()) faults - an entry block must be created first.
		llvm::LLVMContext ctx;
		auto* m = new llvm::Module("smoke", ctx);
		auto* i32 = llvm::Type::getInt32Ty(ctx);
		auto* fty = llvm::FunctionType::get(i32, { i32, i32 }, false);
		auto* fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "smoke_fn", m);
		llvm::BasicBlock* entry = fn->empty() ? llvm::BasicBlock::Create(ctx, "entry", fn) : &fn->getEntryBlock();
		llvm::IRBuilder<> B(entry);
		auto* a = B.CreateAlloca(i32, nullptr, "slot");
		TEST_ASSERT(a != nullptr);

		CompilerInstance compiler;
		TEST_ASSERT(LowerAndVerify(compiler,
			"int32 Add(int32 a, int32 b) { return a + b; }\n"
			"void main() { int32 x = Add(1, 2); }"));
	}

	void CodeGenTest::ControlFlowEmitsVerifiedModule()
	{
		CompilerInstance compiler;
		TEST_ASSERT(LowerAndVerify(compiler,
			"int32 Sum(int32 n) {\n"
			"  int32 total = 0;\n"
			"  for (int32 i = 0; i < n; i++) { total = total + i; }\n"
			"  return total;\n"
			"}\n"
			"void main() { int32 s = Sum(5); }"));
	}

	void CodeGenTest::StructReturnUsesSretAndVerifies()
	{
		CompilerInstance compiler;
		TEST_ASSERT(LowerAndVerify(compiler,
			"struct Point { float x; float y; float z; };\n" // 12 bytes -> sret (too big for one register)
			"Point Make(float a, float b, float c) { Point p; p.x=a; p.y=b; p.z=c; return p; }\n"
			"void main() { Point p = Make(1.0f, 2.0f, 3.0f); }"));
	}

	void CodeGenTest::MethodCallOnObjectEmitsVerifiedModule()
	{
		CompilerInstance compiler;
		TEST_ASSERT(LowerAndVerify(compiler,
			"struct Vec {\n"
			"  float x;\n"
			"  float y;\n"
			"  float Length() { return this.x + this.y; }\n"
			"};\n"
			"void main() { Vec v; v.x = 1.0f; v.y = 2.0f; float l = v.Length(); }"));
	}

	void CodeGenTest::NestedFunctionsEmitAsSiblings()
	{
		CompilerInstance compiler;
		TEST_ASSERT(LowerAndVerify(compiler,
			"int32 Outer() {\n"
			"  int32 Inner() { return 41 + 1; }\n"
			"  return Inner();\n"
			"}\n"
			"void main() { int32 r = Outer(); }"));
	}

	void CodeGenTest::MixedWidthOpsEmitVerifiedModule()
	{
		// Mixed-width numeric operands (the kind Bedrock uses everywhere). The lowerer must convert both
		// sides to their common type so every binary op sees matching LLVM operand types. If any of these
		// emits a mismatched add/icmp, verify fails and this test localizes the gap.
		CompilerInstance compiler;
		TEST_ASSERT(LowerAndVerify(compiler,
			"void main() {\n"
			"  int8 a = 1;\n"
			"  int32 b = 2;\n"
			"  uint64 c = 3;\n"
			"  float f = 1.0f;\n"
			"  bool x = (a < b);\n"
			"  bool y = (b == c);\n"
			"  int32 s = a + b;\n"
			"  float d = f * b;\n"
			"}"));
	}

	void CodeGenTest::ModuleTargetsWindowsX64()
	{
		// BUG-097: the module carries the Windows x64 triple and clang's layout for it.
		CompilerInstance compiler;
		std::string triple, layout;
		TEST_ASSERT(LowerAndInspect(compiler, "int32 main() { return 0; }", [&](llvm::Module& m) {
			triple = m.getTargetTriple().str();
			layout = m.getDataLayoutStr();
		}));
		TEST_ASSERT(triple == "x86_64-pc-windows-msvc");
		TEST_ASSERT(layout == "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
	}

	void CodeGenTest::ATargetAnnotationWidensOneFunction()
	{
		CompilerInstance compiler;
		std::string wide, plain;
		TEST_ASSERT(LowerAndInspect(compiler,
			"[target(\"avx2\")] int32 Wide(int32 x) { return x * 3; }\n"
			"int32 Plain(int32 x) { return x + 1; }\n"
			"int32 main() { return Wide(1) + Plain(1); }", [&](llvm::Module& m) {
				for (const llvm::Function& f : m)
				{
					if (f.isDeclaration()) continue;
					const std::string features = f.getFnAttribute("target-features").getValueAsString().str();
					if (f.getName().contains("Wide")) wide = features;
					else if (f.getName().contains("Plain")) plain = features;
				}
			}));
		TEST_ASSERT(wide.find("+avx2") != std::string::npos && wide.find("+avx,") != std::string::npos);
		TEST_ASSERT(!plain.empty() && plain.find("+avx2") == std::string::npos);
		TEST_PASSED();
	}

	void CodeGenTest::HintsReachTheOptimizer()
	{
		CompilerInstance compiler;
		int expects = 0;
		int prefetches = 0;
		TEST_ASSERT(LowerAndInspect(compiler,
			"int32 Pick(int32 x, int32* p) { mem_prefetch(p); if (likely(x > 0)) return 1; if (unlikely(x < -5)) return 2; return 3; }\n"
			"int32 main() { int32 v = 0; return Pick(1, &v); }", [&](llvm::Module& m) {
				for (const llvm::Function& f : m)
				{
					if (f.getName().starts_with("llvm.expect")) expects += (int)f.getNumUses();
					if (f.getName().starts_with("llvm.prefetch")) prefetches += (int)f.getNumUses();
				}
			}));
		TEST_ASSERT_EQ(expects, 2);
		TEST_ASSERT_EQ(prefetches, 1);
		TEST_PASSED();
	}

	void CodeGenTest::PointerFreeObjectsAllocateNoscan()
	{
		// BUG-092: both allocators take (i64 size, i64 align), matching the runtime's size_t parameters.
		CompilerInstance compiler;
		std::vector<std::string> plain, linked;
		bool widths = false;
		TEST_ASSERT(LowerAndInspect(compiler,
			"struct Floats { float a; float[4] b; int64 c; }\n"
			"struct Linked { int32 v; Linked* next; }\n"
			"struct Wrapper { Floats inner; int32[2] extra; }\n"
			"struct WithRef { Linked*[2] links; }\n"
			"void Plain() { Floats* f = new Floats; Wrapper* w = new Wrapper; int32* i = new int32; }\n"
			"void Pointing() { Linked* l = new Linked; WithRef* r = new WithRef; }\n"
			"int32 main() { Plain(); Pointing(); return 0; }", [&](llvm::Module& m) {
				for (const llvm::Function& f : m)
				{
					if (f.getName().starts_with("Plain")) plain = CalleesOf(f);
					if (f.getName().starts_with("Pointing")) linked = CalleesOf(f);
				}
				widths = true;
				for (const char* name : { "c4_alloc_typed", "c4_alloc_typed_noscan" })
				{
					const llvm::Function* alloc = m.getFunction(name);
					widths = widths && alloc && alloc->arg_size() == 3 && alloc->getArg(0)->getType()->isIntegerTy(64) &&
						alloc->getArg(1)->getType()->isIntegerTy(64) && alloc->getArg(2)->getType()->isPointerTy();
				}
			}));
		TEST_ASSERT(widths);
		TEST_ASSERT(Contains(plain, "c4_alloc_typed_noscan") && !Contains(plain, "c4_alloc_typed"));
		TEST_ASSERT(Contains(linked, "c4_alloc_typed") && !Contains(linked, "c4_alloc_typed_noscan"));
	}

	void CodeGenTest::GcRootsListEveryPointerHoldingGlobal()
	{
		// BUG-008: statics, static members, static locals, arrays and structs holding pointers are roots;
		// pointer-free globals are not.
		CompilerInstance compiler;
		std::vector<std::string> roots;
		TEST_ASSERT(LowerAndInspect(compiler,
			"struct Node { int32 v; }\n"
			"struct Holder { int32 n; Node* item; }\n"
			"struct Registry { public static Node* s_item; }\n"
			"Node* g_plain;\n"
			"static Node* g_static;\n"
			"Node*[4] g_array;\n"
			"Holder g_holder;\n"
			"float g_number;\n"
			"int32[8] g_numbers;\n"
			"Node* Local() { static Node* s_local; return s_local; }\n"
			"int32 main() { Local(); return 0; }", [&](llvm::Module& m) { roots = GcRootNames(m); }));
		TEST_ASSERT_EQ((int)roots.size(), 6);
		TEST_ASSERT(AnyStartsWith(roots, "g_plain") && AnyStartsWith(roots, "g_static") && AnyStartsWith(roots, "g_array"));
		TEST_ASSERT(AnyStartsWith(roots, "g_holder") && AnyStartsWith(roots, "static."));
		TEST_ASSERT(!AnyStartsWith(roots, "g_number"));
	}

	void CodeGenTest::DeclarationsOfOneForeignSymbolShareAFunction()
	{
		// BUG-100: the prelude's and a program's [link] of the same C function, and two [link]s of one symbol, are one function.
		CompilerInstance compiler;
		bool renamed = true;
		unsigned declarations = 0;
		TEST_ASSERT(LowerAndInspect(compiler,
			"[link(\"c4_gc_collect\")] void Collect();\n"
			"[link(\"c4_gc_collect\")] void CollectAgain();\n"
			"int32 main() { gc_collect(); Collect(); CollectAgain(); return 0; }", [&](llvm::Module& m) {
				renamed = m.getFunction("c4_gc_collect.1") != nullptr;
				for (const llvm::Function& f : m)
					declarations += f.getName() == "c4_gc_collect" || f.getName().starts_with("c4_gc_collect.") ? 1u : 0u;
			}));
		TEST_ASSERT(!renamed);
		TEST_ASSERT_EQ((int)declarations, 1);
	}

	void CodeGenTest::TestHarnessModuleIsComplete()
	{
		// BUG-096: a -test build without a main still runs every step after the entry point (roots, verify).
		CompilerInstance compiler;
		bool hasRoots = false, hasMain = false;
		TEST_ASSERT(LowerAndInspect(compiler,
			"int32* g_value;\n"
			"[test(\"harness\")] void ChecksValue() { g_value = new int32; }\n", [&](llvm::Module& m) {
				hasRoots = m.getGlobalVariable("__c4_gc_roots") != nullptr;
				hasMain = m.getFunction("main") != nullptr;
			}, true));
		TEST_ASSERT(hasRoots && hasMain);
	}
} // namespace Testing
