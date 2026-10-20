#include "SimdIntrinsicsTest.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/CpuTarget.h"
#include "Compiler/Toolchain.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// What lowering a program gives: its module as text, or the lowering's errors.
		struct Lowered
		{
			bool ok = false;
			std::string ir;
			std::string errors;
			std::string calledFromMain; // the names main calls, one per line
		};

		std::string CalleesOf(const llvm::Function* fn)
		{
			std::string names;
			if (!fn) return names;
			for (const llvm::BasicBlock& block : *fn)
				for (const llvm::Instruction& inst : block)
					if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst))
						if (const llvm::Function* callee = call->getCalledFunction()) names += callee->getName().str() + "\n";
			return names;
		}

		Lowered Lower(const char* source, const CpuTarget& target = CpuTarget())
		{
			CompilerInstance compiler;
			compiler.SetTarget(target);
			compiler.RegisterInMemory("t.cf", source);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			Lowered out;
			std::string error;
			out.ok = driver.Lower(compiler.GetProgramModelForLowering(), "main", &error);
			for (const CompilationError& e : driver.LoweringErrors()) out.errors += std::string(e.message.c_str()) + "\n";
			out.errors += error;
			if (!out.ok) return out;
			llvm::raw_string_ostream text(out.ir);
			driver.module()->print(text, nullptr);
			out.calledFromMain = CalleesOf(driver.module()->getFunction("main_0")); // the program's main (`main` is the C entry)
			return out;
		}

		bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

		size_t Count(const std::string& text, const char* part)
		{
			size_t count = 0;
			for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++count;
			return count;
		}

		CpuTarget Target(const std::vector<std::string>& flags)
		{
			std::string error;
			return CpuTarget::FromFlags(flags, error).value_or(CpuTarget());
		}
	}

	void SimdIntrinsicsTest::LaneMathLowersToLlvmIntrinsics()
	{
		const Lowered lowered = Lower(R"(
			int32 main() {
				packed int32[4] i = { -1, 2, -3, 4 };
				packed uint32[4] u = { 1u, 2u, 3u, 4u };
				packed float[8] f = 1.5f;
				packed int32[4] a = simd_min(i, 0) + simd_max(i, 1) + simd_abs(i);
				packed uint32[4] b = simd_min(u, 2u);
				packed float[8] c = simd_sqrt(f) + simd_floor(f) + simd_ceil(f) + simd_round(f) + simd_trunc(f) + simd_abs(f) + simd_muladd(f, f, f);
				return a[0] + (int32)b[0] + (int32)c[0];
			})");
		TEST_ASSERT(lowered.ok);
		for (const char* name : { "@llvm.smin.v4i32", "@llvm.smax.v4i32", "@llvm.abs.v4i32", "@llvm.umin.v4i32", "@llvm.sqrt.v8f32",
			"@llvm.floor.v8f32", "@llvm.ceil.v8f32", "@llvm.round.v8f32", "@llvm.trunc.v8f32", "@llvm.fabs.v8f32", "@llvm.fmuladd.v8f32" })
			TEST_ASSERT(Has(lowered.ir, name));
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::FloatMinAndMaxCompareAndSelect()
	{
		const Lowered lowered = Lower(R"(
			int32 main() {
				packed float[4] a = 1.0f;
				packed float[4] b = 2.0f;
				packed float[4] c = simd_min(a, b) + simd_max(a, b);
				return (int32)c[0];
			})");
		TEST_ASSERT(lowered.ok);
		TEST_ASSERT(Has(lowered.ir, "fcmp olt <4 x float>") && Has(lowered.ir, "fcmp ogt <4 x float>"));
		TEST_ASSERT(!Has(lowered.ir, "@llvm.minnum") && !Has(lowered.ir, "@llvm.maxnum"));   // the SSE rule, not IEEE minNum
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::FloatReductionsAreAHalvingTree()
	{
		const Lowered lowered = Lower(R"(
			int32 main() {
				packed float[8] v = 1.0f;
				packed float[3] w = 2.0f;
				return (int32)(simd_reduce_add(v) + simd_reduce_mul(w) + simd_reduce_max(v));
			})");
		TEST_ASSERT(lowered.ok);
		TEST_ASSERT(!Has(lowered.ir, "@llvm.vector.reduce.f"));                          // no order left to the optimizer
		TEST_ASSERT(Has(lowered.ir, "shufflevector <8 x float> %"));                      // the upper half onto the lower
		TEST_ASSERT(Has(lowered.ir, "<3 x float> splat (float 1.000000e+00), <4 x i32> <i32 0, i32 1, i32 2, i32 3>"));   // 3 lanes padded with 1 for a product
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::IntegerReductionsUseLlvmReductions()
	{
		const Lowered lowered = Lower(R"(
			int32 main() {
				packed int32[4] i = { 1, 2, 3, 4 };
				packed uint16[8] u = 7;
				return simd_reduce_add(i) + simd_reduce_mul(i) + simd_reduce_min(i) + simd_reduce_and(i) + simd_reduce_or(i) + simd_reduce_xor(i) +
					(int32)simd_reduce_max(u);
			})");
		TEST_ASSERT(lowered.ok);
		for (const char* name : { "@llvm.vector.reduce.add.v4i32", "@llvm.vector.reduce.mul.v4i32", "@llvm.vector.reduce.smin.v4i32",
			"@llvm.vector.reduce.and.v4i32", "@llvm.vector.reduce.or.v4i32", "@llvm.vector.reduce.xor.v4i32", "@llvm.vector.reduce.umax.v8i16" })
			TEST_ASSERT(Has(lowered.ir, name));
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::ShuffleIndicesAreTheShuffleMask()
	{
		const Lowered lowered = Lower(R"(
			int32 main() {
				packed float[4] a = 1.0f;
				packed float[4] b = 2.0f;
				packed float[8] c = simd_shuffle(a, b, 7, 0, 6, 1, 5, 2, 4, 3);
				packed float[2] d = simd_shuffle(a, 3, 3);
				packed float[8] e = simd_concat(a, b);
				return (int32)(c[0] + d[0] + e[0]);
			})");
		TEST_ASSERT(lowered.ok);
		TEST_ASSERT(Has(lowered.ir, "<8 x i32> <i32 7, i32 0, i32 6, i32 1, i32 5, i32 2, i32 4, i32 3>"));
		TEST_ASSERT(Has(lowered.ir, "<2 x i32> <i32 3, i32 3>"));
		TEST_ASSERT(Has(lowered.ir, "<8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>"));
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::MemoryIntrinsicsUseMaskedAccesses()
	{
		const Lowered lowered = Lower(R"(
			int32 main() {
				float[16] values;
				packed int32[8] at = 3;
				packed bool[8] on = at > 2;
				packed float[8] a = simd_load(&values[1], 8);
				packed float[8] b = simd_load_masked(values, on);
				packed float[8] c = simd_gather(values, at);
				packed float[8] d = simd_gather_masked(values, at, on);
				simd_store(values, a + b);
				simd_store_masked(&values[8], c + d, on);
				return 0;
			})");
		TEST_ASSERT(lowered.ok);
		TEST_ASSERT(Has(lowered.ir, "load <8 x float>, ptr ") && Has(lowered.ir, "align 4"));   // element alignment only
		TEST_ASSERT(Has(lowered.ir, "@llvm.masked.load.v8f32") && Has(lowered.ir, "@llvm.masked.store.v8f32"));
		TEST_ASSERT(Count(lowered.ir, "call <8 x float> @llvm.masked.gather.v8f32") == 2);
		TEST_ASSERT(Has(lowered.ir, "sext <8 x i32>"));   // signed indices widen by their sign
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::FmaNeedsTheFmaFeature()
	{
		const char* source = R"(
			int32 main() {
				packed float[4] a = 2.0f;
				return (int32)simd_fma(a, a, a)[0];
			})";
		const Lowered baseline = Lower(source);
		TEST_ASSERT(!baseline.ok);
		TEST_ASSERT(Has(baseline.errors, "simd_fma needs the processor's fused multiply-add"));
		const Lowered withFma = Lower(source, Target({ "-mfma" }));
		TEST_ASSERT(withFma.ok);
		TEST_ASSERT(Has(withFma.ir, "@llvm.fma.v4f32"));
		TEST_ASSERT(Lower(source, Target({ "-march=x86-64-v3" })).ok);
		TEST_ASSERT(!Lower(source, Target({ "-march=x86-64-v3", "-mno-fma" })).ok);
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::EveryDefinitionCarriesTheTarget()
	{
		const char* source = R"(
			int32 Helper(int32 x) { return x * 2; }
			int32 main() { return Helper(2) - 4; })";
		const Lowered v3 = Lower(source, Target({ "-march=x86-64-v3" }));
		TEST_ASSERT(v3.ok);
		TEST_ASSERT(Has(v3.ir, "\"target-cpu\"=\"x86-64-v3\""));
		TEST_ASSERT(Has(v3.ir, "+avx2") && Has(v3.ir, "+fma"));
		TEST_ASSERT(!Has(v3.ir, "\"target-cpu\"=\"x86-64\""));
		const Lowered baseline = Lower(source);
		TEST_ASSERT(Has(baseline.ir, "\"target-cpu\"=\"x86-64\"") && !Has(baseline.ir, "+avx"));
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::PackedCastsConvertLaneByLane()
	{
		const Lowered lowered = Lower(R"(
			int32 main() {
				packed float[4] f = 1.5f;
				packed int32[4] i = (packed int32[4])f;
				packed uint8[4] u = (packed uint8[4])i;
				packed float[4] back = (packed float[4])u;
				packed int32[4] bits = reinterpret_cast<packed int32[4]>(f);
				packed float[4] fromMask = (packed float[4])(f > 1.0f);
				return i[0] + bits[0] + (int32)back[0] + (int32)fromMask[0];
			})");
		TEST_ASSERT(lowered.ok);
		TEST_ASSERT(Has(lowered.ir, "fptosi <4 x float>") && Has(lowered.ir, "to <4 x i32>"));
		TEST_ASSERT(Has(lowered.ir, "trunc <4 x i32>") && Has(lowered.ir, "uitofp <4 x i8>"));
		TEST_ASSERT(Has(lowered.ir, "bitcast <4 x float>"));
		TEST_ASSERT(Has(lowered.ir, "uitofp <4 x i1>") && !Has(lowered.ir, "sitofp <4 x i1>"));   // BUG-253: true is 1
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::ABroadcastEvaluatesItsScalarOnce()
	{
		const Lowered lowered = Lower(R"(
			int32 g_calls = 0;
			float Next() { g_calls++; return 2.0f; }
			int32 main() {
				packed float[8] v = Next();
				return (int32)v[7];
			})");
		TEST_ASSERT(lowered.ok);
		TEST_ASSERT(Count(lowered.calledFromMain, "Next") == 1);
		TEST_ASSERT(Has(lowered.ir, "insertelement <8 x float>") && Has(lowered.ir, "shufflevector <8 x float>"));
		TEST_PASSED();
	}

	void SimdIntrinsicsTest::AProgramFunctionWinsOverAnIntrinsic()
	{
		const Lowered lowered = Lower(R"(
			int32 simd_min(int32 a, int32 b) { return a < b ? a : b; }
			int32 main() { return simd_min(3, 4) - 3; })");
		TEST_ASSERT(lowered.ok);
		TEST_ASSERT(Has(lowered.calledFromMain, "simd_min"));
		TEST_PASSED();
	}
} // namespace Testing
