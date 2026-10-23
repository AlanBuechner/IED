#include "SplitEmissionTest.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ModulePartitions.h"
#include "Compiler/Toolchain.h"
#include "Model/Types/QualifiedName.h"
#include "Script/HostTable.h"
#include "Script/InlineBodies.h"
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

		std::string Replaced(std::string text, const std::string& from, const std::string& to)
		{
			text.replace(text.find(from), from.size(), to);
			return text;
		}

		// Lowers the program, with `library` as its shapes.ch and `main` as its main.cf, into `count` partitions and hands
		// the driver to `inspect`.
		template <typename Inspect>
		bool LowerSplitDriver(unsigned count, const std::string& library, Inspect inspect, const std::string& main = kMain)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("main.cf", main.c_str());
			compiler.RegisterInMemory("shapes.ch", library.c_str());
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			driver.LowerOn(3);
			driver.SplitInto(count, false);
			std::string error;
			if (!driver.Lower(compiler.GetProgramModelForLowering(), "main", &error))
			{
				printf("lowering failed: %s\n", error.c_str());
				return false;
			}
			inspect(driver);
			return true;
		}

		// Lowers the program into `count` partitions and hands their modules, and the module its reflection image is built
		// in (null when there is none), to `inspect` while they live.
		template <typename Inspect>
		bool LowerSplitWithImage(unsigned count, Inspect inspect)
		{
			return LowerSplitDriver(count, kLibrary, [&inspect](CodeGenDriver& driver) {
				std::vector<llvm::Module*> modules;
				for (unsigned i = 0; i < driver.PartitionCount(); ++i) modules.push_back(&driver.PartitionModule(i));
				llvm::LLVMContext imageContext;
				const std::unique_ptr<llvm::Module> image = driver.ReflectionImageKey().empty() ? nullptr : driver.BuildReflectionModule(imageContext);
				inspect(modules, image.get());
			});
		}

		template <typename Inspect>
		bool LowerSplit(unsigned count, Inspect inspect)
		{
			return LowerSplitWithImage(count, [&inspect](const std::vector<llvm::Module*>& modules, llvm::Module*) { inspect(modules); });
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
			for (const char* own : { "main", "__c4_gc_roots", "__c4_reflect_init" }) TEST_ASSERT_EQ(DefinedIn(modules, own), 0);
		}));
	}

	// The image is built in a module of its own (D-82), which defines it and declares what it points at: the invoke
	// thunks, hidden globals of partition 0, and the reflected functions and globals where their partitions define them.
	void SplitEmissionTest::TheReflectionImageIsAModuleOfItsOwn()
	{
		TEST_ASSERT(LowerSplitWithImage(4, [](const std::vector<llvm::Module*>& modules, llvm::Module* image) {
			TEST_ASSERT(image != nullptr);
			const llvm::GlobalVariable* defined = image->getNamedGlobal("__c4_reflect_image");
			TEST_ASSERT(defined && !defined->isDeclaration() && defined->hasExternalLinkage());
			const llvm::GlobalVariable* declared = modules[0]->getNamedGlobal("__c4_reflect_image");
			TEST_ASSERT(declared && declared->isDeclaration() && !declared->use_empty()); // __c4_reflect_init registers it
			const std::map<std::string, int> definitions = Definitions(modules);
			int thunks = 0;
			for (const llvm::Function& f : *image)
			{
				TEST_ASSERT(f.isDeclaration() && definitions.count(f.getName().str()) == 1);
				if (!f.getName().starts_with("__c4_invoke_")) continue;
				TEST_ASSERT(f.hasHiddenVisibility() && f.getName().ends_with(kLocalSuffix));
				thunks++;
			}
			TEST_ASSERT(thunks > 0);
			for (const llvm::GlobalVariable& g : image->globals())
				if (&g != defined) TEST_ASSERT(g.hasPrivateLinkage() || definitions.count(g.getName().str()) == 1);
		}));
		TEST_ASSERT(LowerSplitWithImage(1, [](const std::vector<llvm::Module*>& modules, llvm::Module* image) {
			TEST_ASSERT(image == nullptr);
			const llvm::GlobalVariable* own = modules[0]->getNamedGlobal("__c4_reflect_image");
			TEST_ASSERT(own && !own->isDeclaration());
		}));
	}

	// The image's object is kept by its key (D-82): the same program has the same key, and so does one whose edit changed
	// a body, while a reflected member changes it.
	void SplitEmissionTest::TheReflectionImageKeyFollowsWhatItHolds()
	{
		const std::string libraries[] = { kLibrary, kLibrary, Replaced(kLibrary, "v * 2", "v * 3"),
			Replaced(kLibrary, "class Square : public Shape {", "class Square : public Shape { public int32 corners;") };
		std::string keys[4];
		for (int i = 0; i < 4; ++i)
			TEST_ASSERT(LowerSplitDriver(4, libraries[i], [&keys, i](CodeGenDriver& driver) { keys[i] = driver.ReflectionImageKey(); }));
		TEST_ASSERT(!keys[0].empty());
		TEST_ASSERT_EQ(keys[1], keys[0]);
		TEST_ASSERT_EQ(keys[2], keys[0]);
		TEST_ASSERT(keys[3] != keys[0]);
	}

	// A partition's inline bodies depend on its own code alone (with the names every table lists, the runtime's and the
	// prelude's): every name a body may use is one its own partition lists. So the inline body cache keys them by the
	// partition, and a function added in another file reuses every other partition's.
	void SplitEmissionTest::APartitionsInlineBodiesDependOnItsOwnCode()
	{
		using Bodies = std::map<std::string, std::vector<std::string>>; // each partition's inline bodies, by its bitcode
		auto collect = [](Bodies& bodies) {
			return [&bodies](CodeGenDriver& driver) {
				std::vector<llvm::Module*> modules;
				for (unsigned i = 0; i < driver.PartitionCount(); ++i) modules.push_back(&driver.PartitionModule(i));
				const std::vector<std::string> symbols = HostNamesOf(HostEntries(modules, {}));
				const HostNames names(symbols);
				for (llvm::Module* module : modules) bodies[ModuleBitcode(*module)] = InlineBodies(*module, names, 4);
			};
		};
		Bodies before, after;
		TEST_ASSERT(LowerSplitDriver(8, kLibrary, collect(before)));
		TEST_ASSERT(LowerSplitDriver(8, kLibrary, collect(after), std::string(kMain) + "int32 Added() { return g_other + 1; }" + char(10)));
		int reused = 0;
		for (const auto& [bitcode, bodies] : after)
			if (const auto same = before.find(bitcode); same != before.end())
			{
				TEST_ASSERT(same->second == bodies);
				reused += bodies.empty() ? 0 : 1;
			}
		TEST_ASSERT(reused > 0);
	}

	// An invoke thunk is emitted where its target is, a shared one by its name, and none in partition 0: an edit that
	// adds a reflected method changes only its own partition.
	void SplitEmissionTest::AThunkLivesWithItsTarget()
	{
		TEST_ASSERT(LowerSplit(4, [](const std::vector<llvm::Module*>& modules) {
			const llvm::StringRef prefix = "__c4_invoke_", shared = "__c4_invoke_sig_";
			std::map<std::string, size_t> home; // each definition by its escaped program name, as a thunk names its target
			for (size_t i = 0; i < modules.size(); ++i)
				for (const llvm::Function& f : *modules[i])
					if (!f.isDeclaration()) home[EscapedText(Core::String(ProgramName(f).str().c_str())).c_str()] = i;
			int own = 0;
			for (size_t i = 0; i < modules.size(); ++i)
				for (const llvm::Function& f : *modules[i])
				{
					const llvm::StringRef name = ProgramName(f);
					if (f.isDeclaration() || !name.starts_with(prefix)) continue;
					TEST_ASSERT(i != 0);
					if (name.starts_with(shared)) continue;
					const auto target = home.find(name.drop_front(prefix.size()).str());
					TEST_ASSERT(target != home.end() && target->second == i);
					own++;
				}
			TEST_ASSERT(own > 0);
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

	// A script knows a host's locals by the names the program gave them: the table lists those, and its own module
	// declares each under its partition name. Partition 0's own locals are listed as they were when the table was in
	// it, now hidden globals it defines; another partition's are its helpers. No partition gains a declaration.
	void SplitEmissionTest::AHostListsItsLocalsByTheirProgramNames()
	{
		TEST_ASSERT(LowerSplit(4, [](const std::vector<llvm::Module*>& modules) {
			const size_t functions = modules[0]->size(), globals = modules[0]->global_size();
			const std::vector<HostEntry> entries = HostEntries(modules, {});
			TEST_ASSERT_EQ(modules[0]->size(), functions);
			TEST_ASSERT_EQ(modules[0]->global_size(), globals);
			const std::vector<std::string> names = HostNamesOf(entries);
			TEST_ASSERT(std::find(names.begin(), names.end(), "static.Count_0.calls") != names.end());
			TEST_ASSERT(std::find(names.begin(), names.end(), "__c4_reflect_init") != names.end()); // partition 0's own local
			const llvm::Function* init = modules[0]->getFunction(std::string("__c4_reflect_init") + kLocalSuffix);
			TEST_ASSERT(init && !init->isDeclaration() && init->hasHiddenVisibility() && !init->hasLocalLinkage());
			TEST_ASSERT(std::find(names.begin(), names.end(), "c4_script_module_create") != names.end());
			for (const std::string& name : names) TEST_ASSERT(!llvm::StringRef(name).ends_with(kLocalSuffix));
			llvm::LLVMContext context;
			const std::unique_ptr<llvm::Module> table = HostTableModule(context, entries, modules[0]->getTargetTriple().str(),
				modules[0]->getDataLayoutStr());
			const llvm::GlobalVariable* defined = table->getNamedGlobal("__c4_script_table");
			TEST_ASSERT(defined && !defined->isDeclaration());
			const llvm::GlobalValue* calls = table->getNamedValue("static.Count_0.calls.local");
			TEST_ASSERT(calls && calls->isDeclaration() && calls->hasHiddenVisibility());
		}));
	}
} // namespace Testing
