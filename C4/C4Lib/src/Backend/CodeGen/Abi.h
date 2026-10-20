#pragma once
#include "Model/Types.h"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Type.h>
#include <cstdint>

namespace Scripting
{
	/// THE calling-convention brick (doc 04). Every "how does this type cross a call boundary" answer in
	/// the whole backend goes through here, so the Windows x64 aggregate rules exist in exactly one place
	/// and cannot drift between call emitters, return emitters, and global emitters. The model is the size
	/// authority (Type::GetSize); this brick only decides register-vs-hidden-pointer and sret from that size.
	class Abi
	{
	public:
		explicit Abi(llvm::LLVMContext& ctx) : m_Ctx(ctx) {}

		// --- classification (pure over model types) -------------------------------------
		// A by-value aggregate is an object, union, or array (after alias/reference unwrapping). These are
		// the only types whose passing convention depends on size. Scalars (ints/floats/pointers/string
		// handles/funcptrs) always pass in their own register.
		bool IsByValueAggregate(const Type* t) const;

		// The single-register integer size a by-value aggregate is passed as, when it fits: 1, 2, 4, or 8
		// bytes -> that int width. Returns 0 when the aggregate does NOT fit one register (size not in that
		// set) - such an aggregate passes by hidden pointer instead. Non-aggregates return 0.
		uint32_t ByValueIntSize(const Type* t) const;

		// Does this type cross a call boundary in a single register? True for all scalars and for the
		// 1/2/4/8-byte aggregates (which ride in one GPR as an integer of that width).
		bool PassesInRegister(const Type* t) const;

		// A by-value aggregate too large for one register: the caller allocates a temporary and passes its
		// ADDRESS as a hidden pointer. (The callee sees a ptr to the aggregate.)
		bool PassesByHiddenPtr(const Type* t) const { return IsByValueAggregate(t) && ByValueIntSize(t) == 0; }

		// How a function receives its object: none (a free or static function), as a member function's `this`
		// (MSVC member convention), or as an extension method's receiver, which is a leading argument of a
		// plain C-convention function (CBind binds C++ methods to extern "C" wrappers of that shape).
		enum class CallKind { Free, Member, Extension };

		// sret: an aggregate RETURN that does not fit one register uses a hidden out-pointer and a void LLVM
		// return type. A member function returns every object type through it, whatever its size (MSVC).
		// A reference return is NOT sret (it returns a ptr).
		bool IsSretReturn(const Type* t, CallKind kind = CallKind::Free) const;

		// Where a signature's hidden parameters sit, in MSVC x64 order: a member function takes `this` first
		// and the sret pointer second; an extension method takes the sret pointer first, then its receiver.
		struct Signature
		{
			bool sret = false;
			int thisIndex = -1;
			int sretIndex = -1;
			unsigned firstParam = 0; // the LLVM index of the first declared parameter
		};
		Signature SignatureOf(const Type* ret, CallKind kind) const;

		// --- the concrete LLVM types for a call boundary ---------------------------------
		// The LLVM type this parameter OCCUPIES in the function signature. A by-value aggregate that fits
		// one register is an integer of that width (not its struct type); a too-large aggregate is a hidden
		// ptr; everything else is its storage type (call ModuleContext::Lower for scalars). Returns nullptr
		// when the caller must use ModuleContext::Lower instead (the scalar case) so this brick only owns
		// the aggregate special cases.
		llvm::Type* ParamType(const Type* t) const;

		// The LLVM RETURN type: void for an sret aggregate, an integer of that width for a 1/2/4/8-byte
		// aggregate return, nullptr otherwise (caller uses ModuleContext::Lower).
		llvm::Type* ReturnType(const Type* t, CallKind kind = CallKind::Free) const;

	private:
		llvm::LLVMContext& m_Ctx;
	};
} // namespace Scripting
