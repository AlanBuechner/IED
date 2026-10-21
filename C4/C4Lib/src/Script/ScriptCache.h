#pragma once
#include "Core/Core.h"
#include "Script/HostInterface.h"
#include <optional>
#include <string>
#include <vector>

namespace Scripting
{
	struct ScriptCompileOptions;

	/// What a compile left for the next one with the same inputs: its machine code and what it said.
	struct CachedScript
	{
		std::string object;                    // the COFF object the JIT generated
		std::string diagnostics;               // its warnings
		std::vector<std::string> threadLocals; // as CompiledScript's
	};

	/// Compiled scripts kept in a folder (docs/runtime/04 section 4.6): a script whose files, options, host build and
	/// compiler are those of an earlier compile links from that compile's machine code. One entry is kept per script
	/// name and optimization level; writing one removes the entry it replaces.
	class ScriptCache
	{
	public:
		/// `compiler` names the build of the compiler (ScriptCompilerIdentity): code another build made is never used.
		ScriptCache(std::string folder, std::string compiler) : m_Folder(std::move(folder)), m_Compiler(std::move(compiler)) {}

		/// The entry a compile of `options` against `host` would have: a file of the folder named for the script and
		/// a digest of every input that decides its code. Empty when a script file does not read.
		std::string EntryFor(const HostInterface& host, const ScriptCompileOptions& options) const;
		/// The entry's compile, or nothing when there is none or it does not read whole.
		std::optional<CachedScript> Read(const std::string& entry) const;
		/// Keeps `script` as the entry and removes the script's other entries. A cache that cannot be written is only
		/// slower: failures are ignored.
		void Write(const std::string& entry, const CachedScript& script) const;

	private:
		std::string m_Folder;
		std::string m_Compiler;
	};

	/// The build of the compiler running this code: the path, size and time of the file it was loaded from.
	std::string ScriptCompilerIdentity();
} // namespace Scripting
