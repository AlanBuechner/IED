#include "Script/InlineImport.h"
#include "Script/ScriptModulePasses.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
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

		llvm::Expected<std::unique_ptr<llvm::Module>> ReadLazily(llvm::LLVMContext& context, const std::string& chunk)
		{
			return llvm::getLazyBitcodeModule(llvm::MemoryBufferRef(chunk, "inline bodies"), context);
		}

		std::string Unread(llvm::Error error) { return "the interface's inline bodies do not read: " + llvm::toString(std::move(error)); }

		// A compile's chunks, each read lazily into the script's context the first time a body in it is asked for.
		class ChunkReader
		{
		public:
			ChunkReader(llvm::LLVMContext& context, const std::vector<std::string>& chunks, const llvm::StringMap<uint32>& where)
				: m_Context(context), m_Chunks(chunks), m_Where(where), m_Read(chunks.size()) {}

			// The body named `name`, in the chunk that holds it; null when none does.
			llvm::Expected<llvm::Function*> Body(llvm::StringRef name)
			{
				auto at = m_Where.find(name);
				if (at == m_Where.end()) return nullptr;
				std::unique_ptr<llvm::Module>& chunk = m_Read[at->second];
				if (!chunk)
				{
					auto lazy = ReadLazily(m_Context, m_Chunks[at->second]);
					if (!lazy) return lazy.takeError();
					chunk = std::move(*lazy);
				}
				return chunk->getFunction(name);
			}

			// The chunks read so far, in the interface's order; null for one never asked for.
			std::vector<std::unique_ptr<llvm::Module>>& Read() { return m_Read; }

		private:
			llvm::LLVMContext& m_Context;
			const std::vector<std::string>& m_Chunks;
			const llvm::StringMap<uint32>& m_Where;
			std::vector<std::unique_ptr<llvm::Module>> m_Read;
		};

		// The bodies of the functions `user` names.
		llvm::Error NamedBodies(const llvm::Function& user, ChunkReader& chunks, std::vector<llvm::Function*>& out)
		{
			for (const llvm::BasicBlock& block : user)
				for (const llvm::Instruction& instruction : block)
					for (const llvm::Value* operand : instruction.operands())
						if (const auto* named = llvm::dyn_cast<llvm::Function>(operand->stripPointerCasts()))
						{
							llvm::Expected<llvm::Function*> body = chunks.Body(named->getName());
							if (!body) return body.takeError();
							if (*body && !(*body)->isDeclaration()) out.push_back(*body);
						}
			return llvm::Error::success();
		}

		// The bodies the script reaches within kInlineImportDepth calls, each read from its chunk.
		llvm::Expected<Reached> ReachedBodies(const llvm::Module& script, ChunkReader& chunks)
		{
			std::vector<llvm::Function*> frontier;
			for (const llvm::Function& f : script)
				if (llvm::Error unread = NamedBodies(f, chunks, frontier)) return std::move(unread);
			Reached reached;
			for (uint32 depth = 0; depth < kInlineImportDepth && !frontier.empty(); ++depth)
			{
				std::vector<llvm::Function*> next;
				for (llvm::Function* body : frontier)
				{
					if (!reached.insert(body).second) continue;
					if (llvm::Error unread = body->materialize()) return std::move(unread);
					if (llvm::Error unread = NamedBodies(*body, chunks, next)) return std::move(unread);
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

		// Each body of `chunks` by name, the last chunk's when two hold one, as a script's import finds them.
		bool IndexChunks(const std::vector<std::string>& chunks, llvm::StringMap<uint32>& where, std::string& error)
		{
			llvm::LLVMContext context;
			for (uint32 i = 0; i < chunks.size(); ++i)
			{
				auto lazy = ReadLazily(context, chunks[i]);
				if (!lazy)
				{
					error = Unread(lazy.takeError());
					return false;
				}
				for (const llvm::Function& f : **lazy)
					if (!f.isDeclaration()) where[f.getName()] = i;
			}
			return true;
		}
	}

	const llvm::StringMap<uint32>* InlineBodyIndex::Locate(const std::vector<std::string>& chunks, std::string& error)
	{
		std::call_once(m_Read, [&] { if (!IndexChunks(chunks, m_ChunkOf, m_Error)) m_ChunkOf.clear(); });
		if (m_Error.empty()) return &m_ChunkOf;
		error = m_Error;
		return nullptr;
	}

	// The chunks that keep a body are linked into one module first, so a body calling into another chunk keeps that
	// body too; that module is then linked into the script, which takes only what it needs.
	bool ImportInlineBodies(llvm::Module& script, const std::vector<std::string>& chunks, InlineBodyIndex& index, std::string& error)
	{
		const llvm::StringMap<uint32>* where = index.Locate(chunks, error);
		if (!where) return false;
		ChunkReader reader(script.getContext(), chunks, *where);
		auto reached = ReachedBodies(script, reader);
		if (!reached)
		{
			error = Unread(reached.takeError());
			return false;
		}
		std::unique_ptr<llvm::Module> imported;
		for (std::unique_ptr<llvm::Module>& chunk : reader.Read())
		{
			if (!chunk || !KeepOnly(*chunk, *reached)) continue;
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
