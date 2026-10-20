#include "PartitionImportsTest.h"
#include "Compiler/ModulePartitions.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		constexpr unsigned kPartitions = 4;
		constexpr int kCallers = 12;

		// Twelve callers of a small function, a small one that calls another, a large one, a noinline one and a
		// linkonce one.
		std::string ProgramText(const std::string& smallBody)
		{
			std::string text = "define i32 @small(i32 %v) { " + smallBody + "\n ret i32 %r }\n"
				"define i32 @middle(i32 %v) { %r = call i32 @leaf(i32 %v)\n ret i32 %r }\n"
				"define i32 @leaf(i32 %v) { %r = mul i32 %v, 3\n ret i32 %r }\n"
				"define i32 @pinned(i32 %v) noinline { ret i32 %v }\n"
				"define linkonce_odr i32 @shared(i32 %v) { ret i32 %v }\n"
				"define i32 @big(i32 %v) {\n %a0 = add i32 %v, 0\n";
			for (int i = 1; i < 120; ++i) text += " %a" + std::to_string(i) + " = add i32 %a" + std::to_string(i - 1) + ", " + std::to_string(i) + "\n";
			text += " ret i32 %a119 }\n";
			for (int i = 0; i < kCallers; ++i)
				text += "define i32 @c" + std::to_string(i) + "(i32 %v) { %a = call i32 @small(i32 %v)\n %b = call i32 @middle(i32 %a)\n"
					" %c = call i32 @pinned(i32 %b)\n %d = call i32 @big(i32 %c)\n %e = call i32 @shared(i32 %d)\n ret i32 %e }\n";
			return text;
		}

		const char* kSmallBody = "%r = add i32 %v, 1";

		std::vector<std::string> Split(const std::string& text, PartitionOptimization optimization)
		{
			llvm::LLVMContext context;
			llvm::SMDiagnostic error;
			std::unique_ptr<llvm::Module> module = llvm::parseAssemblyString(text, error, context);
			return module ? SplitModuleToBitcode(*module, kPartitions, optimization) : std::vector<std::string>();
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

		bool DefinesACaller(const llvm::Module& module)
		{
			for (int i = 0; i < kCallers; ++i)
			{
				const llvm::Function* caller = module.getFunction("c" + std::to_string(i));
				if (caller && !caller->isDeclaration()) return true;
			}
			return false;
		}

		// How `name` appears in a partition.
		enum class Presence { Absent, Declared, Copied, Defined };

		Presence PresenceOf(const llvm::Module& module, const std::string& name)
		{
			const llvm::Function* function = module.getFunction(name);
			if (!function) return Presence::Absent;
			if (function->isDeclaration()) return Presence::Declared;
			return function->hasAvailableExternallyLinkage() ? Presence::Copied : Presence::Defined;
		}

		// For each partition that defines a caller but not `name`: how `name` appears there.
		std::vector<Presence> InCallersPartitions(const std::vector<std::string>& partitions, const std::string& name)
		{
			std::vector<Presence> found;
			for (const std::string& bitcode : partitions)
			{
				llvm::LLVMContext context;
				std::unique_ptr<llvm::Module> module = ReadBack(bitcode, context);
				if (module && DefinesACaller(*module) && PresenceOf(*module, name) != Presence::Defined) found.push_back(PresenceOf(*module, name));
			}
			return found;
		}

		bool All(const std::vector<Presence>& found, Presence presence)
		{
			for (Presence each : found)
				if (each != presence) return false;
			return true;
		}

		int Home(const std::vector<std::string>& partitions, const std::string& name)
		{
			for (size_t i = 0; i < partitions.size(); ++i)
			{
				llvm::LLVMContext context;
				std::unique_ptr<llvm::Module> module = ReadBack(partitions[i], context);
				if (module && PresenceOf(*module, name) == Presence::Defined) return static_cast<int>(i);
			}
			return -1;
		}
	}

	void PartitionImportsTest::SmallCalleesAreCopiedAvailableExternally()
	{
		const std::vector<Presence> after = InCallersPartitions(Split(ProgramText(kSmallBody), PartitionOptimization::After), "small");
		TEST_ASSERT(!after.empty());
		TEST_ASSERT(All(after, Presence::Copied));
		const std::vector<Presence> before = InCallersPartitions(Split(ProgramText(kSmallBody), PartitionOptimization::Before), "small");
		TEST_ASSERT(All(before, Presence::Declared)); // optimized whole: nothing to copy
	}

	void PartitionImportsTest::LargeAndNoInlineCalleesAreOnlyDeclared()
	{
		const std::vector<std::string> partitions = Split(ProgramText(kSmallBody), PartitionOptimization::After);
		const std::vector<Presence> big = InCallersPartitions(partitions, "big");
		const std::vector<Presence> pinned = InCallersPartitions(partitions, "pinned");
		TEST_ASSERT(!big.empty() && !pinned.empty());
		TEST_ASSERT(All(big, Presence::Declared));
		TEST_ASSERT(All(pinned, Presence::Declared));
	}

	void PartitionImportsTest::ImportsReachThroughSmallCallees()
	{
		const std::vector<std::string> partitions = Split(ProgramText(kSmallBody), PartitionOptimization::After);
		const std::vector<Presence> leaf = InCallersPartitions(partitions, "leaf");
		TEST_ASSERT(!leaf.empty());
		TEST_ASSERT(All(leaf, Presence::Copied)); // with middle, or for the middle the partition defines
	}

	void PartitionImportsTest::MinSizeCallersCopyNothing()
	{
		std::string text = "define i32 @tiny(i32 %v) { ret i32 %v }\n";
		for (int i = 0; i < kCallers; ++i)
			text += "define i32 @t" + std::to_string(i) + "(i32 %v) minsize { %r = call i32 @tiny(i32 %v)\n ret i32 %r }\n";
		for (const std::string& bitcode : Split(text, PartitionOptimization::After))
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = ReadBack(bitcode, context);
			TEST_ASSERT(module && PresenceOf(*module, "tiny") != Presence::Copied);
		}
	}

	void PartitionImportsTest::LinkOnceDefinitionsAreKept()
	{
		const std::vector<std::string> after = Split(ProgramText(kSmallBody), PartitionOptimization::After);
		const std::vector<std::string> before = Split(ProgramText(kSmallBody), PartitionOptimization::Before);
		const int home = Home(after, "shared");
		TEST_ASSERT(home >= 0 && home == Home(before, "shared"));
		llvm::LLVMContext context;
		TEST_ASSERT(ReadBack(after[home], context)->getFunction("shared")->getLinkage() == llvm::GlobalValue::WeakODRLinkage);
		TEST_ASSERT(ReadBack(before[home], context)->getFunction("shared")->getLinkage() == llvm::GlobalValue::LinkOnceODRLinkage);
	}

	void PartitionImportsTest::AnImportedBodyChangesItsCallersPartitions()
	{
		const std::string edited = "%r = add i32 %v, 2";
		for (PartitionOptimization optimization : { PartitionOptimization::After, PartitionOptimization::Before })
		{
			const std::vector<std::string> original = Split(ProgramText(kSmallBody), optimization);
			const std::vector<std::string> changed = Split(ProgramText(edited), optimization);
			const int home = Home(changed, "small");
			TEST_ASSERT(home >= 0 && original[home] != changed[home]);
			for (size_t i = 0; i < changed.size(); ++i)
			{
				if (static_cast<int>(i) == home) continue;
				llvm::LLVMContext context;
				const bool calls = DefinesACaller(*ReadBack(changed[i], context));
				const bool expectChange = calls && optimization == PartitionOptimization::After;
				TEST_ASSERT_EQ(original[i] != changed[i], expectChange);
			}
		}
	}

	void PartitionImportsTest::EveryPartitionVerifies()
	{
		for (const std::string& bitcode : Split(ProgramText(kSmallBody), PartitionOptimization::After))
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = ReadBack(bitcode, context);
			TEST_ASSERT(module != nullptr);
			if (module) TEST_ASSERT(!llvm::verifyModule(*module, &llvm::errs()));
		}
	}
} // namespace Testing
