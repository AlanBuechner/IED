#include "SplitEmissionTest.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ModulePartitions.h"
#include "Compiler/Toolchain.h"
#include "Script/HostTable.h"
#include <algorithm>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <map>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kLibrary =
			"static int32 Twice(int32 v) { return v * 2; }\n"
			"int32 g_total = 3;\n"
			"int32 Count() { static int32 calls = 0; calls += 1; return calls + Twice(g_total); }\n"
			"enum Corner { TopLeft, TopRight }\n"
			"class Shape { public virtual int32 Sides() const { return 0; } }\n"
			"class Square : public Shape { public int32 Sides() const override { return 4; } }\n";

		const char* kMain =
			"include \"shapes.ch\"\n"
			"int32 g_other = 5;\n"
			"int32 main() { Shape* s = new Square(); return Count() + s->Sides() + g_other - 14 + (int32)TopRight - 1; }\n";

		// Lowers the program into `count` partitions and hands their modules to `inspect` while they live.
		template <typename Inspect>
		bool LowerSplit(unsigned count, Inspect inspect)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("main.cf", kMain);
			compiler.RegisterInMemory("shapes.ch", kLibrary);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			driver.SplitInto(count, 3, false);
			std::string error;
			if (!driver.Lower(compiler.GetProgramModelForLowering(), "main", &error))
			{
				printf("lowering failed: %s\n", error.c_str());
				return false;
			}
			std::vector<llvm::Module*> modules;
			for (unsigned i = 0; i < driver.PartitionCount(); ++i) modules.push_back(&driver.PartitionModule(i));
			inspect(modules);
			return true;
		}

		// How many of `modules` define each function or global that one of them defines (a copy for inlining defines
		// nothing).
		std::map<std::string, int> Definitions(const std::vector<llvm::Module*>& modules)
		{
			std::map<std::string, int> definitions;
			for (const llvm::Module* module : modules)
			{
				for (const llvm::Function& f : *module)
					if (!f.isDeclaration() && !f.hasLocalLinkage() && !f.hasAvailableExternallyLinkage()) definitions[f.getName().str()]++;
				for (const llvm::GlobalVariable& g : module->globals())
					if (!g.isDeclaration() && !g.hasLocalLinkage() && !g.hasLinkOnceLinkage()) definitions[g.getName().str()]++;
			}
			return definitions;
		}

		// The partition that defines `name`, or -1.
		int DefinedIn(const std::vector<llvm::Module*>& modules, const std::string& name)
		{
			for (size_t i = 0; i < modules.size(); ++i)
				if (const llvm::GlobalValue* value = modules[i]->getNamedValue(name); value && !value->isDeclaration()) return static_cast<int>(i);
			return -1;
		}

		// The function `module` defines whose symbol starts with `prefix` (a file-static one's symbol names its unit).
		const llvm::Function* DefinedStartingWith(const llvm::Module& module, const char* prefix)
		{
			for (const llvm::Function& f : module)
				if (!f.isDeclaration() && !f.hasAvailableExternallyLinkage() && f.getName().starts_with(prefix)) return &f;
			return nullptr;
		}
	}

	void SplitEmissionTest::EachFunctionIsDefinedInOnePartition()
	{
		TEST_ASSERT(LowerSplit(4, [](const std::vector<llvm::Module*>& modules) {
			TEST_ASSERT_EQ(modules.size(), size_t(4));
			const std::map<std::string, int> definitions = Definitions(modules);
			for (const auto& [name, count] : definitions) TEST_ASSERT_EQ(count, 1);
			for (const char* function : { "Count_0", "main_0" }) TEST_ASSERT(DefinedIn(modules, function) > 0);
			// Every function a partition calls is defined somewhere in the program, or is foreign.
			for (const llvm::Module* module : modules)
				for (const llvm::Function& f : *module)
					if (f.isDeclaration() && !f.isIntrinsic() && !f.use_empty() && f.getName().starts_with("Count"))
						TEST_ASSERT(definitions.count(f.getName().str()) == 1);
		}));
	}

	void SplitEmissionTest::EachGlobalIsDefinedInOnePartition()
	{
		TEST_ASSERT(LowerSplit(4, [](const std::vector<llvm::Module*>& modules) {
			for (const char* global : { "g_total", "g_other" })
			{
				const int home = DefinedIn(modules, global);
				TEST_ASSERT(home > 0);
				for (size_t i = 0; i < modules.size(); ++i)
					if (static_cast<int>(i) != home)
						if (const llvm::GlobalValue* declared = modules[i]->getNamedValue(global)) TEST_ASSERT(declared->isDeclaration());
			}
		}));
	}

	// The GC roots in the first partition name a static local another partition defines.
	void SplitEmissionTest::LocalsOthersNameAreHiddenGlobals()
	{
		TEST_ASSERT(LowerSplit(4, [](const std::vector<llvm::Module*>& modules) {
			int statics = 0;
			for (const llvm::Module* module : modules)
				for (const llvm::GlobalVariable& g : module->globals())
					if (g.getName().starts_with("static.") && !g.isDeclaration())
					{
						TEST_ASSERT(g.getName().ends_with(".local"));
						TEST_ASSERT(g.hasExternalLinkage() && g.hasHiddenVisibility());
						statics++;
					}
			TEST_ASSERT_EQ(statics, 1);
			int defined = 0;
			for (const llvm::Module* module : modules)
				if (const llvm::Function* twice = DefinedStartingWith(*module, "Twice_"))
				{
					TEST_ASSERT(twice->getName().ends_with(".local") && twice->hasExternalLinkage() && twice->hasHiddenVisibility());
					defined++;
				}
			TEST_ASSERT_EQ(defined, 1);
		}));
	}

	void SplitEmissionTest::TheProgramsOwnCodeIsInTheFirstPartition()
	{
		TEST_ASSERT(LowerSplit(4, [](const std::vector<llvm::Module*>& modules) {
			for (const char* own : { "main", "__c4_gc_roots", "__c4_reflect_image" }) TEST_ASSERT_EQ(DefinedIn(modules, own), 0);
		}));
	}

	// An unscoped enum's members are constants the model lists with the globals: no partition gives them storage, so
	// none names one after the order it made it in.
	void SplitEmissionTest::ConstantsWithoutASymbolHaveNoStorage()
	{
		for (unsigned count : { 1u, 4u })
			TEST_ASSERT(LowerSplit(count, [](const std::vector<llvm::Module*>& modules) {
				for (const llvm::Module* module : modules)
					for (const llvm::GlobalVariable& g : module->globals()) TEST_ASSERT(!g.getName().starts_with("c4_global"));
			}));
	}

	void SplitEmissionTest::OnePartitionKeepsLocalsLocal()
	{
		TEST_ASSERT(LowerSplit(1, [](const std::vector<llvm::Module*>& modules) {
			TEST_ASSERT_EQ(modules.size(), size_t(1));
			const llvm::Function* twice = DefinedStartingWith(*modules[0], "Twice_");
			TEST_ASSERT(twice != nullptr && twice->hasInternalLinkage() && !twice->getName().ends_with(".local"));
		}));
	}

	// A script knows a host's locals by the names the program gave them: the table lists those, and finds each under
	// its partition name.
	void SplitEmissionTest::AHostListsItsLocalsByTheirProgramNames()
	{
		TEST_ASSERT(LowerSplit(4, [](const std::vector<llvm::Module*>& modules) {
			const std::vector<std::string> symbols = HostSymbols(modules, {});
			TEST_ASSERT(std::find(symbols.begin(), symbols.end(), "static.Count_0.calls") != symbols.end());
			for (const std::string& symbol : symbols) TEST_ASSERT(!llvm::StringRef(symbol).ends_with(kLocalSuffix));
			std::string error;
			TEST_ASSERT(EmitHostTable(*modules[0], symbols, &error));
			TEST_ASSERT_EQ(error, std::string());
		}));
	}
} // namespace Testing
