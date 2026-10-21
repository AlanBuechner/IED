#include "Script/InlineImport.h"
#include "Script/ScriptModulePasses.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/MemoryBuffer.h>
#include <unordered_set>
#include <vector>

namespace Scripting
{
	namespace
	{
		using Reached = std::unordered_set<const llvm::Function*>;

		// The bodies `bodies` has for the functions `user` names.
		void NamedBodies(const llvm::Function& user, llvm::Module& bodies, std::vector<llvm::Function*>& out)
		{
			for (const llvm::BasicBlock& block : user)
				for (const llvm::Instruction& instruction : block)
					for (const llvm::Value* operand : instruction.operands())
						if (const auto* named = llvm::dyn_cast<llvm::Function>(operand->stripPointerCasts()))
							if (llvm::Function* body = bodies.getFunction(named->getName()); body && !body->isDeclaration()) out.push_back(body);
		}

		// The bodies the script reaches within kInlineImportDepth calls, each read from the bitcode.
		llvm::Expected<Reached> ReachedBodies(const llvm::Module& script, llvm::Module& bodies)
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
		void KeepOnly(llvm::Module& bodies, const Reached& reached)
		{
			for (llvm::Function& f : bodies)
			{
				if (f.isDeclaration()) continue;
				if (reached.count(&f)) f.setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
				else f.deleteBody();
			}
		}
	}

	bool ImportInlineBodies(llvm::Module& script, const std::string& bitcode, std::string& error)
	{
		if (bitcode.empty()) return true;
		auto lazy = llvm::getLazyBitcodeModule(llvm::MemoryBufferRef(bitcode, "inline bodies"), script.getContext());
		if (!lazy)
		{
			error = "the interface's inline bodies do not read: " + llvm::toString(lazy.takeError());
			return false;
		}
		std::unique_ptr<llvm::Module> bodies = std::move(*lazy);
		auto reached = ReachedBodies(script, *bodies);
		if (!reached)
		{
			error = "the interface's inline bodies do not read: " + llvm::toString(reached.takeError());
			return false;
		}
		KeepOnly(*bodies, *reached);
		if (llvm::Linker::linkModules(script, std::move(bodies), llvm::Linker::Flags::LinkOnlyNeeded))
		{
			error = "the interface's inline bodies do not link into the script";
			return false;
		}
		UseScriptRuntime(script); // a host body that throws or registers an exit goes through the script's runtime too
		return true;
	}
} // namespace Scripting
