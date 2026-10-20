#pragma once
#include "Backend/CodeGen/AliasClass.h"
#include <array>

namespace llvm { class Instruction; class LLVMContext; class MDNode; }

namespace Scripting
{
	/// The type-based alias metadata of a module (D-64): a `!tbaa` scalar node per AliasClass under the root
	/// `C4 TBAA`, `bytes` the parent of the others, and an access tag for each. When strict aliasing is off every
	/// tag is null, so accesses carry none and may touch any memory.
	class AliasTags
	{
	public:
		AliasTags(llvm::LLVMContext& context, std::function<bool(const Type*)> hasVptrs);

		/// `-fno-strict-aliasing`.
		void Disable() { m_Enabled = false; }

		/// The tag of an access of `accessed` at `loc`, or null.
		llvm::MDNode* OfAccess(const LValue* loc, const Type* accessed);
		/// The tag of an access of `type` through a pointer, or null.
		llvm::MDNode* OfType(const Type* type);

	private:
		llvm::MDNode* Tag(AliasClass c);

		llvm::LLVMContext& m_Context;
		AliasClassifier m_Classes;
		bool m_Enabled = true;
		std::array<llvm::MDNode*, static_cast<size_t>(AliasClass::Count)> m_Tags{};
	};

	/// Gives a load or store (a masked one, a gather) its alias tag, when it has one; returns the access.
	llvm::Instruction* Attach(llvm::Instruction* access, llvm::MDNode* tag);
} // namespace Scripting
