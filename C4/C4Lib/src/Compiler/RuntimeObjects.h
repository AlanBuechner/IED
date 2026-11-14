#pragma once

#include "Compiler/TargetSystem.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Scripting
{
	// The C4 runtime linked into every executable: each C and C++ source under the install's C4Runtime/ (GC,
	// reflection, threads, tests, EH, IO, RTTI, memory) that RuntimeSourceIsFor the target's system, compiled by
	// RuntimeCompileCommand into %TEMP%/c4_runtime/<hash of the runtime folder>/<system>/ under RuntimeObjectName, so an
	// object is reused only for the same bytes of its source and of every runtime header, compiled against the same
	// MSVC and SDK kit (Toolchain::WindowsSysroot) or Linux sysroot (LinuxSysrootForBuild: the system's own glibc on
	// Linux without one). Appends the object paths; false (with a message on stderr) when the runtime folder is missing,
	// a build for Linux on Windows has no sysroot, or a source does not compile.
	bool EnsureRuntimeObjects(bool verbose, TargetSystem system, Core::Vector<Core::String>& objects);

	// Whether the runtime source at `relativeSource` ("GC/GCPlatformWin32.c") is part of a program for `system`: a file
	// named for Win32 is Windows', one named for Posix or Linux is Linux's, every other file (the script host's in Script/
	// among them) is both systems'.
	bool RuntimeSourceIsFor(const std::string& relativeSource, TargetSystem system);

	// FNV-1a over `bytes`, continuing from `seed`: a source's key is its bytes after every header's.
	uint64_t RuntimeContentKey(const std::string& bytes, uint64_t seed);

	// The cached object of `relativeSource` ("GC/GCHeap.c") whose content key is `key`: "GC_GCHeap-<key>.obj". A
	// changed byte is a new name, whatever the files' times say (BUG-231).
	std::string RuntimeObjectName(const std::string& relativeSource, uint64_t key);

	// The folder holding the runtime's sources and headers (the include path of code that uses ReflectABI.h);
	// empty when it cannot be found.
	std::string RuntimeFolder();

	// The clang command for one runtime source for `system`: optimized, warnings are errors, C++ sources as C++20. For
	// Windows the MSVC and SDK headers come from `sysroot` when it is not empty (Toolchain::WindowsSysroot); for Linux
	// glibc's from the sysroot (Toolchain::LinuxSysroot), compiled position independent.
	std::vector<std::string> RuntimeCompileCommand(const std::string& clang, const std::string& runtimeRoot,
		const std::string& source, const std::string& output, TargetSystem system, const std::string& sysroot);
}
