#pragma once

#include "Core/String.h"
#include "Core/Vector.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Scripting
{
	// The C4 runtime linked into every executable: each C and C++ source under the install's C4Runtime/ (GC,
	// reflection, threads, tests, EH, IO, RTTI, memory), compiled by RuntimeCompileCommand into
	// %TEMP%/c4_runtime/<hash of the runtime folder>/O2/ under RuntimeObjectName, so an object is reused only for
	// the same bytes of its source and of every runtime header. Appends the object paths; false (with a message on
	// stderr) when the runtime folder is missing or a source does not compile.
	bool EnsureRuntimeObjects(bool verbose, Core::Vector<Core::String>& objects);

	// FNV-1a over `bytes`, continuing from `seed`: a source's key is its bytes after every header's.
	uint64_t RuntimeContentKey(const std::string& bytes, uint64_t seed);

	// The cached object of `relativeSource` ("GC/GCHeap.c") whose content key is `key`: "GC_GCHeap-<key>.obj". A
	// changed byte is a new name, whatever the files' times say (BUG-231).
	std::string RuntimeObjectName(const std::string& relativeSource, uint64_t key);

	// The folder holding the runtime's sources and headers (the include path of code that uses ReflectABI.h);
	// empty when it cannot be found.
	std::string RuntimeFolder();

	// The clang command for one runtime source: optimized, warnings are errors, C++ sources as C++20.
	std::vector<std::string> RuntimeCompileCommand(const std::string& clang, const std::string& runtimeRoot,
		const std::string& source, const std::string& output);
}
