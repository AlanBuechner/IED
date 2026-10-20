#include "ModuleOwnershipTest.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Compiler/CompilerInstance.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ValueHandle.h>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kProgram = "int32 Seven() { return 7; }\nint32 main() { return Seven() - 7; }\n";

		// Lowers kProgram into `context` and hands the driver to `use` while it lives.
		template <typename Use>
		void Lowered(llvm::LLVMContext& context, Use use)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", kProgram);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			CodeGenDriver driver(context);
			if (driver.Lower(compiler.GetProgramModelForLowering(), "main", nullptr)) use(driver);
		}
	}

	// BUG-240: the module stayed until its LLVMContext ended.
	void ModuleOwnershipTest::TheModuleGoesWithItsDriver()
	{
		llvm::LLVMContext context;
		llvm::WeakVH seven;
		Lowered(context, [&](CodeGenDriver& driver) { seven = driver.module()->getFunction("Seven_0"); });
		TEST_ASSERT(seven == nullptr); // the context lives on; the driver took its module with it
		TEST_PASSED();
	}

	void ModuleOwnershipTest::ATakenModuleOutlivesItsDriver()
	{
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> taken;
		Lowered(context, [&](CodeGenDriver& driver) { taken = driver.TakeModule(); });
		TEST_ASSERT(taken != nullptr);
		TEST_ASSERT(taken->getFunction("Seven_0") != nullptr && !taken->getFunction("Seven_0")->isDeclaration());
		TEST_PASSED();
	}
} // namespace Testing
