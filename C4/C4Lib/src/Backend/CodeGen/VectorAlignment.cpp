#include "Backend/CodeGen/VectorAlignment.h"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <optional>

namespace Scripting
{
	namespace
	{
		// The alignment C4 gives a vector of `type`, or none when LLVM's own is already right.
		std::optional<llvm::Align> GuaranteedAlignment(const llvm::DataLayout& layout, llvm::Type* type)
		{
			if (!llvm::isa<llvm::FixedVectorType>(type)) return std::nullopt;
			const uint64_t size = layout.getTypeStoreSize(type).getFixedValue();
			if (size == 0 || (size & (size - 1)) == 0) return std::nullopt;
			return llvm::Align(size & (~size + 1));
		}

		template <typename Access>
		void CapAlignment(Access& access, std::optional<llvm::Align> guaranteed)
		{
			if (guaranteed && *guaranteed < access.getAlign()) access.setAlignment(*guaranteed);
		}
	}

	void AlignVectorAccesses(llvm::Module& module)
	{
		const llvm::DataLayout& layout = module.getDataLayout();
		for (llvm::Function& function : module)
			for (llvm::BasicBlock& block : function)
				for (llvm::Instruction& instruction : block)
				{
					if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction))
						CapAlignment(*load, GuaranteedAlignment(layout, load->getType()));
					else if (auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction))
						CapAlignment(*store, GuaranteedAlignment(layout, store->getValueOperand()->getType()));
				}
	}
} // namespace Scripting
