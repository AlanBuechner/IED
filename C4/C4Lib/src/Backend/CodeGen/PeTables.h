#pragma once
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <string>

namespace Scripting
{
	/// Building blocks of the read-only tables the MSVC runtime walks (RTTI, EH): they store 32-bit offsets
	/// from the image base instead of pointers.
	namespace PeTables
	{
		/// trunc(&g - &__ImageBase): what an RTTI or EH table stores for a reference to `g`.
		llvm::Constant* ImageRelative(llvm::Module& m, llvm::GlobalValue* g);

		/// An ELF image has no __ImageBase (D-86): every ImageRelative reference in `m`'s tables becomes relative to its
		/// own field, trunc(&g - &field), which the runtime reads as `(char*)&field + field`. Zero stays zero (null).
		void AnchorAtFields(llvm::Module& m);

		/// A read-only table shared by name across object files (linkonce_odr in its own comdat).
		llvm::GlobalVariable* Shared(llvm::Module& m, const std::string& name, llvm::Constant* init, const char* section);

		/// A read-only table private to this module.
		llvm::GlobalVariable* Private(llvm::Module& m, const std::string& name, llvm::Constant* init);

		llvm::Constant* I32(llvm::LLVMContext& ctx, int64_t v);
	}
} // namespace Scripting
