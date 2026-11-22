#pragma once
#include "Core/Core.h"
#include <llvm/ADT/StringMap.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm { class LLVMContext; class Module; }

namespace C4Compiler
{
	/// How many calls deep a script takes the host's inline bodies: the functions it calls, then the ones those call.
	/// Each body taken is work for the optimizer, and one further down is rarely inlined into the script.
	constexpr uint32 kInlineImportDepth = 2;

	/// Which chunk of an interface's inline bodies holds each body, by its name: read once for every script compiled
	/// against the interface, so each reads only the chunks holding the bodies it reaches. Shared by the compiles, from
	/// any thread.
	class InlineBodyIndex
	{
	public:
		/// Where each body of `chunks` is: read at the first call; every call names the same chunks. Null, with why in
		/// `error`, when a chunk does not read.
		const llvm::StringMap<uint32>* Locate(const std::vector<std::string>& chunks, std::string& error);
		/// What the body named `name` is, as a digest of its code with what it names but no debug information: what a
		/// cached script that inlined it checks it against (docs/runtime/04 section 4.6). "" when no chunk holds it, and
		/// when a chunk does not read (`error` says why).
		std::string Digest(const std::string& name, const std::vector<std::string>& chunks, std::string& error);

		InlineBodyIndex();
		~InlineBodyIndex();

	private:
		std::once_flag m_Read;
		llvm::StringMap<uint32> m_ChunkOf;
		std::string m_Error;
		// A chunk read to make digests, in a context of its own: one context gives a second struct type of a name another
		// suffix (`Pair.12`), so a body's text, and its digest, would follow the order the chunks were read in.
		struct ReadChunk
		{
			std::unique_ptr<llvm::LLVMContext> context;
			std::unique_ptr<llvm::Module> module; // destroyed before its context
		};

		// The digests made so far, and the chunks read to make them.
		std::mutex m_Digesting;
		std::unordered_map<std::string, std::string> m_Digests;
		std::vector<ReadChunk> m_Chunks;
	};

	/// Links into `script` the bodies of `chunks` (the interface's inline bodies, Script/InlineBodies) it reaches within
	/// kInlineImportDepth calls, each `available_externally`: kept only for inlining, every call not inlined goes to the
	/// host. A body may call one in another chunk. Only the chunks holding those bodies are read (`index` says which),
	/// and only those bodies of them; `imported` gets their names. False, with the reason in `error`, when a chunk does not
	/// read or link.
	bool ImportInlineBodies(llvm::Module& script, const std::vector<std::string>& chunks, InlineBodyIndex& index,
		std::vector<std::string>& imported, std::string& error);
} // namespace C4Compiler
