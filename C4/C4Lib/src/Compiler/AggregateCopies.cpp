#include "Compiler/AggregateCopies.h"
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

namespace Scripting
{
	namespace
	{
		bool NothingWritesBetween(const llvm::Instruction& first, const llvm::Instruction& last)
		{
			for (const llvm::Instruction* i = first.getNextNode(); i != &last; i = i->getNextNode())
				if (i->mayWriteToMemory()) return false;
			return true;
		}

		// The load a store copies from, when the store is a plain copy of a whole aggregate nothing else reads.
		llvm::LoadInst* CopiedFrom(llvm::StoreInst& store)
		{
			auto* load = llvm::dyn_cast<llvm::LoadInst>(store.getValueOperand());
			if (!load || !load->getType()->isAggregateType() || !load->hasOneUse()) return nullptr;
			if (!load->isSimple() || !store.isSimple() || load->getParent() != store.getParent()) return nullptr;
			return NothingWritesBetween(*load, store) ? load : nullptr;
		}

		void LowerCopy(llvm::StoreInst& store, llvm::LoadInst& load, const llvm::DataLayout& layout)
		{
			llvm::IRBuilder<> builder(&store);
			llvm::CallInst* copy = builder.CreateMemCpy(store.getPointerOperand(), store.getAlign(), load.getPointerOperand(), load.getAlign(),
				layout.getTypeStoreSize(load.getType()));
			copy->setDebugLoc(store.getDebugLoc());
			store.eraseFromParent();
			load.eraseFromParent();
		}
	}

	void LowerAggregateCopies(llvm::Module& module)
	{
		const llvm::DataLayout& layout = module.getDataLayout();
		for (llvm::Function& function : module)
			for (llvm::BasicBlock& block : function)
				for (llvm::Instruction& instruction : llvm::make_early_inc_range(block))
					if (auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction))
						if (llvm::LoadInst* load = CopiedFrom(*store)) LowerCopy(*store, *load, layout);
	}
}
