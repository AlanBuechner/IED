#include "Script/InlineImport.h"
#include "Script/ScriptModulePasses.h"
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/BLAKE3.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <memory>
#include <unordered_set>
#include <vector>

namespace C4Compiler
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

		std::string Hex(const llvm::BLAKE3Result<16>& sum)
		{
			static constexpr char kDigits[] = "0123456789abcdef";
			std::string hex;
			for (uint8_t byte : sum) { hex += kDigits[byte >> 4]; hex += kDigits[byte & 15]; }
			return hex;
		}

		bool KeptWithBody(const llvm::GlobalValue& global)
		{
			const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(&global);
			return variable && variable->hasLocalLinkage() && variable->hasInitializer();
		}

		// The globals `value` names, through constants and the initializers of private variables, each once, in order.
		void NamedGlobals(llvm::Value* value, std::vector<llvm::GlobalValue*>& named, llvm::SmallPtrSetImpl<const llvm::Value*>& seen)
		{
			auto* constant = llvm::dyn_cast_or_null<llvm::Constant>(value);
			if (!constant || !seen.insert(constant).second) return;
			if (auto* global = llvm::dyn_cast<llvm::GlobalValue>(constant))
			{
				named.push_back(global);
				if (KeptWithBody(*global)) NamedGlobals(llvm::cast<llvm::GlobalVariable>(global)->getInitializer(), named, seen);
				return;
			}
			for (llvm::Value* operand : constant->operands()) NamedGlobals(operand, named, seen);
		}

		std::vector<llvm::GlobalValue*> NamedGlobals(llvm::Function& body)
		{
			std::vector<llvm::GlobalValue*> named;
			llvm::SmallPtrSet<const llvm::Value*, 32> seen;
			if (body.hasPersonalityFn()) NamedGlobals(body.getPersonalityFn(), named, seen);
			if (body.hasPrefixData()) NamedGlobals(body.getPrefixData(), named, seen);
			if (body.hasPrologueData()) NamedGlobals(body.getPrologueData(), named, seen);
			for (llvm::BasicBlock& block : body)
				for (llvm::Instruction& instruction : block)
					for (llvm::Value* operand : instruction.operands()) NamedGlobals(operand, named, seen);
			return named;
		}

		// `global` as `alone` names it: a declaration, or a private variable with its type, to take its initializer after.
		llvm::GlobalValue* Declared(llvm::Module& alone, const llvm::GlobalValue& global)
		{
			if (const auto* function = llvm::dyn_cast<llvm::Function>(&global))
			{
				llvm::Function* declared = llvm::Function::Create(function->getFunctionType(), llvm::GlobalValue::ExternalLinkage,
					function->getAddressSpace(), function->getName(), &alone);
				declared->setAttributes(function->getAttributes());
				declared->setCallingConv(function->getCallingConv());
				return declared;
			}
			const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(&global);
			auto* declared = new llvm::GlobalVariable(alone, global.getValueType(), variable && variable->isConstant(),
				KeptWithBody(global) ? global.getLinkage() : llvm::GlobalValue::ExternalLinkage, nullptr, global.getName(), nullptr,
				global.getThreadLocalMode(), global.getAddressSpace());
			if (variable) declared->setAlignment(variable->getAlign());
			return declared;
		}

		// `body` alone in a module of its own - what it names declared, the private constants it reads with it - without
		// debug information, as text: the same for the same code whatever else its chunk holds. Built from the body and
		// what it names alone, not by cloning its chunk (thousands of declarations for each of a hundred bodies), so a
		// cached script's check of the bodies it took costs little. Strips `body`'s own debug information.
		std::string TextAlone(llvm::Function& body)
		{
			const llvm::Module& chunk = *body.getParent();
			llvm::stripDebugInfo(body);
			llvm::Module alone("inline body", body.getContext()); // the chunk's own name says nothing of the body
			alone.setSourceFileName("inline body");
			alone.setDataLayout(chunk.getDataLayout());
			alone.setTargetTriple(chunk.getTargetTriple());
			llvm::ValueToValueMapTy map;
			llvm::Function* copy = llvm::Function::Create(body.getFunctionType(), body.getLinkage(), body.getAddressSpace(), body.getName(), &alone);
			map[&body] = copy;
			const std::vector<llvm::GlobalValue*> named = NamedGlobals(body);
			for (llvm::GlobalValue* global : named)
				if (global != &body) map[global] = Declared(alone, *global);
			for (llvm::GlobalValue* global : named)
				if (global != &body && KeptWithBody(*global))
					llvm::cast<llvm::GlobalVariable>(map[global])->setInitializer(llvm::MapValue(llvm::cast<llvm::GlobalVariable>(global)->getInitializer(), map));
			auto argument = copy->arg_begin();
			for (llvm::Argument& original : body.args())
			{
				argument->setName(original.getName());
				map[&original] = &*argument++;
			}
			llvm::SmallVector<llvm::ReturnInst*, 8> returns;
			llvm::CloneFunctionInto(copy, &body, map, llvm::CloneFunctionChangeType::DifferentModule, returns);
			std::string text;
			llvm::raw_string_ostream out(text);
			alone.print(out, nullptr);
			return text;
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

	InlineBodyIndex::InlineBodyIndex() = default;
	InlineBodyIndex::~InlineBodyIndex() = default;

	std::string InlineBodyIndex::Digest(const std::string& name, const std::vector<std::string>& chunks, std::string& error)
	{
		const llvm::StringMap<uint32>* where = Locate(chunks, error);
		if (!where) return "";
		std::lock_guard<std::mutex> digesting(m_Digesting);
		if (auto known = m_Digests.find(name); known != m_Digests.end()) return known->second;
		auto at = where->find(name);
		if (at == where->end()) return m_Digests[name] = "";
		if (m_Chunks.size() < chunks.size()) m_Chunks.resize(chunks.size());
		ReadChunk& chunk = m_Chunks[at->second];
		if (!chunk.module)
		{
			chunk.context = std::make_unique<llvm::LLVMContext>();
			auto lazy = ReadLazily(*chunk.context, chunks[at->second]);
			if (!lazy)
			{
				error = Unread(lazy.takeError());
				return "";
			}
			chunk.module = std::move(*lazy);
		}
		llvm::Function* body = chunk.module->getFunction(name);
		if (llvm::Error unread = body->materialize())
		{
			error = Unread(std::move(unread));
			return "";
		}
		const std::string text = TextAlone(*body);
		llvm::BLAKE3 hasher;
		hasher.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
		return m_Digests[name] = Hex(hasher.final<16>());
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
	bool ImportInlineBodies(llvm::Module& script, const std::vector<std::string>& chunks, InlineBodyIndex& index,
		std::vector<std::string>& imported, std::string& error)
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
		for (const llvm::Function* body : *reached) imported.push_back(body->getName().str());
		std::unique_ptr<llvm::Module> bodies;
		for (std::unique_ptr<llvm::Module>& chunk : reader.Read())
		{
			if (!chunk || !KeepOnly(*chunk, *reached)) continue;
			if (!bodies) bodies = std::move(chunk);
			else if (llvm::Linker::linkModules(*bodies, std::move(chunk)))
			{
				error = "the interface's inline bodies do not link together";
				return false;
			}
		}
		if (!bodies) return true;
		if (llvm::Linker::linkModules(script, std::move(bodies), llvm::Linker::Flags::LinkOnlyNeeded))
		{
			error = "the interface's inline bodies do not link into the script";
			return false;
		}
		UseScriptRuntime(script); // a host body that throws or registers an exit goes through the script's runtime too
		return true;
	}
} // namespace C4Compiler
