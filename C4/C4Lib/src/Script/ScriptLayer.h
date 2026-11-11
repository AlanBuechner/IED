#pragma once
#include "Core/Core.h"
#include "Compiler/CpuTarget.h"
#include "Script/HostInterface.h"
#include "Script/ScriptJit.h"
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Scripting
{
	/// A host interface's lookups, made once for every compile against it.
	struct HostIndex
	{
		explicit HostIndex(const HostInterface& host);
		std::unordered_set<std::string> symbols; // the table's entries
		std::unordered_set<std::string> types;   // the types the host's reflection registered
		CpuTarget target;                        // the host's processor: every script compiles for it (D-62)
	};

	/// What a script compiled to be extended leaves for the scripts over it (docs/runtime/04 section 5.2): to them its
	/// files are as the host's are, and what it defines resolves as the host's table does.
	struct ScriptLayer
	{
		std::string project;                     // the project its files are in, which an extender's allow-list names
		std::vector<std::string> folders;        // the folders includes of its files resolve from, on disk
		std::vector<InterfaceFile> files;        // `folder` indexes `folders`, -1 for a file given as text; `project` is unused
		std::vector<std::string> symbols;        // what it defines and a script over it may name
		std::vector<std::string> reflectedTypes; // the types it registered that nothing below it did
	};

	/// A layer as bytes, for the script cache (section 4.6).
	std::string SerializeLayer(const ScriptLayer& layer);
	bool DeserializeLayer(std::string_view bytes, ScriptLayer& layer);

	/// One linked script of a chain, and the host as those over it see it: the interface and the table with this layer
	/// and every one below. Immutable once made; each script over it holds it, so what it linked to outlives it.
	struct ScriptChain
	{
		ScriptChain(HostIndex index, HostSymbolTable table) : index(std::move(index)), table(std::move(table)) {}

		std::shared_ptr<const ScriptChain> parent;   // the layer below; null directly over the host
		ScriptLayer layer;
		std::string digest;                          // this layer's interface and those below: what code over it depends on
		HostIndex index;
		HostSymbolTable table;
		std::shared_ptr<std::atomic<int64>> extenders = std::make_shared<std::atomic<int64>>(0); // the scripts directly over it that have not unloaded
	};

	/// `top` and the layers below it, the lowest first.
	inline std::vector<const ScriptChain*> LayersOf(const ScriptChain* top)
	{
		std::vector<const ScriptChain*> layers;
		for (const ScriptChain* at = top; at; at = at->parent.get()) layers.insert(layers.begin(), at);
		return layers;
	}
} // namespace Scripting
