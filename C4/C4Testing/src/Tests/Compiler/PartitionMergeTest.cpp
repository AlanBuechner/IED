#include "PartitionMergeTest.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/IrVerification.h"
#include "Compiler/ModulePartitions.h"
#include "Compiler/PartitionMerge.h"
#include "Compiler/Toolchain.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		const char* kLibrary =
			"static int32 Twice(int32 v) { return v * 2; }\n"
			"int32 g_total = 3;\n"
			"int32 Count() { static int32 calls = 0; calls += 1; return calls + Twice(g_total); }\n"
			"class Shape { public virtual int32 Sides() const { return 0; } }\n"
			"class Square : public Shape { public int32 Sides() const override { return 4; } }\n";

		const char* kMain =
			"include \"shapes.ch\"\n"
			"int32 g_other = 5;\n"
			"int32 main() { Shape* s = new Square(); return Count() + s->Sides() + g_other - 14; }\n";

		// The program lowered into `count` partitions, as a whole-program build emits it (no copies between them), merged
		// with its reflection image when it has one apart; `inspect` gets the merged module, or null with the error.
		template <typename Inspect>
		bool LowerAndMerge(unsigned count, Inspect inspect)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("main.cf", kMain);
			compiler.RegisterInMemory("shapes.ch", kLibrary);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			ProgramBackend driver(context);
			driver.LowerOn(3);
			driver.SplitInto(count, PartitionCopies::None);
			std::string error;
			if (!driver.Lower(compiler.GetProgramModelForLowering(), "main", &error))
			{
				printf("lowering failed: %s\n", error.c_str());
				return false;
			}
			std::vector<std::string> bitcode;
			for (unsigned i = 0; i < driver.PartitionCount(); ++i)
				bitcode.push_back(PartitionBitcode(driver.PartitionModule(i), PartitionOptimization::Whole));
			std::vector<KeyedModule::Builder> beside;
			if (!driver.ReflectionImageKey().empty())
				beside.push_back([&driver](llvm::LLVMContext& into) { return driver.BuildReflectionModule(into); });
			llvm::LLVMContext mergedContext;
			std::unique_ptr<llvm::Module> merged = MergePartitions(mergedContext, bitcode, beside, error);
			inspect(merged.get(), error);
			return true;
		}

		const llvm::GlobalValue* DefinedStartingWith(const llvm::Module& module, const char* prefix)
		{
			for (const llvm::Function& f : module)
				if (!f.isDeclaration() && f.getName().starts_with(prefix)) return &f;
			for (const llvm::GlobalVariable& g : module.globals())
				if (!g.isDeclaration() && g.getName().starts_with(prefix)) return &g;
			return nullptr;
		}
	}

	void PartitionMergeTest::EachDefinitionIsInTheMergedModuleOnce()
	{
		TEST_ASSERT(LowerAndMerge(4, [](const llvm::Module* merged, const std::string& error) {
			TEST_ASSERT_MSG(merged != nullptr, error.c_str());
			if (!merged) return;
			TEST_ASSERT(!InvalidIr(*merged));
			std::map<std::string, int> definitions;
			for (const llvm::Function& f : *merged)
			{
				TEST_ASSERT(!f.hasAvailableExternallyLinkage());
				if (!f.isDeclaration()) definitions[f.getName().str()]++;
			}
			for (const auto& [name, count] : definitions) TEST_ASSERT_EQ(count, 1);
			for (const char* defined : { "main", "main_0", "Count_0" }) TEST_ASSERT(definitions.count(defined) == 1);
			for (const char* global : { "g_total", "g_other", "__c4_gc_roots" })
			{
				const llvm::GlobalVariable* storage = merged->getNamedGlobal(global);
				TEST_ASSERT(storage && !storage->isDeclaration());
			}
		}));
	}

	void PartitionMergeTest::LocalsAreLocalAgainUnderTheirProgramNames()
	{
		TEST_ASSERT(LowerAndMerge(4, [](const llvm::Module* merged, const std::string&) {
			TEST_ASSERT(merged != nullptr);
			if (!merged) return;
			for (const llvm::GlobalValue& value : merged->global_values())
				if (!value.isDeclaration()) TEST_ASSERT(!value.getName().ends_with(kLocalSuffix));
			const llvm::GlobalValue* twice = DefinedStartingWith(*merged, "Twice_");
			TEST_ASSERT(twice && twice->hasInternalLinkage() && !twice->hasHiddenVisibility());
			const llvm::GlobalValue* calls = merged->getNamedValue("static.Count_0.calls");
			TEST_ASSERT(calls && !calls->isDeclaration() && calls->hasInternalLinkage());
		}));
	}

	// The image a split program builds apart (D-82) is in the merged module, and the invoke thunks it points at are locals
	// of the program again.
	void PartitionMergeTest::TheReflectionImageJoinsTheProgram()
	{
		TEST_ASSERT(LowerAndMerge(4, [](const llvm::Module* merged, const std::string&) {
			TEST_ASSERT(merged != nullptr);
			if (!merged) return;
			const llvm::GlobalVariable* image = merged->getNamedGlobal("__c4_reflect_image");
			TEST_ASSERT(image && !image->isDeclaration());
			int thunks = 0;
			for (const llvm::Function& f : *merged)
				if (f.getName().starts_with("__c4_invoke_"))
				{
					TEST_ASSERT(!f.isDeclaration() && f.hasInternalLinkage());
					thunks++;
				}
			TEST_ASSERT(thunks > 0);
		}));
	}

	void PartitionMergeTest::OnePartitionIsTheProgram()
	{
		TEST_ASSERT(LowerAndMerge(1, [](const llvm::Module* merged, const std::string&) {
			TEST_ASSERT(merged != nullptr && !InvalidIr(*merged));
			if (!merged) return;
			const llvm::GlobalVariable* image = merged->getNamedGlobal("__c4_reflect_image");
			TEST_ASSERT(image && !image->isDeclaration());
			const llvm::GlobalValue* twice = DefinedStartingWith(*merged, "Twice_");
			TEST_ASSERT(twice && twice->hasInternalLinkage());
		}));
	}

	void PartitionMergeTest::AnUnreadablePartitionIsAnError()
	{
		llvm::LLVMContext context;
		std::string error;
		TEST_ASSERT(MergePartitions(context, { std::string("not bitcode") }, {}, error) == nullptr);
		TEST_ASSERT(error.find("cannot read a partition back") != std::string::npos);
		error.clear();
		TEST_ASSERT(MergePartitions(context, {}, {}, error) == nullptr);
		TEST_ASSERT(!error.empty());
	}
} // namespace Testing
