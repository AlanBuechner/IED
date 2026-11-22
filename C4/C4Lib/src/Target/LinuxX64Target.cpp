#include "Target/AggregateRegisters.h"
#include "Target/Targets.h"
#include "Backend/CodeGen/PeTables.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/FileIo.h"
#include "Compiler/Toolchain.h"
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>
#include <iostream>

namespace C4Compiler
{
	namespace
	{
		// clang's driver argument for `sysroot`: --sysroot, or none for the system's own.
		std::vector<std::string> SysrootFlags(const std::filesystem::path& sysroot)
		{
			if (sysroot.empty()) return {};
			return { "--sysroot=" + sysroot.generic_string() };
		}

		std::vector<std::string> LinkArgument(const std::string& argument) { return { LinuxLinkerArgument(argument) }; }

		class LinuxX64 final : public TargetAbi
		{
		public:
			TargetSystem System() const override { return TargetSystem::Linux; }

			const char* Triple() const override { return "x86_64-unknown-linux-gnu"; }
			// Windows' but for symbol mangling (m:e, not m:w).
			const char* DataLayout() const override { return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"; }
			void ConfigureModule(llvm::Module& module) const override
			{
				module.setTargetTriple(llvm::Triple(Triple()));
				// A position-independent executable: what it defines it reaches directly, the rest through the GOT and PLT.
				module.setPICLevel(llvm::PICLevel::BigPIC);
				module.setPIELevel(llvm::PIELevel::Large);
			}

			CallConvention Convention() const override { return CallConvention::SystemV; }
			llvm::Type* AggregateRegisters(const AggregateShape& aggregate) const override { return SystemVAggregateRegisters(aggregate); }
			// clang passes __m256 without AVX in memory.
			bool VectorPassesInMemory(uint64_t vectorBytes, uint64_t registerBytes) const override { return vectorBytes > registerBytes; }
			bool MemberReturnsObjectsInMemory() const override { return false; }
			bool SretBeforeThis() const override { return true; }
			bool MemoryArgumentsOnStack() const override { return true; }

			ExceptionScheme Exceptions() const override { return ExceptionScheme::LandingPads; }
			// C4Runtime/ExceptionsPosix.c, which reads the same catchable types as MSVC's frame handler.
			const char* Personality() const override { return "__c4_personality_v0"; }
			void AnchorTables(llvm::Module& module) const override { PeTables::AnchorAtFields(module); } // an ELF image has no __ImageBase

			DebugFormat DebugInfoFormat() const override { return DebugFormat::Dwarf; }
			bool SeparateDebugFile() const override { return false; } // DWARF stays in the program
			std::vector<std::string> DebugFileLinkArguments(const std::filesystem::path&, const std::filesystem::path&) const override { return {}; }
			const char* ExecutableExtension() const override { return ""; }
			const char* SharedLibraryExtension() const override { return ".so"; }
			// ELF startup runs .init_array; nothing runs the old .ctors LLVM writes otherwise (clang sets this for Linux).
			bool ConstructorsInInitArray() const override { return true; }
			// A PIE, as clang builds one by default.
			std::optional<llvm::Reloc::Model> Relocation() const override { return llvm::Reloc::PIC_; }

			// Toolchain::LinuxSysroot, else, for a compiler that runs on Linux, an empty path (clang's driver takes the system's
			// own glibc).
			std::optional<std::filesystem::path> Sysroot() const override
			{
				const std::filesystem::path sysroot = Toolchain::LinuxSysroot();
				if (!sysroot.empty() || HostSystem() == TargetSystem::Linux) return sysroot;
				std::cerr << "error: the Linux sysroot was not found: C4 looks in <C4 home>/sysroots/linux-x64 (the IED checkout's "
					"tools/setup/linux_sysroot.py makes vendor/sysroots/linux-x64)" << std::endl;
				return std::nullopt;
			}
			const char* SysrootRecord() const override { return "sysroot.txt"; }
			std::vector<std::string> ForeignSourceFlags(const std::filesystem::path& sysroot) const override
			{
				std::vector<std::string> flags = { "-fPIC" };
				const std::vector<std::string> root = SysrootFlags(sysroot);
				flags.insert(flags.end(), root.begin(), root.end());
				return flags;
			}
			std::vector<std::string> RuntimeSourceFlags(const std::string& sysroot) const override
			{
				std::vector<std::string> flags = { "-fPIC", "-D_GNU_SOURCE" };
				if (!sysroot.empty()) flags.push_back("--sysroot=" + sysroot);
				return flags;
			}
			bool BuildsRuntimeSource(const std::string& stem) const override { return !NamedFor(stem, "Win32"); }
			const char* RuntimeObjectFolder() const override { return "linux-x64-O2"; }

			// Linked by its file and found beside the program as it starts ($ORIGIN): Linux has no delay-loading.
			std::vector<std::string> ScriptHostLinkArguments(const std::filesystem::path& folder) const override
			{
				return { (folder / SharedLibrary("C4Script")).string(), "-Wl,-rpath,$ORIGIN" };
			}

			// clang's driver as g++ links: glibc's startup files and libraries from the sysroot (or the system's), GCC's C++
			// runtime linked in, ld.lld beside clang. The unwinder is GCC's shared one (libgcc_s.so.1, which every glibc system
			// has: glibc itself loads it), one for the program, the libraries it loads and the script frames a script host
			// registers with it, so an exception unwinds through all of them. A runtime object is linked only when the program
			// uses it (as an archive's member is), and a system library only when something links against it.
			bool Link(const LinkJob& job) const override
			{
				const std::string clang = ClangPath();
				if (clang.empty()) return false;
				if (Toolchain::LdLld().empty())
				{
					std::cerr << "error: ld.lld, which links a Linux program, was not found beside clang (" << clang << ")" << std::endl;
					return false;
				}
				const std::optional<std::filesystem::path> sysroot = Sysroot();
				if (!sysroot) return false;
				const std::string inputs = std::string(job.output.c_str()) + ".link.rsp";
				Core::Vector<Core::String> lazy = job.objects;
				lazy.Push("-Wl,--start-lib");
				for (const Core::String& object : job.runtime) lazy.Push(object);
				lazy.Push("-Wl,--end-lib");
				if (!WriteLinkInputs(lazy, job.libraries, &LinkArgument, inputs)) return false;
				std::vector<std::string> args = { clang, "--driver-mode=g++", "--target=" + std::string(Triple()) };
				const std::vector<std::string> sysrootFlags = SysrootFlags(*sysroot);
				args.insert(args.end(), sysrootFlags.begin(), sysrootFlags.end());
				args.insert(args.end(), { "-fuse-ld=lld", "-pie", "-static-libstdc++", "-shared-libgcc",
					"-o", job.output.c_str(), "@" + inputs, "-Wl,--as-needed", "-lpthread", "-ldl", "-lm" });
				if (job.verbose)
				{
					std::cout << "Linking: ";
					for (const auto& a : args) std::cout << a << " ";
					std::cout << std::endl;
				}
				const bool linked = RunTool(args);
				RemoveFile(inputs);
				return linked;
			}
		};
	}

	const TargetAbi& LinuxX64Target()
	{
		static const LinuxX64 target;
		return target;
	}
} // namespace C4Compiler
