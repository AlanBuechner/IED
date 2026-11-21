#include "Target/AggregateRegisters.h"
#include "Target/Targets.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/FileIo.h"
#include "Compiler/Toolchain.h"
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>
#include <iostream>

namespace Scripting
{
	namespace
	{
		// clang's driver arguments that take the MSVC runtime's and Windows SDK's headers and libraries from `sysroot`
		// (Toolchain::WindowsSysroot) instead of an installed Visual Studio; none when it is empty.
		std::vector<std::string> KitFlags(const std::string& sysroot)
		{
			if (sysroot.empty()) return {};
			return { "-Xmicrosoft-windows-sys-root", sysroot };
		}

		void Print(const char* what, const std::vector<std::string>& args)
		{
			std::cout << what;
			for (const auto& a : args) std::cout << a << " ";
			std::cout << std::endl;
		}

		class WindowsX64 final : public TargetAbi
		{
		public:
			TargetSystem System() const override { return TargetSystem::Windows; }

			const char* Triple() const override { return "x86_64-pc-windows-msvc"; }
			const char* DataLayout() const override { return "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"; }
			void ConfigureModule(llvm::Module& module) const override { module.setTargetTriple(llvm::Triple(Triple())); }

			CallConvention Convention() const override { return CallConvention::Win64; }
			llvm::Type* AggregateRegisters(const AggregateShape& aggregate) const override { return Win64AggregateRegisters(aggregate); }
			bool VectorPassesInMemory(uint64_t, uint64_t) const override { return false; }
			bool MemberReturnsObjectsInMemory() const override { return true; }
			bool SretBeforeThis() const override { return false; }
			bool MemoryArgumentsOnStack() const override { return false; }

			ExceptionScheme Exceptions() const override { return ExceptionScheme::Funclets; }
			const char* Personality() const override { return "__CxxFrameHandler3"; }
			void AnchorTables(llvm::Module&) const override {} // image-relative, as the MSVC runtime reads them

			DebugFormat DebugInfoFormat() const override { return DebugFormat::CodeView; }
			bool SeparateDebugFile() const override { return true; }
			std::vector<std::string> DebugFileLinkArguments(const std::filesystem::path& partial, const std::filesystem::path& final) const override
			{
				return { "-Wl,/DEBUG", "-Wl,/PDB:" + partial.string(), "-Wl,/PDBALTPATH:" + final.string() };
			}
			const char* ExecutableExtension() const override { return ".exe"; }
			const char* SharedLibraryExtension() const override { return ".dll"; }
			bool ConstructorsInInitArray() const override { return false; }
			std::optional<llvm::Reloc::Model> Relocation() const override { return std::nullopt; }

			std::optional<std::filesystem::path> Sysroot() const override { return Toolchain::WindowsSysroot(); }
			const char* SysrootRecord() const override { return ".setup.json"; }
			std::vector<std::string> ForeignSourceFlags(const std::filesystem::path& sysroot) const override
			{
				std::vector<std::string> flags = { "-mstackrealign" };
				const std::vector<std::string> kit = KitFlags(sysroot.string());
				flags.insert(flags.end(), kit.begin(), kit.end());
				return flags;
			}
			std::vector<std::string> RuntimeSourceFlags(const std::string& sysroot) const override
			{
				std::vector<std::string> flags = { "-mstackrealign", "-D_CRT_SECURE_NO_WARNINGS" };
				const std::vector<std::string> kit = KitFlags(sysroot);
				flags.insert(flags.end(), kit.begin(), kit.end());
				return flags;
			}
			bool BuildsRuntimeSource(const std::string& stem) const override { return !NamedFor(stem, "Posix") && !NamedFor(stem, "Linux"); }
			const char* RuntimeObjectFolder() const override { return "O2"; }

			std::vector<std::string> ScriptHostLinkArguments(const std::filesystem::path& folder) const override
			{
				// Loaded on the first call, so a program without the DLL beside it starts and Host::Open says so.
				return { (folder / "C4Script.lib").string(), "-Wl,/DELAYLOAD:" + SharedLibrary("C4Script"), "-ldelayimp" };
			}

			// lld-link itself, as clang's driver would run it: it takes the MSVC and Windows SDK libraries from setup.bat's kit
			// (Toolchain::WindowsSysroot), else finds them on its own (or through LIB when that is set), and one process less
			// starts. The objects and libraries go in a response file: a split program's object paths outgrow a Windows
			// command line (32,767 characters).
			bool Link(const LinkJob& job) const override
			{
				const std::filesystem::path lld = Toolchain::LldLink();
				if (lld.empty())
				{
					std::cerr << "error: lld-link.exe was not found beside clang.exe (" << ClangPath() << ")" << std::endl;
					return false;
				}
				const std::string inputs = std::string(job.output.c_str()) + ".link.rsp";
				Core::Vector<Core::String> objects = job.objects;
				for (const Core::String& object : job.runtime) objects.Push(object);
				if (!WriteLinkInputs(objects, job.libraries, &LinkerArguments, inputs)) return false;
				std::vector<std::string> args = { lld.string(), "-nologo", "-out:" + std::string(job.output.c_str()), "-defaultlib:libcmt",
					"-defaultlib:oldnames" };
				if (const std::filesystem::path builtins = Toolchain::ClangRuntimeLibraries(); !builtins.empty()) args.push_back("-libpath:" + builtins.string());
				if (const std::filesystem::path sysroot = Toolchain::WindowsSysroot(); !sysroot.empty()) args.push_back("/winsysroot:" + sysroot.string());
				args.push_back("@" + inputs);
				if (job.subsystem == Subsystem::Windows)
				{
					args.push_back("/SUBSYSTEM:WINDOWS");
					args.push_back("/ENTRY:mainCRTStartup"); // the C runtime's console entry, which calls main, not WinMain
				}
				else args.push_back("/SUBSYSTEM:CONSOLE");
				if (job.verbose) Print("Linking: ", args);
				const bool linked = RunTool(args);
				RemoveFile(inputs);
				return linked;
			}
		};
	}

	const TargetAbi& WindowsX64Target()
	{
		static const WindowsX64 target;
		return target;
	}
} // namespace Scripting
