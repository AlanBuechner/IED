#pragma once
#include "Compiler/TargetSystem.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include <llvm/ADT/STLFunctionalExtras.h>
#include <llvm/Support/CodeGen.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace llvm
{
	class DataLayout;
	class LLVMContext;
	class Module;
	class Type;
}

namespace Scripting
{
	/// The Windows subsystem an executable runs in: a console program, or a windowed one that gets no console (its
	/// entry is still `main`).
	enum class Subsystem { Console, Windows };

	/// How arguments cross a call: the Windows x64 convention or System V's x86-64 psABI.
	enum class CallConvention { Win64, SystemV };
	/// How a function's exceptional exits are lowered: MSVC funclets (catchswitch, catchpad, cleanuppad) or landing pads.
	enum class ExceptionScheme { Funclets, LandingPads };
	/// The debug information `-g` writes: CodeView (linked into a .pdb) or DWARF (kept in the program).
	enum class DebugFormat { CodeView, Dwarf };

	/// A by-value aggregate as a calling convention classifies it: its size, and its LLVM storage type laid out by
	/// `layout`, made only when the convention looks at the scalars it holds.
	struct AggregateShape
	{
		llvm::LLVMContext& context;
		const llvm::DataLayout& layout;
		uint64_t size = 0;
		llvm::function_ref<llvm::Type*()> storage;
	};

	/// What a program's link takes.
	struct LinkJob
	{
		const Core::Vector<Core::String>& objects; // the program's and the foreign C/C++ objects
		const Core::Vector<Core::String>& runtime; // the C4 runtime's
		const Core::Vector<Core::String>& libraries; // paths, -l / -L / -Wl arguments and what the target's link added
		const Core::String& output;
		Subsystem subsystem = Subsystem::Console;
		bool verbose = false;
	};

	/// The target a build compiles for (R11, docs/compiler/04): every answer that differs between targets - the calling
	/// convention, how exceptions unwind and what the RTTI and EH tables reference, the debug format, the object format,
	/// the runtime and the link. Picked once per build from its TargetSystem (CpuTarget::Abi, ModuleContext::Target); an
	/// emitter asks it instead of branching on the system, so another target is one more implementation.
	class TargetAbi
	{
	public:
		/// The target of a program for `system`.
		static const TargetAbi& For(TargetSystem system);

		virtual ~TargetAbi() = default;
		virtual TargetSystem System() const = 0;

		// --- the module ------------------------------------------------------------------
		/// The LLVM triple.
		virtual const char* Triple() const = 0;
		/// The data layout clang uses for the triple: C4's sizes and alignments agree with C's on it.
		virtual const char* DataLayout() const = 0;
		/// Gives a new module the triple and what it is built as (a position-independent executable on Linux).
		virtual void ConfigureModule(llvm::Module& module) const = 0;

		// --- calling convention ----------------------------------------------------------
		virtual CallConvention Convention() const = 0;
		/// The registers an aggregate crosses a call in, as the LLVM type that holds them; null when it goes in memory
		/// (a hidden pointer, an sret return).
		virtual llvm::Type* AggregateRegisters(const AggregateShape& aggregate) const = 0;
		/// Whether a packed vector of `vectorBytes` passes in memory when the widest vector register holds
		/// `registerBytes` (System V); Win64 passes every vector by its own convention.
		virtual bool VectorPassesInMemory(uint64_t vectorBytes, uint64_t registerBytes) const = 0;
		/// Whether a member function returns every object through sret, whatever its size (MSVC's member rule).
		virtual bool MemberReturnsObjectsInMemory() const = 0;
		/// Whether the sret pointer comes before `this` (Itanium) rather than after it (MSVC).
		virtual bool SretBeforeThis() const = 0;
		/// Whether an aggregate passed in memory to a C function is a copy on the stack (`byval`) rather than a pointer to
		/// the caller's temporary.
		virtual bool MemoryArgumentsOnStack() const = 0;

		// --- exceptions and the RTTI and EH tables ---------------------------------------
		virtual ExceptionScheme Exceptions() const = 0;
		/// The personality routine of a function that catches or cleans up.
		virtual const char* Personality() const = 0;
		/// Once a module's tables are complete: their 32-bit references made what the target's runtime reads -
		/// image-relative as MSVC's on Windows, relative to their own field on ELF (PeTables::AnchorAtFields).
		virtual void AnchorTables(llvm::Module& module) const = 0;

		// --- debug information and objects -----------------------------------------------
		virtual DebugFormat DebugInfoFormat() const = 0;
		/// Whether `-g` writes a file of its own beside the program (a .pdb), which the link writes.
		virtual bool SeparateDebugFile() const = 0;
		/// The link's arguments that write it: under `partial`, naming `final` in the program.
		virtual std::vector<std::string> DebugFileLinkArguments(const std::filesystem::path& partial,
			const std::filesystem::path& final) const = 0;
		/// `.exe` or nothing.
		virtual const char* ExecutableExtension() const = 0;
		/// `.dll` or `.so`.
		virtual const char* SharedLibraryExtension() const = 0;
		/// Whether static constructors go in `.init_array` (ELF) rather than the object format's own list.
		virtual bool ConstructorsInInitArray() const = 0;
		/// The relocation model objects are built with; nothing for the target's default.
		virtual std::optional<llvm::Reloc::Model> Relocation() const = 0;

		// --- C and C++ sources, the runtime and the link ---------------------------------
		/// The headers and libraries of the target's C library: the MSVC and SDK kit (empty: an installed Visual
		/// Studio's), or the Linux sysroot (empty: the system's own, on Linux). Nothing, after printing where it was
		/// looked for, when the target needs one this install does not have.
		virtual std::optional<std::filesystem::path> Sysroot() const = 0;
		/// The file in a sysroot that records what it holds: a changed one compiles the runtime again.
		virtual const char* SysrootRecord() const = 0;
		/// clang's flags for a foreign C or C++ source of the program, against `sysroot`.
		virtual std::vector<std::string> ForeignSourceFlags(const std::filesystem::path& sysroot) const = 0;
		/// clang's flags for a C4 runtime source, against `sysroot`.
		virtual std::vector<std::string> RuntimeSourceFlags(const std::string& sysroot) const = 0;
		/// Whether a runtime source whose file name (without its extension) is `stem` is part of the target's runtime:
		/// every source but the ones named for another system (`...Win32`, `...Posix`, `...Linux`).
		virtual bool BuildsRuntimeSource(const std::string& stem) const = 0;
		/// The folder of the runtime's object cache that holds the target's objects.
		virtual const char* RuntimeObjectFolder() const = 0;
		/// The link's arguments for a script host, whose script host library is in `folder`.
		virtual std::vector<std::string> ScriptHostLinkArguments(const std::filesystem::path& folder) const = 0;
		/// Links an executable; false after printing why.
		virtual bool Link(const LinkJob& job) const = 0;

		/// `name` as a shared library of the target names it: `C4Script.dll`, `C4Script.so`.
		std::string SharedLibrary(const char* name) const { return std::string(name) + SharedLibraryExtension(); }
	};
} // namespace Scripting
