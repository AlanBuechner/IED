#pragma once

#include "Core/String.h"
#include "Core/Vector.h"
#include "Target/TargetAbi.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Scripting
{
	// The C4 runtime linked into every executable: each C and C++ source under the install's C4Runtime/ (GC,
	// reflection, threads, tests, EH, IO, RTTI, memory) that RuntimeSourceIsFor the target, compiled by
	// RuntimeCompileCommand into %TEMP%/c4_runtime/<hash of the runtime folder>/<the target's RuntimeObjectFolder>/ under
	// RuntimeObjectName, so an object is reused only for the same bytes of its source and of every runtime header,
	// compiled against the same MSVC and SDK kit or Linux sysroot (TargetAbi::Sysroot: the system's own glibc on Linux
	// without one). Appends the object paths; false (with a message on stderr) when the runtime folder is missing, a
	// build for Linux on Windows has no sysroot, or a source does not compile. A program built with stack maps (`stackMaps`,
	// `-fstack-maps`, R16) links the runtime compiled with C4_STACK_MAPS defined, into a folder of its own beside it, its
	// stack maps' sources (RuntimeSourceIsForStackMaps) among them; any other program, the runtime without them.
	bool EnsureRuntimeObjects(bool verbose, const TargetAbi& target, bool stackMaps, Core::Vector<Core::String>& objects);

	// Whether the runtime source at `relativeSource` ("GC/GCPlatformWin32.c") is part of a program for `target`: a file
	// named for Win32 is Windows', one named for Posix or Linux is Linux's, every other file (the script host's in Script/
	// among them) is every target's (TargetAbi::BuildsRuntimeSource).
	bool RuntimeSourceIsFor(const std::string& relativeSource, const TargetAbi& target);

	// Whether the runtime source at `relativeSource` is a program's with stack maps only: the stack maps' reader and the
	// stack walks (GC/GCStack*).
	bool RuntimeSourceIsForStackMaps(const std::string& relativeSource);

	// FNV-1a over `bytes`, continuing from `seed`: a source's key is its bytes after every header's.
	uint64_t RuntimeContentKey(const std::string& bytes, uint64_t seed);

	// The cached object of `relativeSource` ("GC/GCHeap.c") whose content key is `key`: "GC_GCHeap-<key>.obj". A
	// changed byte is a new name, whatever the files' times say (BUG-231).
	std::string RuntimeObjectName(const std::string& relativeSource, uint64_t key);

	// The folder holding the runtime's sources and headers (the include path of code that uses ReflectABI.h);
	// empty when it cannot be found.
	std::string RuntimeFolder();

	// The clang command for one runtime source for `target`: optimized, warnings are errors, C++ sources as C++20, with
	// the target's flags (TargetAbi::RuntimeSourceFlags): for Windows the MSVC and SDK headers from `sysroot` when it is
	// not empty; for Linux glibc's from the sysroot, compiled position independent; with C4_STACK_MAPS defined for a
	// program built with stack maps.
	std::vector<std::string> RuntimeCompileCommand(const std::string& clang, const std::string& runtimeRoot,
		const std::string& source, const std::string& output, const TargetAbi& target, const std::string& sysroot, bool stackMaps = false);
}
