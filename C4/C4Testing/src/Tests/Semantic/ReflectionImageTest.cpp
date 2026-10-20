#include "ReflectionImageTest.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Compiler/CompilerInstance.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <algorithm>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// The image's fields (C4Runtime/ReflectABI.h c4_reflect_image).
		enum ImageField { kTypeCount = 0, kTypes = 1, kVariableCount = 2, kVariables = 3, kAddresses = 4, kFunctionCount = 5, kFunctions = 6 };
		enum FunctionField { kFunctionSymbol = 1 };

		// Lowers `source` with `entry` and hands the module (or null when lowering failed) and the driver to `inspect`.
		template <typename Inspect>
		void Lowered(const char* source, const char* entry, Inspect inspect)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			const bool ok = driver.Lower(compiler.GetProgramModelForLowering(), entry, nullptr);
			inspect(ok ? driver.module() : nullptr, driver);
		}

		const llvm::ConstantStruct* Image(const llvm::Module& module)
		{
			const llvm::GlobalVariable* image = module.getNamedGlobal("__c4_reflect_image");
			return image && image->hasInitializer() ? llvm::dyn_cast<llvm::ConstantStruct>(image->getInitializer()) : nullptr;
		}

		uint64_t Count(const llvm::ConstantStruct& image, ImageField field)
		{
			return llvm::cast<llvm::ConstantInt>(image.getOperand(field))->getZExtValue();
		}

		// The rows an image field points at.
		const llvm::ConstantArray* Rows(const llvm::ConstantStruct& image, ImageField field)
		{
			const auto* rows = llvm::dyn_cast<llvm::GlobalVariable>(image.getOperand(field));
			return rows ? llvm::dyn_cast<llvm::ConstantArray>(rows->getInitializer()) : nullptr;
		}

		std::string Text(const llvm::Constant* pointer)
		{
			const auto* global = llvm::dyn_cast<llvm::GlobalVariable>(pointer);
			const auto* data = global ? llvm::dyn_cast<llvm::ConstantDataArray>(global->getInitializer()) : nullptr;
			return data ? data->getAsCString().str() : std::string();
		}

		std::vector<std::string> FunctionSymbols(const llvm::ConstantStruct& image)
		{
			std::vector<std::string> symbols;
			if (const llvm::ConstantArray* rows = Rows(image, kFunctions))
				for (const llvm::Use& row : rows->operands())
					symbols.push_back(Text(llvm::cast<llvm::Constant>(llvm::cast<llvm::ConstantStruct>(row)->getOperand(kFunctionSymbol))));
			return symbols;
		}

		// The priority of `function` in @llvm.global_ctors, or -1.
		int64_t CtorPriority(const llvm::Module& module, const char* function)
		{
			const llvm::GlobalVariable* ctors = module.getNamedGlobal("llvm.global_ctors");
			const auto* entries = ctors ? llvm::dyn_cast<llvm::ConstantArray>(ctors->getInitializer()) : nullptr;
			if (!entries) return -1;
			for (const llvm::Use& entry : entries->operands())
			{
				const auto* row = llvm::cast<llvm::ConstantStruct>(entry);
				if (row->getOperand(1)->getName() == function) return llvm::cast<llvm::ConstantInt>(row->getOperand(0))->getSExtValue();
			}
			return -1;
		}

		const char* kProgram =
			"struct Point { public int32 x; }\n"
			"enum Mode { Off, On }\n"
			"int32 g_count = 1;\n"
			"int32 Make() { return 4; }\n"
			"int32 g_made = Make();\n"
			"int32 Add(int32 a, int32 b) { return a + b; }\n"
			"int32 main() { Point p; p.x = Add(g_count, g_made); return p.x - 5; }\n";
	}

	void ReflectionImageTest::ImageCountsTheProgramsEntities()
	{
		Lowered(kProgram, "main", [](llvm::Module* module, CodeGenDriver&) {
			TEST_ASSERT(module != nullptr);
			const llvm::ConstantStruct* image = Image(*module);
			TEST_ASSERT(image != nullptr);
			TEST_ASSERT_EQ(Count(*image, kTypeCount), uint64_t(2));      // Point, Mode
			TEST_ASSERT_EQ(Count(*image, kVariableCount), uint64_t(2));  // g_count, g_made
			const std::vector<std::string> symbols = FunctionSymbols(*image);
			TEST_ASSERT_EQ(Count(*image, kFunctionCount), symbols.size());
			TEST_ASSERT(std::find(symbols.begin(), symbols.end(), "Add_2int32int32") != symbols.end());
			TEST_ASSERT(std::find(symbols.begin(), symbols.end(), "Make_0") != symbols.end());
		});
		TEST_PASSED();
	}

	void ReflectionImageTest::RegistersBeforeEveryOtherConstructor()
	{
		Lowered(kProgram, "main", [](llvm::Module* module, CodeGenDriver&) {
			TEST_ASSERT(module != nullptr);
			TEST_ASSERT_EQ(CtorPriority(*module, "__c4_reflect_init"), int64_t(65534));
			TEST_ASSERT_EQ(CtorPriority(*module, "__c4_global_init"), int64_t(65535));
			TEST_ASSERT(module->getFunction("c4_reflect_register_image") != nullptr);
		});
		TEST_PASSED();
	}

	void ReflectionImageTest::VariablesRecordTheirStorage()
	{
		Lowered(kProgram, "main", [](llvm::Module* module, CodeGenDriver&) {
			TEST_ASSERT(module != nullptr);
			const llvm::ConstantStruct* image = Image(*module);
			TEST_ASSERT(image != nullptr);
			const llvm::ConstantArray* addresses = Rows(*image, kAddresses);
			TEST_ASSERT(addresses != nullptr && addresses->getNumOperands() == 2);
			std::vector<std::string> names;
			for (const llvm::Use& address : addresses->operands()) names.push_back(address->getName().str());
			TEST_ASSERT(std::find(names.begin(), names.end(), "g_count") != names.end());
			TEST_ASSERT(std::find(names.begin(), names.end(), "g_made") != names.end());
			TEST_ASSERT(module->getNamedGlobal("__c4_gv_g_5fcount") == nullptr); // no address companion any more
		});
		TEST_PASSED();
	}

	void ReflectionImageTest::InvokeThunksAreInternal()
	{
		Lowered(kProgram, "main", [](llvm::Module* module, CodeGenDriver&) {
			TEST_ASSERT(module != nullptr);
			const llvm::Function* thunk = module->getFunction("__c4_invoke_Add_5f2int32int32");
			TEST_ASSERT(thunk != nullptr && !thunk->isDeclaration());
			TEST_ASSERT(thunk->hasInternalLinkage());
		});
		TEST_PASSED();
	}

	// BUG-238: a heap Holder needs a compiler-written finalizer, which reflection listed as a function.
	void ReflectionImageTest::CompilerWrittenFunctionsAreLeftOut()
	{
		const char* program =
			"class Resource { public int32 id; [operator(\"delete\")] public void Release() { id = 0; } }\n"
			"class Holder { public Resource resource; }\n"
			"int32 main() { Holder* h = new Holder(); return h->resource.id; }\n";
		Lowered(program, "main", [](llvm::Module* module, CodeGenDriver&) {
			TEST_ASSERT(module != nullptr);
			TEST_ASSERT(module->getFunction("__c4_delete_Holder") != nullptr);
			const llvm::ConstantStruct* image = Image(*module);
			TEST_ASSERT(image != nullptr);
			for (const std::string& symbol : FunctionSymbols(*image)) TEST_ASSERT(symbol.compare(0, 5, "__c4_") != 0);
			for (const llvm::Function& f : *module) TEST_ASSERT(f.getName().find("_5f_5fc4_5fdelete") == llvm::StringRef::npos);
		});
		TEST_PASSED();
	}

	void ReflectionImageTest::AnnotationThatDoesNotFoldFailsTheBuild()
	{
		const char* program =
			"int32 Seven() { return 7; }\n"
			"[Range(Seven())] int32 g_level = 1;\n"
			"int32 main() { return g_level - 1; }\n";
		Lowered(program, "main", [](llvm::Module* module, CodeGenDriver& driver) {
			TEST_ASSERT(module == nullptr);
			TEST_ASSERT(!driver.LoweringErrors().Empty());
		});
		TEST_PASSED();
	}

	// BUG-236: an empty entry name is a module without main (a library, a script).
	void ReflectionImageTest::ModuleWithoutEntryPointLowers()
	{
		Lowered("int32 Add(int32 a, int32 b) { return a + b; }\n", "", [](llvm::Module* module, CodeGenDriver&) {
			TEST_ASSERT(module != nullptr);
			TEST_ASSERT(module->getFunction("main") == nullptr);
			TEST_ASSERT(module->getFunction("Add_2int32int32") != nullptr);
		});
		TEST_PASSED();
	}
} // namespace Testing
