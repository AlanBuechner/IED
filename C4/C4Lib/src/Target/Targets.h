#pragma once
#include "Target/TargetAbi.h"

namespace C4Compiler
{
	/// x86_64-pc-windows-msvc: PE/COFF, the Windows x64 convention, MSVC's object model and funclet exceptions,
	/// CodeView in a .pdb, lld-link against the MSVC runtime.
	const TargetAbi& WindowsX64Target();

	/// x86_64-unknown-linux-gnu (D-86): an ELF PIE, System V, the MSVC-shaped tables anchored at their fields, landing
	/// pads on the runtime's personality, DWARF 5, clang and ld.lld against glibc.
	const TargetAbi& LinuxX64Target();

	/// Whether a runtime source's file name (without its extension) `stem` is named for a system: ends in `suffix`.
	bool NamedFor(const std::string& stem, const char* suffix);

	/// The objects, then the libraries as `library` turns each into the link's arguments, one quoted argument a line
	/// with forward slashes (which neither quoting convention escapes), in a response file at `path`; false after
	/// printing why.
	bool WriteLinkInputs(const Core::Vector<Core::String>& objects, const Core::Vector<Core::String>& libraries,
		std::vector<std::string> (*library)(const std::string& argument), const std::string& path);
} // namespace C4Compiler
