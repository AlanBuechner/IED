#include "Backend/CodeGen/UnwindInference.h"
#include "Compiler/WorkerPool.h"
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Local.h>

namespace C4Compiler
{
	namespace
	{
		constexpr int c_NeverUnwinds = -1;
		constexpr int c_Opaque = -2;

		UnwindCaller CallerOf(const llvm::Function& fn)
		{
			UnwindCaller caller{ fn.getName().str() };
			for (const llvm::Instruction& inst : llvm::instructions(fn))
			{
				const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst);
				if (!call || call->doesNotThrow()) continue;
				if (const llvm::Function* callee = call->getCalledFunction()) caller.callees.emplace_back(callee->getName().str(), !callee->isDeclaration());
				else caller.opaque = true;
			}
			return caller;
		}

		bool Shared(const llvm::Function& fn)
		{
			return !fn.isDeclaration() && !fn.hasLocalLinkage() && !fn.hasAvailableExternallyLinkage();
		}

		// The invokes of functions that never unwind become calls, each continuing in the block it ends; the exception
		// paths only they reached go with them.
		void DropDeadUnwinds(llvm::Function& fn)
		{
			std::vector<llvm::InvokeInst*> invokes;
			for (llvm::BasicBlock& block : fn)
				if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(block.getTerminator()); invoke && invoke->doesNotThrow())
					invokes.push_back(invoke);
			if (invokes.empty()) return;
			std::vector<llvm::BasicBlock*> continuations;
			for (llvm::InvokeInst* invoke : invokes)
			{
				continuations.push_back(invoke->getNormalDest());
				llvm::changeToCall(invoke);
			}
			for (llvm::BasicBlock* continuation : continuations) llvm::MergeBlockIntoPredecessor(continuation);
			llvm::EliminateUnreachableBlocks(fn);
		}
	}

	UnwindGraph::UnwindGraph(const std::vector<const UnwindPart*>& parts) : m_NodeIn(parts.size())
	{
		for (size_t part = 0; part < parts.size(); ++part)
			for (const UnwindCaller& caller : parts[part]->callers)
			{
				m_NodeIn[part].try_emplace(caller.name, static_cast<int>(m_Callers.size()));
				m_Callers.push_back(&caller);
				m_PartOf.push_back(part);
			}
		for (size_t part = 0; part < parts.size(); ++part)
			for (const auto& [name, neverUnwinds] : parts[part]->shared)
				m_Defined.try_emplace(name, neverUnwinds ? c_NeverUnwinds : m_NodeIn[part].lookup(name));
		FindUnwinding();
	}

	// A function defined in its part is a node there unless it never unwinds; a declaration stands for the definition of
	// its name in another part.
	int UnwindGraph::Resolve(size_t part, const std::string& name, bool definedThere) const
	{
		if (definedThere)
		{
			const auto node = m_NodeIn[part].find(name);
			return node == m_NodeIn[part].end() ? c_NeverUnwinds : node->second;
		}
		const auto defined = m_Defined.find(name);
		return defined != m_Defined.end() ? defined->second : c_Opaque;
	}

	bool UnwindGraph::NeverUnwinds(size_t part, const std::string& name, bool definedThere) const
	{
		const int node = Resolve(part, name, definedThere);
		return node == c_NeverUnwinds || (node >= 0 && !m_Unwinds[static_cast<size_t>(node)]);
	}

	bool UnwindGraph::Link(int node, std::vector<std::vector<int>>& callersOf) const
	{
		const UnwindCaller& caller = *m_Callers[static_cast<size_t>(node)];
		bool opaque = caller.opaque;
		for (const auto& [callee, definedThere] : caller.callees)
		{
			const int reached = Resolve(m_PartOf[static_cast<size_t>(node)], callee, definedThere);
			if (reached >= 0) callersOf[static_cast<size_t>(reached)].push_back(node);
			opaque |= reached == c_Opaque;
		}
		return opaque;
	}

	// Every node that can unwind: the ones calling something opaque, then their callers, to a fixed point.
	void UnwindGraph::FindUnwinding()
	{
		std::vector<std::vector<int>> callersOf(m_Callers.size());
		std::vector<int> work;
		for (int node = 0; node < static_cast<int>(m_Callers.size()); ++node)
			if (Link(node, callersOf)) work.push_back(node);
		m_Unwinds.assign(m_Callers.size(), false);
		for (const int node : work) m_Unwinds[static_cast<size_t>(node)] = true;
		while (!work.empty())
		{
			const int node = work.back();
			work.pop_back();
			for (const int caller : callersOf[static_cast<size_t>(node)])
				if (!m_Unwinds[static_cast<size_t>(caller)])
				{
					m_Unwinds[static_cast<size_t>(caller)] = true;
					work.push_back(caller);
				}
		}
	}

	UnwindPart UnwindPartOf(const llvm::Module& module)
	{
		UnwindPart part;
		for (const llvm::Function& fn : module)
		{
			if (!fn.isDeclaration() && !fn.doesNotThrow()) part.callers.push_back(CallerOf(fn));
			if (Shared(fn)) part.shared.emplace_back(fn.getName().str(), fn.doesNotThrow());
		}
		return part;
	}

	void ApplyNoUnwind(llvm::Module& module, size_t part, const UnwindGraph& graph, UnwindPart& kept)
	{
		kept.answered.clear();
		for (llvm::Function& fn : module)
		{
			if (fn.doesNotThrow()) continue;
			const bool never = graph.NeverUnwinds(part, fn.getName().str(), !fn.isDeclaration());
			kept.answered.emplace_back(fn.getName().str(), !fn.isDeclaration(), never);
			if (never) fn.setDoesNotThrow();
		}
		for (llvm::Function& fn : module)
			if (!fn.isDeclaration()) DropDeadUnwinds(fn);
	}

	bool SameAnswers(size_t part, const UnwindPart& kept, const UnwindGraph& graph)
	{
		for (const auto& [name, definedThere, never] : kept.answered)
			if (graph.NeverUnwinds(part, name, definedThere) != never) return false;
		return true;
	}

	void InferNoUnwind(const std::vector<llvm::Module*>& modules, WorkerPool& pool)
	{
		std::vector<UnwindPart> parts(modules.size());
		pool.Run(modules.size(), [&](size_t i) { parts[i] = UnwindPartOf(*modules[i]); });
		std::vector<const UnwindPart*> all;
		for (const UnwindPart& part : parts) all.push_back(&part);
		const UnwindGraph graph(all);
		pool.Run(modules.size(), [&](size_t i) { ApplyNoUnwind(*modules[i], i, graph, parts[i]); });
	}
} // namespace C4Compiler
