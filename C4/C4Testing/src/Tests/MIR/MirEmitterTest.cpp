#include "MirEmitterTest.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/CpuTarget.h"
#include "Model/ProgramModel.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		constexpr const char* c_Program =
			"struct Res { public int32 id; [operator(\"delete\")] public void Delete() { id = 0; } }\n"
			"int32 Risky(int32 x) { if (x > 3) throw x; return x; }\n"
			"int32 Guarded(int32 x) noexcept { return Risky(x); }\n"
			"int32 Straight(int32 a, int32 b) { int32 c = a + b; int32 d = c * 2; return d - a; }\n"
			"int32 main() {\n"
			"  int32 total = 0;\n"
			"  try { Res r; r.id = 2; total = Risky(5) + r.id; }\n"
			"  catch (int32 e) { total = e; }\n"
			"  catch (...) { total = -1; }\n"
			"  return total + Guarded(1) + Straight(1, 2);\n"
			"}\n";

		// The program's module as text for `system`, or the lowering's errors.
		struct Emitted
		{
			std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
			std::string ir;
			std::string errors;
		};

		Emitted Emit(TargetSystem system)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", c_Program);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			ProgramModel& model = compiler.GetProgramModelForLowering();
			model.SetTarget(CpuTarget().On(system));
			Emitted out;
			ProgramBackend driver(*out.context, system);
			std::string error;
			const bool ok = driver.Lower(model, "main", &error);
			for (const CompilationError& e : driver.LoweringErrors()) out.errors += std::string(e.message.c_str()) + "\n";
			out.errors += error;
			if (!ok) return out;
			std::unique_ptr<llvm::Module> module = driver.TakeModule();
			llvm::raw_string_ostream text(out.ir);
			module->print(text, nullptr);
			return out;
		}

		// The text of the definition of the function whose name starts with `prefix`.
		std::string Definition(const std::string& ir, const std::string& prefix)
		{
			const size_t at = ir.find("define ");
			for (size_t start = at; start != std::string::npos; start = ir.find("define ", start + 1))
			{
				const size_t name = ir.find('@', start);
				if (ir.compare(name + 1, prefix.size(), prefix) != 0) continue;
				const size_t end = ir.find("\n}\n", start);
				return ir.substr(start, end == std::string::npos ? std::string::npos : end - start);
			}
			return "";
		}

		bool Has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }
	}

	void MirEmitterTest::WindowsUnwindsThroughFunclets()
	{
		const Emitted out = Emit(TargetSystem::Windows);
		TEST_ASSERT(out.errors.empty());
		const std::string main = Definition(out.ir, "main_");
		TEST_ASSERT(!main.empty());
		TEST_ASSERT(Has(main, "catchswitch"));
		TEST_ASSERT(Has(main, "catchpad"));
		TEST_ASSERT(Has(main, "catchret"));
		TEST_ASSERT(Has(main, "cleanuppad")); // `r`'s cleanup on the way out of the try
		TEST_ASSERT(Has(main, "cleanupret"));
		TEST_ASSERT(!Has(main, "landingpad"));
		TEST_ASSERT(Has(out.ir, "__CxxFrameHandler3"));
	}

	void MirEmitterTest::LinuxUnwindsThroughLandingPads()
	{
		const Emitted out = Emit(TargetSystem::Linux);
		TEST_ASSERT(out.errors.empty());
		const std::string main = Definition(out.ir, "main_");
		TEST_ASSERT(!main.empty());
		TEST_ASSERT(Has(main, "landingpad"));
		TEST_ASSERT(Has(main, "llvm.eh.typeid.for"));
		TEST_ASSERT(Has(main, "c4_eh_begin_catch"));
		TEST_ASSERT(Has(main, "c4_eh_end_catch"));
		TEST_ASSERT(!Has(main, "catchswitch") && !Has(main, "cleanuppad"));
		TEST_ASSERT(Has(out.ir, "__c4_personality_v0"));
	}

	void MirEmitterTest::ANoexceptBodyStopsTheProgramOnBoth()
	{
		const std::string windows = Definition(Emit(TargetSystem::Windows).ir, "Guarded");
		TEST_ASSERT(Has(windows, "catchswitch") && Has(windows, "c4_noexcept_violation"));
		const std::string linux = Definition(Emit(TargetSystem::Linux).ir, "Guarded");
		TEST_ASSERT(Has(linux, "landingpad") && Has(linux, "catch ptr null") && Has(linux, "c4_noexcept_violation"));
	}

	void MirEmitterTest::StraightLineCodeIsOneBlock()
	{
		// Its MIR blocks (the entry, the declarations' scopes) are each the last's only way on: one LLVM block.
		const std::string straight = Definition(Emit(TargetSystem::Windows).ir, "Straight");
		TEST_ASSERT(!straight.empty());
		TEST_ASSERT(!Has(straight, "\nbb"));
		TEST_ASSERT(!Has(straight, " br "));
	}
} // namespace Testing
