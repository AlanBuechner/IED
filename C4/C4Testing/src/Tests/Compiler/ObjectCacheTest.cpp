#include "ObjectCacheTest.h"
#include "Compiler/ModulePartitions.h"
#include "Compiler/ObjectCache.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;
	namespace fs = std::filesystem;

	namespace
	{
		constexpr unsigned kPartitions = 4;

		// Twelve functions calling one another, a private literal used by one of them, one used by two, a global with
		// an initializer, a private function and file-scope assembly.
		std::string ProgramText(const std::string& extra)
		{
			std::string text = "module asm \".globl partition_asm\"\n"
				"@only = private constant [5 x i8] c\"only\\00\"\n"
				"@shared = private constant [7 x i8] c\"shared\\00\"\n"
				"@counter = global i32 7\n"
				"define internal i32 @helper(i32 %v) { ret i32 %v }\n"
				"define ptr @f0() { ret ptr @only }\n"
				"define ptr @f1() { ret ptr @shared }\n"
				"define ptr @f2() { ret ptr @shared }\n";
			for (int i = 3; i < 12; ++i)
				text += "define i32 @f" + std::to_string(i) + "() { %a = load i32, ptr @counter\n %b = call i32 @helper(i32 %a)\n ret i32 %b }\n";
			return text + extra;
		}

		// `globals` described globals g0, g1, ... (twelve functions use the first four), a described constant no global
		// carries, and an enum in the compile unit's program-wide list.
		std::string DescribedProgramText(int globals)
		{
			std::string text = "!llvm.dbg.cu = !{!0}\n"
				"!llvm.module.flags = !{!1}\n"
				"!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !2, producer: \"c4\", isOptimized: false, "
				"runtimeVersion: 0, emissionKind: FullDebug, enums: !3, globals: !4)\n"
				"!1 = !{i32 2, !\"Debug Info Version\", i32 3}\n"
				"!2 = !DIFile(filename: \"program.cf\", directory: \"C:/\")\n"
				"!3 = !{!5}\n"
				"!5 = !DICompositeType(tag: DW_TAG_enumeration_type, name: \"Color\", file: !2, line: 1, size: 32, elements: !6)\n"
				"!6 = !{!7}\n"
				"!7 = !DIEnumerator(name: \"Red\", value: 0)\n"
				"!8 = !DIBasicType(name: \"int32\", size: 32, encoding: DW_ATE_signed)\n"
				"!199 = !DIGlobalVariableExpression(var: !299, expr: !DIExpression(DW_OP_constu, 42, DW_OP_stack_value))\n"
				"!299 = distinct !DIGlobalVariable(name: \"kAnswer\", scope: !0, file: !2, line: 40, type: !8, isLocal: true, isDefinition: true)\n";
			std::string listed;
			for (int i = 0; i < globals; ++i)
			{
				const std::string n = std::to_string(i);
				text += "@g" + n + " = global i32 " + n + ", !dbg !" + std::to_string(100 + i) + "\n";
				text += "!" + std::to_string(100 + i) + " = !DIGlobalVariableExpression(var: !" + std::to_string(200 + i) + ", expr: !DIExpression())\n";
				text += "!" + std::to_string(200 + i) + " = distinct !DIGlobalVariable(name: \"g" + n + "\", scope: !0, file: !2, line: " +
					std::to_string(i + 2) + ", type: !8, isLocal: false, isDefinition: true)\n";
				listed += "!" + std::to_string(100 + i) + ", ";
			}
			text += "!4 = !{" + listed + "!199}\n";
			for (int i = 0; i < 12; ++i)
				text += "define i32 @f" + std::to_string(i) + "() { %a = load i32, ptr @g" + std::to_string(i % 4) + "\n ret i32 %a }\n";
			return text;
		}

		// Six functions with debug info in each of a.cf and b.cf, a.cf's lines moved down by `shift`.
		std::string TwoFileProgramText(int shift)
		{
			std::string text = "!llvm.dbg.cu = !{!0}\n"
				"!llvm.module.flags = !{!1}\n"
				"!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !2, producer: \"c4\", isOptimized: false, "
				"runtimeVersion: 0, emissionKind: FullDebug)\n"
				"!1 = !{i32 2, !\"Debug Info Version\", i32 3}\n"
				"!2 = !DIFile(filename: \"a.cf\", directory: \"C:/\")\n"
				"!3 = !DIFile(filename: \"b.cf\", directory: \"C:/\")\n"
				"!8 = !DIBasicType(name: \"int32\", size: 32, encoding: DW_ATE_signed)\n"
				"!9 = !DISubroutineType(types: !{!8})\n";
			int node = 100;
			for (const char* file : { "a", "b" })
				for (int i = 0; i < 6; ++i)
				{
					const bool inA = file[0] == 'a';
					const std::string line = std::to_string(10 * i + 1 + (inA ? shift : 0));
					const std::string name = std::string(file) + std::to_string(i);
					const std::string program = "!" + std::to_string(node);
					const std::string location = "!" + std::to_string(node + 1);
					text += "define i32 @" + name + "() !dbg " + program + " { ret i32 " + std::to_string(i) + ", !dbg " + location + " }\n";
					text += program + " = distinct !DISubprogram(name: \"" + name + "\", scope: " + (inA ? "!2" : "!3") + ", file: " +
						(inA ? "!2" : "!3") + ", line: " + line + ", type: !9, scopeLine: " + line + ", spFlags: DISPFlagDefinition, unit: !0)\n";
					text += location + " = !DILocation(line: " + line + ", column: 1, scope: " + program + ")\n";
					node += 2;
				}
			return text;
		}

		// The names of the globals a module's compile units list, each with whether a definition there carries it.
		std::vector<std::pair<std::string, bool>> ListedGlobals(const llvm::Module& module)
		{
			std::set<const llvm::DIGlobalVariableExpression*> carried;
			llvm::SmallVector<llvm::DIGlobalVariableExpression*, 1> expressions;
			for (const llvm::GlobalVariable& variable : module.globals())
			{
				if (variable.isDeclaration()) continue;
				expressions.clear();
				variable.getDebugInfo(expressions);
				carried.insert(expressions.begin(), expressions.end());
			}
			std::vector<std::pair<std::string, bool>> listed;
			for (const llvm::DICompileUnit* unit : module.debug_compile_units())
				for (const llvm::DIGlobalVariableExpression* expression : unit->getGlobalVariables())
					listed.push_back({ expression->getVariable()->getName().str(), carried.count(expression) != 0 });
			return listed;
		}

		std::unique_ptr<llvm::Module> Parse(const std::string& text, llvm::LLVMContext& context)
		{
			llvm::SMDiagnostic error;
			return llvm::parseAssemblyString(text, error, context);
		}

		std::vector<std::string> Split(const std::string& text)
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = Parse(text, context);
			return module ? SplitModuleToBitcode(*module, kPartitions) : std::vector<std::string>();
		}

		std::unique_ptr<llvm::Module> ReadBack(const std::string& bitcode, llvm::LLVMContext& context)
		{
			llvm::Expected<std::unique_ptr<llvm::Module>> module = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, "partition"), context);
			if (!module)
			{
				llvm::consumeError(module.takeError());
				return nullptr;
			}
			return std::move(*module);
		}

		// The partition that defines `name`, or -1.
		int DefinedIn(const std::vector<std::string>& partitions, const std::string& name)
		{
			for (size_t i = 0; i < partitions.size(); ++i)
			{
				llvm::LLVMContext context;
				std::unique_ptr<llvm::Module> module = ReadBack(partitions[i], context);
				const llvm::GlobalValue* value = module ? module->getNamedValue(name) : nullptr;
				if (value && !value->isDeclaration()) return static_cast<int>(i);
			}
			return -1;
		}

		fs::path FreshFolder(const char* name)
		{
			const fs::path folder = fs::temp_directory_path() / "c4-objectcache-test" / name;
			fs::remove_all(folder);
			fs::create_directories(folder);
			return folder;
		}
	}

	void ObjectCacheTest::TheSameCodeNamesTheSameObject()
	{
		const ObjectCache cache("C:/cache", "C4 100 -O0 x86-64");
		TEST_ASSERT(cache.ObjectFor("bitcode") == cache.ObjectFor(std::string("bitcode")));
		TEST_ASSERT(cache.ObjectFor("bitcode").parent_path() == fs::path("C:/cache"));
		TEST_ASSERT(cache.ObjectFor("bitcode").extension() == ".obj");
	}

	void ObjectCacheTest::ChangedCodeNamesAnotherObject()
	{
		const ObjectCache cache("C:/cache", "salt");
		TEST_ASSERT(cache.ObjectFor("bitcode") != cache.ObjectFor("bitcodf"));
		TEST_ASSERT(cache.ObjectFor("") != cache.ObjectFor(std::string(1, '\0')));
	}

	void ObjectCacheTest::AnotherCompilerOrTargetNamesAnotherObject()
	{
		const ObjectCache o0("C:/cache", "C4 100 12345 -O0 x86-64");
		const ObjectCache o2("C:/cache", "C4 100 12345 -O2 x86-64");
		const ObjectCache rebuilt("C:/cache", "C4 100 12346 -O0 x86-64");
		TEST_ASSERT(o0.ObjectFor("bitcode") != o2.ObjectFor("bitcode"));
		TEST_ASSERT(o0.ObjectFor("bitcode") != rebuilt.ObjectFor("bitcode"));
	}

	void ObjectCacheTest::KeepOnlyRemovesEverythingElse()
	{
		const fs::path folder = FreshFolder("keep");
		const ObjectCache cache(folder, "salt");
		const fs::path kept = cache.ObjectFor("kept");
		const fs::path stale = cache.ObjectFor("stale");
		for (const fs::path& path : { kept, stale, folder / "half-written.obj.tmp" }) std::ofstream(path) << "x";
		cache.KeepOnly({ kept });
		TEST_ASSERT(fs::exists(kept));
		TEST_ASSERT(!fs::exists(stale));
		TEST_ASSERT(!fs::exists(folder / "half-written.obj.tmp"));
		cache.KeepOnly({});
		TEST_ASSERT(fs::is_empty(folder));
		fs::remove_all(folder.parent_path());
	}

	void ObjectCacheTest::EveryPartitionReadsBackAndVerifies()
	{
		const std::vector<std::string> partitions = Split(ProgramText(""));
		TEST_ASSERT_EQ(partitions.size(), static_cast<size_t>(kPartitions));
		for (const std::string& bitcode : partitions)
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = ReadBack(bitcode, context);
			TEST_ASSERT(module != nullptr);
			if (module) TEST_ASSERT(!llvm::verifyModule(*module, &llvm::errs()));
		}
	}

	void ObjectCacheTest::EachDefinitionIsInOnePartition()
	{
		const std::vector<std::string> partitions = Split(ProgramText(""));
		for (int i = 0; i < 12; ++i)
		{
			const std::string name = "f" + std::to_string(i);
			int definitions = 0;
			for (const std::string& bitcode : partitions)
			{
				llvm::LLVMContext context;
				std::unique_ptr<llvm::Module> module = ReadBack(bitcode, context);
				const llvm::Function* function = module ? module->getFunction(name) : nullptr;
				if (function && !function->isDeclaration()) ++definitions;
			}
			TEST_ASSERT_EQ(definitions, 1);
		}
		TEST_ASSERT(DefinedIn(partitions, "counter") >= 0);
	}

	void ObjectCacheTest::PrivateDataLivesWithItsUser()
	{
		const std::vector<std::string> partitions = Split(ProgramText(""));
		TEST_ASSERT_EQ(DefinedIn(partitions, "only.local"), DefinedIn(partitions, "f0"));
		// Of its two users, the one whose name sorts first.
		TEST_ASSERT_EQ(DefinedIn(partitions, "shared.local"), DefinedIn(partitions, "f1"));
	}

	void ObjectCacheTest::LocalsBecomeSuffixedHiddenGlobals()
	{
		const std::vector<std::string> partitions = Split(ProgramText(""));
		const int home = DefinedIn(partitions, "helper.local");
		TEST_ASSERT(home >= 0);
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = ReadBack(partitions[home], context);
		const llvm::Function* helper = module->getFunction("helper.local");
		TEST_ASSERT(helper->hasExternalLinkage() && helper->hasHiddenVisibility());
		TEST_ASSERT(module->getFunction("helper") == nullptr);
	}

	void ObjectCacheTest::OnlyTheFirstPartitionKeepsFileScopeAssembly()
	{
		const std::vector<std::string> partitions = Split(ProgramText(""));
		for (size_t i = 0; i < partitions.size(); ++i)
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = ReadBack(partitions[i], context);
			TEST_ASSERT_EQ(module->getModuleInlineAsm().empty(), i != 0);
		}
	}

	void ObjectCacheTest::EachUnitListsTheGlobalsItsPartitionDefines()
	{
		const std::vector<std::string> partitions = Split(DescribedProgramText(4));
		TEST_ASSERT_EQ(partitions.size(), static_cast<size_t>(kPartitions));
		std::map<std::string, int> listings;
		for (size_t i = 0; i < partitions.size(); ++i)
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = ReadBack(partitions[i], context);
			TEST_ASSERT(module != nullptr);
			if (!module) continue;
			TEST_ASSERT(!llvm::verifyModule(*module, &llvm::errs()));
			for (const auto& [name, carried] : ListedGlobals(*module))
			{
				++listings[name];
				// The constant no global carries goes with the program-wide lists.
				if (name == "kAnswer") TEST_ASSERT_EQ(i, static_cast<size_t>(0));
				else TEST_ASSERT(carried);
			}
		}
		for (const char* name : { "g0", "g1", "g2", "g3", "kAnswer" }) TEST_ASSERT_EQ(listings[name], 1);
		TEST_ASSERT_EQ(listings.size(), static_cast<size_t>(5));
	}

	void ObjectCacheTest::OnlyTheFirstPartitionKeepsTheProgramWideDebugLists()
	{
		const std::vector<std::string> partitions = Split(DescribedProgramText(4));
		for (size_t i = 0; i < partitions.size(); ++i)
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = ReadBack(partitions[i], context);
			TEST_ASSERT(module != nullptr);
			if (!module) continue;
			for (const llvm::DICompileUnit* unit : module->debug_compile_units())
				TEST_ASSERT_EQ(unit->getEnumTypes().size(), i == 0 ? 1u : 0u);
		}
	}

	void ObjectCacheTest::Bug327AddingADescribedGlobalLeavesTheOtherPartitionsUnchanged()
	{
		const std::vector<std::string> before = Split(DescribedProgramText(4));
		const std::vector<std::string> after = Split(DescribedProgramText(5));
		const int home = DefinedIn(after, "g4");
		TEST_ASSERT(home >= 0);
		int changed = 0;
		for (size_t i = 0; i < after.size(); ++i)
		{
			if (before[i] != after[i]) ++changed;
			if (static_cast<int>(i) != home) TEST_ASSERT(before[i] == after[i]);
		}
		TEST_ASSERT_EQ(changed, 1);
	}

	void ObjectCacheTest::AFilesCodeSharesAPartition()
	{
		const std::vector<std::string> partitions = Split(TwoFileProgramText(0));
		for (const char* file : { "a", "b" })
		{
			const int home = DefinedIn(partitions, std::string(file) + "0");
			TEST_ASSERT(home >= 0);
			for (int i = 1; i < 6; ++i) TEST_ASSERT_EQ(DefinedIn(partitions, std::string(file) + std::to_string(i)), home);
		}
	}

	void ObjectCacheTest::Bug328MovingAFilesLinesChangesOnlyItsPartition()
	{
		const std::vector<std::string> before = Split(TwoFileProgramText(0));
		const std::vector<std::string> after = Split(TwoFileProgramText(1));
		const int home = DefinedIn(after, "a0");
		TEST_ASSERT(home >= 0);
		TEST_ASSERT(before[home] != after[home]);
		for (size_t i = 0; i < after.size(); ++i)
			if (static_cast<int>(i) != home) TEST_ASSERT(before[i] == after[i]);
	}

	void ObjectCacheTest::AddingAFunctionLeavesTheOtherPartitionsUnchanged()
	{
		const std::vector<std::string> before = Split(ProgramText(""));
		const std::vector<std::string> after = Split(ProgramText("define i32 @added() { %a = load i32, ptr @counter\n ret i32 %a }\n"));
		const int home = DefinedIn(after, "added");
		TEST_ASSERT(home >= 0);
		for (size_t i = 0; i < after.size(); ++i)
			if (static_cast<int>(i) != home) TEST_ASSERT(before[i] == after[i]);
	}
} // namespace Testing
