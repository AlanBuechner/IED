#pragma once
#include "Core/Core.h"
#include "Script/HostAssumptions.h"
#include "Script/HostInterface.h"
#include <optional>
#include <string>
#include <vector>

namespace C4Compiler
{
	struct ScriptCompileOptions;

	/// What a compile left for the next one with the same inputs: its machine code and what it said.
	struct CachedScript
	{
		std::string object;                    // the COFF object the JIT generated
		std::string diagnostics;               // its warnings
		std::vector<std::string> threadLocals; // as CompiledScript's
		std::string layer;                     // an extensible script's layer (SerializeLayer); empty for none
		HostAssumptions assumptions;           // what its code assumed of its host, checked before it is used
	};

	/// What code compiled over a layer depends on: its interface and those below it, not its bodies (section 5.2). A script
	/// over it whose layer's digest is the same links its kept machine code to the new layer.
	std::string LayerDigest(const std::string& below, const std::string& serializedLayer);

	/// Compiled scripts kept in a folder (docs/runtime/04 section 4.6): a script whose files, options, compiler and what
	/// it reads of its host - the interface files its project reaches, by their digests (InterfaceDigests) - are those of
	/// an earlier compile links from that compile's machine code, once that compile's HostAssumptions hold. A host build
	/// that changes no file a script reads, beyond its function bodies, keeps its entry. One entry is kept per script name
	/// (without a trailing `#<tag>`, as `go#7` is `go`) and optimization level; writing one removes the entry it replaces.
	class ScriptCache
	{
	public:
		/// `compiler` names the build of the compiler (ScriptCompilerIdentity): code another build made is never used.
		ScriptCache(std::string folder, std::string compiler) : m_Folder(std::move(folder)), m_Compiler(std::move(compiler)) {}

		/// The entry a compile of `options` against `host` would have: a file of the folder named for the script and
		/// a digest of every input that decides its code, `digests` the host's files'. Empty when a script file does not
		/// read.
		std::string EntryFor(const HostInterface& host, InterfaceDigests& digests, const ScriptCompileOptions& options) const;
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
} // namespace C4Compiler
