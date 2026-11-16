#pragma once
#include "Core/Core.h"
#include <llvm/ADT/StringMap.h>
#include <mutex>
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
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

	private:
		std::once_flag m_Read;
		llvm::StringMap<uint32> m_ChunkOf;
		std::string m_Error;
	};

	/// Links into `script` the bodies of `chunks` (the interface's inline bodies, Script/InlineBodies) it reaches within
	/// kInlineImportDepth calls, each `available_externally`: kept only for inlining, every call not inlined goes to the
	/// host. A body may call one in another chunk. Only the chunks holding those bodies are read (`index` says which),
	/// and only those bodies of them. False, with the reason in `error`, when a chunk does not read or link.
	bool ImportInlineBodies(llvm::Module& script, const std::vector<std::string>& chunks, InlineBodyIndex& index, std::string& error);
} // namespace Scripting
