#include "PartitionCopiesTest.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/IrVerification.h"
#include "Compiler/Toolchain.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		constexpr unsigned kPartitions = 4;
		constexpr int kCallerFiles = 6;

		// A small function, one that calls another small one, a large one and an `inline` one, called from six files.
		std::string LibraryText()
		{
			std::string text = "int32 Small(int32 v) { return v + 1; }\n"
				"int32 Leaf(int32 v) { return v * 3; }\n"
				"int32 Middle(int32 v) { return Leaf(v) + 1; }\n"
				"inline int32 Tiny(int32 v) { return v - 1; }\n"
				"int32 Big(int32 v) {\n\tint32 a = v;\n";
			for (int i = 0; i < 60; ++i) text += "\ta = a * " + std::to_string(i + 2) + " + v;\n";
			return text + "\treturn a;\n}\n";
		}

		std::string CallerText(int file)
		{
			return "include \"lib.ch\"\nint32 Caller" + std::to_string(file) + "(int32 v) { return Small(v) + Middle(v) + Big(v) + Tiny(v); }\n";
		}

		std::string MainText()
		{
			std::string text;
			for (int i = 0; i < kCallerFiles; ++i) text += "int32 Caller" + std::to_string(i) + "(int32 v);\n";
			text += "int32 main() { int32 sum = 0;\n";
			for (int i = 0; i < kCallerFiles; ++i) text += "\tsum += Caller" + std::to_string(i) + "(" + std::to_string(i) + ");\n";
			return text + "\treturn sum; }\n";
		}

		// How a function appears in a partition.
		enum class Presence { Absent, Declared, Copied, Defined };

		// The function whose symbol starts with `prefix` (its overload suffix follows the name), as `module` has it.
		Presence PresenceOf(const llvm::Module& module, const std::string& prefix)
		{
			for (const llvm::Function& function : module)
			{
				if (!function.getName().starts_with(prefix + "_")) continue;
				if (function.isDeclaration()) return Presence::Declared;
				return function.hasAvailableExternallyLinkage() ? Presence::Copied : Presence::Defined;
			}
			return Presence::Absent;
		}

		bool DefinesACaller(const llvm::Module& module)
		{
			for (int i = 0; i < kCallerFiles; ++i)
				if (PresenceOf(module, "Caller" + std::to_string(i)) == Presence::Defined) return true;
			return false;
		}

		// Lowers the program into kPartitions partitions with `copies` and hands their modules to `inspect`; false when it
		// does not lower.
		template <typename Inspect>
		bool LowerProgram(PartitionCopies copies, Inspect inspect)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("main.cf", MainText().c_str());
			compiler.RegisterInMemory("lib.ch", LibraryText().c_str());
			for (int i = 0; i < kCallerFiles; ++i) compiler.RegisterInMemory(("caller" + std::to_string(i) + ".cf").c_str(), CallerText(i).c_str());
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			ProgramBackend driver(context);
			driver.LowerOn(3);
			driver.SplitInto(kPartitions, copies);
			std::string error;
			if (!driver.Lower(compiler.GetProgramModelForLowering(), "main", &error))
			{
				printf("lowering failed: %s\n", error.c_str());
				return false;
			}
			std::vector<const llvm::Module*> modules;
			for (unsigned i = 0; i < driver.PartitionCount(); ++i) modules.push_back(&driver.PartitionModule(i));
			inspect(modules);
			return true;
		}

		// For each partition that defines a caller but not `name`: how `name` appears there.
		std::vector<Presence> InCallersPartitions(PartitionCopies copies, const std::string& name)
		{
			std::vector<Presence> found;
			LowerProgram(copies, [&](const std::vector<const llvm::Module*>& modules) {
				for (const llvm::Module* module : modules)
					if (DefinesACaller(*module) && PresenceOf(*module, name) != Presence::Defined) found.push_back(PresenceOf(*module, name));
			});
			return found;
		}

		bool All(const std::vector<Presence>& found, Presence presence)
		{
			if (found.empty()) return false; // the program must put a caller where the callee is not
			for (Presence each : found)
				if (each != presence) return false;
			return true;
		}
	}

	void PartitionCopiesTest::SmallCalleesAreCopiedAvailableExternally()
	{
		TEST_ASSERT(All(InCallersPartitions(PartitionCopies::Small, "Small"), Presence::Copied));
		TEST_ASSERT(All(InCallersPartitions(PartitionCopies::Small, "Tiny"), Presence::Copied));
	}

	void PartitionCopiesTest::LargeCalleesAreOnlyDeclared()
	{
		TEST_ASSERT(All(InCallersPartitions(PartitionCopies::Small, "Big"), Presence::Declared));
	}

	void PartitionCopiesTest::CopiesReachThroughSmallCallees()
	{
		TEST_ASSERT(All(InCallersPartitions(PartitionCopies::Small, "Middle"), Presence::Copied));
		TEST_ASSERT(All(InCallersPartitions(PartitionCopies::Small, "Leaf"), Presence::Copied));
	}

	void PartitionCopiesTest::AtO0OnlyInlineFunctionsAreCopied()
	{
		TEST_ASSERT(All(InCallersPartitions(PartitionCopies::Inline, "Tiny"), Presence::Copied));
		TEST_ASSERT(All(InCallersPartitions(PartitionCopies::Inline, "Small"), Presence::Declared));
		for (Presence presence : InCallersPartitions(PartitionCopies::Inline, "Leaf")) TEST_ASSERT(presence != Presence::Copied);
	}

	void PartitionCopiesTest::PartitionsMergedFirstCopyNothing()
	{
		for (const char* name : { "Small", "Tiny", "Middle", "Big" })
			TEST_ASSERT(All(InCallersPartitions(PartitionCopies::None, name), Presence::Declared));
	}

	void PartitionCopiesTest::EveryPartitionVerifies()
	{
		for (PartitionCopies copies : { PartitionCopies::None, PartitionCopies::Inline, PartitionCopies::Small })
			TEST_ASSERT(LowerProgram(copies, [](const std::vector<const llvm::Module*>& modules) {
				TEST_ASSERT_EQ(modules.size(), size_t(kPartitions));
				for (const llvm::Module* module : modules) TEST_ASSERT(!InvalidIr(*module));
			}));
	}
} // namespace Testing
