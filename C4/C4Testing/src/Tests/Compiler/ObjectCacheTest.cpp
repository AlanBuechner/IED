#include "ObjectCacheTest.h"
#include "Compiler/ModulePartitions.h"
#include "Compiler/ObjectCache.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <filesystem>
#include <fstream>
#include <memory>
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
