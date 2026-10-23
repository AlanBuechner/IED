#include "Backend/CodeGen/UnwindInference.h"
#include "Compiler/WorkerPool.h"
#include <llvm/ADT/StringMap.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Local.h>
#include <unordered_map>

namespace Scripting
{
	namespace
	{
		constexpr int kNeverUnwinds = -1;
		constexpr int kOpaque = -2;

		// A function with a body that may unwind, and what it calls that may.
		struct Caller
		{
			const llvm::Function* function = nullptr;
			bool opaque = false;                          // calls through a pointer
			std::vector<const llvm::Function*> callees;   // called directly: a definition or a declaration of its module
		};

		Caller CallerOf(const llvm::Function& fn)
		{
			Caller caller{ &fn };
			for (const llvm::Instruction& inst : llvm::instructions(fn))
			{
				const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst);
				if (!call || call->doesNotThrow()) continue;
				if (const llvm::Function* callee = call->getCalledFunction()) caller.callees.push_back(callee);
				else caller.opaque = true;
			}
			return caller;
		}

		std::vector<Caller> CallersOf(const llvm::Module& module)
		{
			std::vector<Caller> callers;
			for (const llvm::Function& fn : module)
				if (!fn.isDeclaration() && !fn.doesNotThrow()) callers.push_back(CallerOf(fn));
			return callers;
		}

		bool Shared(const llvm::Function& fn)
		{
			return !fn.isDeclaration() && !fn.hasLocalLinkage() && !fn.hasAvailableExternallyLinkage();
		}

		// The program's functions as one call graph: a function is a node of the module that defines it, and a
		// declaration stands for the definition of its name in another module.
		class CallGraph
		{
		public:
			explicit CallGraph(std::vector<std::vector<Caller>> byModule, const std::vector<llvm::Module*>& modules)
			{
				for (std::vector<Caller>& callers : byModule)
					for (Caller& caller : callers)
					{
						m_Node.emplace(caller.function, static_cast<int>(m_Callers.size()));
						m_Callers.push_back(std::move(caller));
					}
				for (const llvm::Module* module : modules)
					for (const llvm::Function& fn : *module)
						if (Shared(fn)) m_Defined.try_emplace(fn.getName(), fn.doesNotThrow() ? kNeverUnwinds : m_Node.at(&fn));
			}

			// The node a direct call reaches, kNeverUnwinds, or kOpaque.
			int Resolve(const llvm::Function& callee) const
			{
				if (!callee.isDeclaration()) return callee.doesNotThrow() ? kNeverUnwinds : m_Node.at(&callee);
				const auto defined = m_Defined.find(callee.getName());
				return defined != m_Defined.end() ? defined->second : kOpaque;
			}

			// Every node that can unwind: the ones calling something opaque, then their callers, to a fixed point.
			std::vector<bool> Unwinding() const
			{
				std::vector<std::vector<int>> callersOf(m_Callers.size());
				std::vector<int> work;
				for (int i = 0; i < static_cast<int>(m_Callers.size()); ++i)
					if (Link(i, callersOf)) work.push_back(i);
				std::vector<bool> unwinds(m_Callers.size(), false);
				for (const int node : work) unwinds[node] = true;
				while (!work.empty())
				{
					const int node = work.back();
					work.pop_back();
					for (const int caller : callersOf[node])
						if (!unwinds[caller])
						{
							unwinds[caller] = true;
							work.push_back(caller);
						}
				}
				return unwinds;
			}

			bool NeverUnwinds(const llvm::Function& fn, const std::vector<bool>& unwinds) const
			{
				const int node = Resolve(fn);
				return node == kNeverUnwinds || (node >= 0 && !unwinds[node]);
			}

		private:
			// Whether node `i` calls something opaque; it is recorded as a caller of the nodes it calls.
			bool Link(int i, std::vector<std::vector<int>>& callersOf) const
			{
				bool opaque = m_Callers[i].opaque;
				for (const llvm::Function* callee : m_Callers[i].callees)
				{
					const int node = Resolve(*callee);
					if (node >= 0) callersOf[node].push_back(i);
					opaque |= node == kOpaque;
				}
				return opaque;
			}

			std::vector<Caller> m_Callers;
			std::unordered_map<const llvm::Function*, int> m_Node;
			llvm::StringMap<int> m_Defined;
		};

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

		void Apply(llvm::Module& module, const CallGraph& graph, const std::vector<bool>& unwinds)
		{
			for (llvm::Function& fn : module)
				if (!fn.doesNotThrow() && graph.NeverUnwinds(fn, unwinds)) fn.setDoesNotThrow();
			for (llvm::Function& fn : module)
				if (!fn.isDeclaration()) DropDeadUnwinds(fn);
		}
	}

	void InferNoUnwind(const std::vector<llvm::Module*>& modules, WorkerPool& pool)
	{
		std::vector<std::vector<Caller>> callers(modules.size());
		pool.Run(modules.size(), [&](size_t i) { callers[i] = CallersOf(*modules[i]); });
		const CallGraph graph(std::move(callers), modules);
		const std::vector<bool> unwinds = graph.Unwinding();
		pool.Run(modules.size(), [&](size_t i) { Apply(*modules[i], graph, unwinds); });
	}
} // namespace Scripting
