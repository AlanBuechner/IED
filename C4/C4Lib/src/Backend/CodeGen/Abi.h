#pragma once
#include "Compiler/TargetSystem.h"
#include "Model/Types.h"
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Type.h>
#include <cstdint>
#include <functional>

namespace Scripting
{
	/// THE calling-convention brick (doc 04). Every "how does this type cross a call boundary" answer in
	/// the whole backend goes through here, so the aggregate rules of the target's convention exist in exactly one
	/// place and cannot drift between call emitters, return emitters, and global emitters: the Windows x64
	/// convention, or System V's on Linux (D-86). The model is the size authority (Type::GetSize); this brick only
	/// decides register-vs-memory and sret from that size and, on System V, the scalars the aggregate holds.
	class Abi
	{
	public:
		/// `lower` gives a type's LLVM storage type, whose scalars System V classifies, laid out by `layout`;
		/// `vectorBytes` the widest vector register the target's processor has (16, 32 with AVX, 64 with AVX-512F).
		Abi(llvm::LLVMContext& ctx, TargetSystem system, const llvm::DataLayout& layout, std::function<llvm::Type*(const Type*)> lower,
			std::function<uint64_t()> vectorBytes)
			: m_Ctx(ctx), m_System(system), m_Layout(layout), m_Lower(std::move(lower)), m_VectorBytes(std::move(vectorBytes)) {}

		// --- classification (pure over model types) -------------------------------------
		// A by-value aggregate is an object, union, or array (after alias/reference unwrapping). These are
		// the only types whose passing convention depends on size. Scalars (ints/floats/pointers/string
		// handles/funcptrs) always pass in their own register. On System V so is a packed vector wider than the
		// processor's vector registers, which goes in memory (clang's __m256 without AVX).
		bool IsByValueAggregate(const Type* t) const;

		/// The registers a by-value aggregate crosses a call in, as the LLVM type that holds them: on Windows an
		/// integer of its width when it is 1, 2, 4 or 8 bytes; on System V each eightbyte of one up to 16 bytes, an
		/// integer or a float type as its scalars say (`{ i64, double }`, `<2 x float>`). Null when it does not
		/// cross in registers (it passes by hidden pointer and returns through sret), and for a non-aggregate.
		llvm::Type* RegisterType(const Type* t) const;

		// Does this type cross a call boundary in registers? True for all scalars and for the aggregates
		// RegisterType gives a type.
		bool PassesInRegister(const Type* t) const;

		// A by-value aggregate that does not cross in registers: the caller allocates a temporary and passes its
		// ADDRESS as a hidden pointer. (The callee sees a ptr to the aggregate.)
		bool PassesByHiddenPtr(const Type* t) const { return IsByValueAggregate(t) && !RegisterType(t); }

		/// System V passes such an aggregate to a C function on the stack, which the hidden pointer marked `byval`
		/// does (the callee gets a copy). C4's own functions take the hidden pointer as it is, so a C function a
		/// function pointer reaches takes one only up to 16 bytes (D-86).
		bool PassesOnStack(const Type* t) const { return m_System == TargetSystem::Linux && PassesByHiddenPtr(t); }

		// How a function receives its object: none (a free or static function), as a member function's `this`
		// (MSVC member convention), or as an extension method's receiver, which is a leading argument of a
		// plain C-convention function (CBind binds C++ methods to extern "C" wrappers of that shape).
		enum class CallKind { Free, Member, Extension };

		// sret: an aggregate RETURN that does not cross in registers uses a hidden out-pointer and a void LLVM
		// return type. On Windows a member function returns every object type through it, whatever its size
		// (MSVC). A reference return is NOT sret (it returns a ptr).
		bool IsSretReturn(const Type* t, CallKind kind = CallKind::Free) const;

		// Where a signature's hidden parameters sit. MSVC x64: a member function takes `this` first and the sret
		// pointer second. Itanium (Linux): the sret pointer comes first. Either way an extension method takes the sret
		// pointer first, then its receiver.
		struct Signature
		{
			bool sret = false;
			int thisIndex = -1;
			int sretIndex = -1;
			unsigned firstParam = 0; // the LLVM index of the first declared parameter
		};
		Signature SignatureOf(const Type* ret, CallKind kind) const;

		// --- the concrete LLVM types for a call boundary ---------------------------------
		// The LLVM type this parameter OCCUPIES in the function signature: an aggregate that crosses in registers
		// is its RegisterType (not its struct type); one that does not is a hidden ptr; everything else is its
		// storage type (call ModuleContext::Lower for scalars). Returns nullptr when the caller must use
		// ModuleContext::Lower instead (the scalar case) so this brick only owns the aggregate special cases.
		llvm::Type* ParamType(const Type* t) const;

		// The LLVM RETURN type: void for an sret aggregate, the RegisterType of an aggregate returned in registers,
		// nullptr otherwise (caller uses ModuleContext::Lower).
		llvm::Type* ReturnType(const Type* t, CallKind kind = CallKind::Free) const;

		/// The type of a temporary that holds both an aggregate of `object` type and its `registers`: the larger, so
		/// reading either through it reads only its own bytes.
		llvm::Type* CoercionSlot(llvm::Type* object, llvm::Type* registers) const;

	private:
		llvm::Type* WindowsRegisterType(const Type* t) const;
		llvm::Type* SystemVRegisterType(const Type* t) const;

		llvm::LLVMContext& m_Ctx;
		TargetSystem m_System;
		const llvm::DataLayout& m_Layout;
		std::function<llvm::Type*(const Type*)> m_Lower;
		std::function<uint64_t()> m_VectorBytes;
	};
} // namespace Scripting
