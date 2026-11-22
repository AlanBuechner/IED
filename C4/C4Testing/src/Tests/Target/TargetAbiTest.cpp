#include "TargetAbiTest.h"
#include "Backend/CodeGen/PeTables.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/CpuTarget.h"
#include "Compiler/Toolchain.h"
#include "Model/ProgramModel.h"
#include "Target/TargetAbi.h"
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const TargetAbi& Windows() { return TargetAbi::For(TargetSystem::Windows); }
		const TargetAbi& Linux() { return TargetAbi::For(TargetSystem::Linux); }

		bool Has(const std::vector<std::string>& args, const std::string& arg)
		{
			return std::find(args.begin(), args.end(), arg) != args.end();
		}

		// A table holding one image-relative reference to a global, as RTTI and EH tables do.
		llvm::GlobalVariable* TableReferencingAGlobal(llvm::Module& module)
		{
			llvm::LLVMContext& ctx = module.getContext();
			auto* referenced = new llvm::GlobalVariable(module, llvm::Type::getInt32Ty(ctx), true, llvm::GlobalValue::ExternalLinkage,
				llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 7), "referenced");
			llvm::Constant* field = PeTables::ImageRelative(module, referenced);
			const std::vector<llvm::Type*> fields = { llvm::Type::getInt32Ty(ctx) };
			auto* type = llvm::StructType::get(ctx, fields);
			return PeTables::Private(module, "table", llvm::ConstantStruct::get(type, { field }));
		}

		// `source`'s module lowered for `system`, or null.
		std::unique_ptr<llvm::Module> Lowered(const char* source, TargetSystem system, llvm::LLVMContext& context)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			ProgramModel& model = compiler.GetProgramModelForLowering();
			model.SetTarget(CpuTarget().On(system));
			ProgramBackend driver(context, system);
			std::string error;
			return driver.Lower(model, "main", &error) ? driver.TakeModule() : nullptr;
		}
	}

	void TargetAbiTest::EachSystemHasOneTarget()
	{
		TEST_ASSERT(Windows().System() == TargetSystem::Windows);
		TEST_ASSERT(Linux().System() == TargetSystem::Linux);
		TEST_ASSERT(&Windows() == &TargetAbi::For(TargetSystem::Windows));
		TEST_ASSERT(&Windows() != &Linux());
	}

	void TargetAbiTest::ABuildPicksItsSystemsTarget()
	{
		const CpuTarget host;
		TEST_ASSERT(&host.Abi() == &TargetAbi::For(HostSystem()));
		TEST_ASSERT(&host.On(TargetSystem::Linux).Abi() == &Linux());
		TEST_ASSERT(&host.On(TargetSystem::Windows).Abi() == &Windows());
	}

	void TargetAbiTest::WindowsIsMsvcShaped()
	{
		const TargetAbi& target = Windows();
		TEST_ASSERT_EQ(std::string(target.Triple()), std::string("x86_64-pc-windows-msvc"));
		TEST_ASSERT(target.Convention() == CallConvention::Win64);
		TEST_ASSERT(target.MemberReturnsObjectsInMemory() && !target.SretBeforeThis() && !target.MemoryArgumentsOnStack());
		TEST_ASSERT(!target.VectorPassesInMemory(64, 16)); // a vector crosses by its own convention, register or not
		TEST_ASSERT(target.Exceptions() == ExceptionScheme::Funclets);
		TEST_ASSERT_EQ(std::string(target.Personality()), std::string("__CxxFrameHandler3"));
		TEST_ASSERT(target.DebugInfoFormat() == DebugFormat::CodeView);
		TEST_ASSERT_EQ(std::string(target.ExecutableExtension()), std::string(".exe"));
		TEST_ASSERT_EQ(target.SharedLibrary("C4Script"), std::string("C4Script.dll"));
		TEST_ASSERT(!target.ConstructorsInInitArray() && !target.Relocation());
	}

	void TargetAbiTest::LinuxIsSystemVAndElf()
	{
		const TargetAbi& target = Linux();
		TEST_ASSERT_EQ(std::string(target.Triple()), std::string("x86_64-unknown-linux-gnu"));
		TEST_ASSERT(target.Convention() == CallConvention::SystemV);
		TEST_ASSERT(!target.MemberReturnsObjectsInMemory() && target.SretBeforeThis() && target.MemoryArgumentsOnStack());
		TEST_ASSERT(target.VectorPassesInMemory(32, 16) && !target.VectorPassesInMemory(32, 32) && !target.VectorPassesInMemory(16, 16));
		TEST_ASSERT(target.Exceptions() == ExceptionScheme::LandingPads);
		TEST_ASSERT_EQ(std::string(target.Personality()), std::string("__c4_personality_v0"));
		TEST_ASSERT(target.DebugInfoFormat() == DebugFormat::Dwarf);
		TEST_ASSERT_EQ(std::string(target.ExecutableExtension()), std::string(""));
		TEST_ASSERT_EQ(target.SharedLibrary("C4Script"), std::string("C4Script.so"));
		TEST_ASSERT(target.ConstructorsInInitArray());
		TEST_ASSERT(target.Relocation() == llvm::Reloc::PIC_);
	}

	// C4's sizes and alignments are C's on both: the layouts differ in symbol mangling alone.
	void TargetAbiTest::DataLayoutsDifferOnlyInMangling()
	{
		std::string windows = Windows().DataLayout(), linux = Linux().DataLayout();
		TEST_ASSERT(windows.find("m:w") != std::string::npos && linux.find("m:e") != std::string::npos);
		windows.replace(windows.find("m:w"), 3, "m:?");
		linux.replace(linux.find("m:e"), 3, "m:?");
		TEST_ASSERT_EQ(windows, linux);
	}

	void TargetAbiTest::LinuxModulesArePositionIndependent()
	{
		llvm::LLVMContext ctx;
		llvm::Module windows("w", ctx), linux("l", ctx);
		Windows().ConfigureModule(windows);
		Linux().ConfigureModule(linux);
		TEST_ASSERT(windows.getTargetTriple().isOSWindows() && linux.getTargetTriple().isOSLinux());
		TEST_ASSERT(windows.getPICLevel() == llvm::PICLevel::NotPIC && windows.getPIELevel() == llvm::PIELevel::Default);
		TEST_ASSERT(linux.getPICLevel() == llvm::PICLevel::BigPIC && linux.getPIELevel() == llvm::PIELevel::Large);
	}

	// An ELF image has no __ImageBase: Linux makes each reference relative to its own field; Windows keeps MSVC's.
	void TargetAbiTest::OnlyElfAnchorsTablesAtTheirFields()
	{
		llvm::LLVMContext ctx;
		llvm::Module windows("w", ctx), linux("l", ctx);
		llvm::GlobalVariable* windowsTable = TableReferencingAGlobal(windows);
		TableReferencingAGlobal(linux);
		llvm::Constant* before = windowsTable->getInitializer();
		Windows().AnchorTables(windows);
		Linux().AnchorTables(linux);
		TEST_ASSERT(windows.getGlobalVariable("__ImageBase") && windowsTable->getInitializer() == before);
		TEST_ASSERT(!linux.getGlobalVariable("__ImageBase"));
		TEST_ASSERT(linux.getGlobalVariable("table", true)->getInitializer() != nullptr);
	}

	void TargetAbiTest::OnlyCodeViewLinksADebugFile()
	{
		TEST_ASSERT(Windows().SeparateDebugFile() && !Linux().SeparateDebugFile());
		const std::vector<std::string> pdb = Windows().DebugFileLinkArguments("C:/out/app.pdb.partial", "C:/out/app.pdb");
		TEST_ASSERT(Has(pdb, "-Wl,/DEBUG"));
		TEST_ASSERT(Has(pdb, "-Wl,/PDB:C:/out/app.pdb.partial") && Has(pdb, "-Wl,/PDBALTPATH:C:/out/app.pdb"));
		TEST_ASSERT(Linux().DebugFileLinkArguments("/out/app.partial", "/out/app").empty()); // DWARF stays in the program
	}

	void TargetAbiTest::ScriptHostsLinkTheirSystemsLibrary()
	{
		const std::vector<std::string> windows = Windows().ScriptHostLinkArguments("host");
		TEST_ASSERT(Has(windows, (std::filesystem::path("host") / "C4Script.lib").string()));
		TEST_ASSERT(Has(windows, "-Wl,/DELAYLOAD:C4Script.dll") && Has(windows, "-ldelayimp"));
		const std::vector<std::string> linux = Linux().ScriptHostLinkArguments("host");
		TEST_ASSERT(Has(linux, (std::filesystem::path("host") / "C4Script.so").string()));
		TEST_ASSERT(Has(linux, "-Wl,-rpath,$ORIGIN"));
	}

	void TargetAbiTest::ForeignSourcesTakeTheSysroot()
	{
		const std::vector<std::string> kit = Windows().ForeignSourceFlags("C:/IED/vendor/msvc");
		const auto flag = std::find(kit.begin(), kit.end(), "-Xmicrosoft-windows-sys-root");
		TEST_ASSERT(Has(kit, "-mstackrealign") && flag != kit.end() && flag + 1 != kit.end() && *(flag + 1) == "C:/IED/vendor/msvc");
		TEST_ASSERT(Windows().ForeignSourceFlags("") == std::vector<std::string>{ "-mstackrealign" }); // an installed Visual Studio's
		const std::vector<std::string> glibc = Linux().ForeignSourceFlags("C:/IED/vendor/sysroots/linux-x64");
		TEST_ASSERT(Has(glibc, "-fPIC") && Has(glibc, "--sysroot=C:/IED/vendor/sysroots/linux-x64"));
		TEST_ASSERT(Linux().ForeignSourceFlags("") == std::vector<std::string>{ "-fPIC" }); // the system's own glibc
	}

	void TargetAbiTest::EachTargetBuildsItsOwnRuntimeFiles()
	{
		TEST_ASSERT(Windows().BuildsRuntimeSource("GCPlatformWin32") && !Linux().BuildsRuntimeSource("GCPlatformWin32"));
		TEST_ASSERT(Linux().BuildsRuntimeSource("GCPlatformPosix") && !Windows().BuildsRuntimeSource("GCPlatformPosix"));
		TEST_ASSERT(Linux().BuildsRuntimeSource("WatchLinux") && !Windows().BuildsRuntimeSource("WatchLinux"));
		TEST_ASSERT(Windows().BuildsRuntimeSource("GCHeap") && Linux().BuildsRuntimeSource("GCHeap"));
		TEST_ASSERT(Windows().BuildsRuntimeSource("Win32") && Linux().BuildsRuntimeSource("Linux")); // only a suffix names a system
		TEST_ASSERT(std::string(Windows().RuntimeObjectFolder()) != Linux().RuntimeObjectFolder());
		TEST_ASSERT(std::string(Windows().SysrootRecord()) != Linux().SysrootRecord());
	}

	// BUG-484: the compiler this build installs (bin/<config>/C4, beside C4Testing's folder) links a program for Linux
	// with the ld.lld beside its clang, on Windows too.
	void TargetAbiTest::TheInstallLinksEveryTarget()
	{
		const std::filesystem::path bin = Toolchain::Executable().parent_path().parent_path() / "C4" / "toolchain" / "bin";
		const std::string extension = TargetAbi::For(HostSystem()).ExecutableExtension();
		TEST_ASSERT(std::filesystem::is_regular_file(bin / ("clang" + extension)));
		TEST_ASSERT(std::filesystem::is_regular_file(bin / ("ld.lld" + extension)));
		if (HostSystem() == TargetSystem::Windows) TEST_ASSERT(std::filesystem::is_regular_file(bin / "lld-link.exe"));
	}

	// System V passes a 20-byte aggregate in memory: `byval` on the C function and at its call, where alias analysis reads
	// it (without, an optimized call was taken for one that reads none of the caller's slots and the copy was never filled).
	void TargetAbiTest::Bug499ACallPassesOnTheStackAsItsCalleeSays()
	{
		llvm::LLVMContext context;
		const std::unique_ptr<llvm::Module> module = Lowered(R"(
			struct S20 { public int32 a; public int32 b; public int32 c; public int32 d; public int32 e; }
			[link("SumS20")] int32 SumS20(S20 v) noexcept;
			int32 main() { S20 s; s.a = 1; s.e = 5; return SumS20(s); }
		)", TargetSystem::Linux, context);
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		const llvm::Function* callee = module->getFunction("SumS20");
		TEST_ASSERT(callee && callee->getParamByValType(0) != nullptr);
		size_t calls = 0;
		for (const llvm::Function& function : *module)
			for (const llvm::Instruction& instruction : llvm::instructions(function))
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction); call && call->getCalledFunction() == callee)
				{
					++calls;
					TEST_ASSERT(call->getAttributes().getParamByValType(0) == callee->getParamByValType(0));
					TEST_ASSERT(call->getAttributes().getParamAlignment(0) == callee->getParamAlign(0));
				}
		TEST_ASSERT(calls == 1);
	}
} // namespace Testing
