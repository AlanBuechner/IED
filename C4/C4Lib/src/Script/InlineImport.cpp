#include "Script/InlineImport.h"
#include "Script/ScriptModulePasses.h"
#include <llvm/ADT/StringMap.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/MemoryBuffer.h>
#include <memory>
#include <unordered_set>
#include <vector>

namespace Scripting
{
	namespace
	{
		using Reached = std::unordered_set<const llvm::Function*>;
		using Bodies = llvm::StringMap<llvm::Function*>; // every chunk's bodies, by name

		// The bodies of the functions `user` names.
		void NamedBodies(const llvm::Function& user, const Bodies& bodies, std::vector<llvm::Function*>& out)
		{
			for (const llvm::BasicBlock& block : user)
				for (const llvm::Instruction& instruction : block)
					for (const llvm::Value* operand : instruction.operands())
						if (const auto* named = llvm::dyn_cast<llvm::Function>(operand->stripPointerCasts()))
							if (auto body = bodies.find(named->getName()); body != bodies.end()) out.push_back(body->second);
		}

		// The bodies the script reaches within kInlineImportDepth calls, each read from its chunk.
		llvm::Expected<Reached> ReachedBodies(const llvm::Module& script, const Bodies& bodies)
		{
			std::vector<llvm::Function*> frontier;
			for (const llvm::Function& f : script) NamedBodies(f, bodies, frontier);
			Reached reached;
			for (uint32 depth = 0; depth < kInlineImportDepth && !frontier.empty(); ++depth)
			{
				std::vector<llvm::Function*> next;
				for (llvm::Function* body : frontier)
				{
					if (!reached.insert(body).second) continue;
					if (llvm::Error unread = body->materialize()) return std::move(unread);
					NamedBodies(*body, bodies, next);
				}
				frontier = std::move(next);
			}
			return reached;
		}

		// A body the script reaches stays for inlining only; any other becomes a declaration, so linking reads no more.
		// Whether the chunk keeps one.
		bool KeepOnly(llvm::Module& chunk, const Reached& reached)
		{
			bool kept = false;
			for (llvm::Function& f : chunk)
			{
				if (f.isDeclaration()) continue;
				if (reached.count(&f))
				{
					f.setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
					kept = true;
				}
				else f.deleteBody();
			}
			return kept;
		}

		// Each chunk read lazily into the script's context, its bodies by name in `bodies`.
		bool ReadChunks(llvm::LLVMContext& context, const std::vector<std::string>& chunks, std::vector<std::unique_ptr<llvm::Module>>& read,
			Bodies& bodies, std::string& error)
		{
			for (const std::string& chunk : chunks)
			{
				auto lazy = llvm::getLazyBitcodeModule(llvm::MemoryBufferRef(chunk, "inline bodies"), context);
				if (!lazy)
				{
					error = "the interface's inline bodies do not read: " + llvm::toString(lazy.takeError());
					return false;
				}
				for (llvm::Function& f : **lazy)
					if (!f.isDeclaration()) bodies[f.getName()] = &f;
				read.push_back(std::move(*lazy));
			}
			return true;
		}
	}

	// The chunks that keep a body are linked into one module first, so a body calling into another chunk keeps that
	// body too; that module is then linked into the script, which takes only what it needs.
	bool ImportInlineBodies(llvm::Module& script, const std::vector<std::string>& chunks, std::string& error)
	{
		std::vector<std::unique_ptr<llvm::Module>> read;
		Bodies bodies;
		if (!ReadChunks(script.getContext(), chunks, read, bodies, error)) return false;
		auto reached = ReachedBodies(script, bodies);
		if (!reached)
		{
			error = "the interface's inline bodies do not read: " + llvm::toString(reached.takeError());
			return false;
		}
		std::unique_ptr<llvm::Module> imported;
		for (std::unique_ptr<llvm::Module>& chunk : read)
		{
			if (!KeepOnly(*chunk, *reached)) continue;
			if (!imported) imported = std::move(chunk);
			else if (llvm::Linker::linkModules(*imported, std::move(chunk)))
			{
				error = "the interface's inline bodies do not link together";
				return false;
			}
		}
		if (!imported) return true;
		if (llvm::Linker::linkModules(script, std::move(imported), llvm::Linker::Flags::LinkOnlyNeeded))
		{
			error = "the interface's inline bodies do not link into the script";
			return false;
		}
		UseScriptRuntime(script); // a host body that throws or registers an exit goes through the script's runtime too
		return true;
	}
} // namespace Scripting
