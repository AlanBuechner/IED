#include "Backend/CodeGen/CallEmitter.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/FuncEmitter.h"
#include "Model/Types/SignatureType.h"
#include "Model/TypeHandle.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		uint32_t BitWidth(const Type* t) { return static_cast<uint32_t>(t ? t->GetSize() * 8 : 0); }

		// The signature an indirect call goes through: the target's funcptr or func type.
		const SignatureType* TargetSignature(const HIRNode* target)
		{
			return target ? dynamic_cast<const SignatureType*>(TypeHandle::UnwrapToValue(target->type)) : nullptr;
		}

		bool CallsThroughFunc(const CallNode* n)
		{
			const SignatureType* s = n->callee ? nullptr : TargetSignature(n->target);
			return s && s->GetKind() == TypeKind::Func;
		}

		bool IsAddressOf(const Type* argType, const Type* referent)
		{
			const Type* t = TypeHandle::UnwrapToValue(argType);
			if (!t || t->GetKind() != TypeKind::Pointer) return false;
			return TypeHandle::UnwrapToValue(static_cast<const PointerType*>(t)->GetPointeeType()) == referent;
		}

		const Type* ReturnTypeOf(const CallNode* n)
		{
			if (n->callee) return n->callee->type ? n->callee->type->GetReturnType() : nullptr;
			const SignatureType* fp = TargetSignature(n->target);
			return fp ? fp->GetReturnType() : n->type;
		}
	}

	Abi::Signature CallEmitter::SignatureOf(const CallNode* n)
	{
		// A func's code is called like a member function whose `this` is the func's environment.
		ModuleContext& mc = m_Ctx.mctx();
		if (n->callee) return mc.SignatureOf(*n->callee);
		return mc.abi().SignatureOf(ReturnTypeOf(n), CallsThroughFunc(n) ? Abi::CallKind::Member : Abi::CallKind::Free);
	}

	bool CallEmitter::ReturnsInMemory(const CallNode* n) { return !n->materialize && SignatureOf(n).sret; }

	llvm::AllocaInst* CallEmitter::ResultTemporary(llvm::Type* type, const char* name)
	{
		llvm::AllocaInst* temporary = m_Ctx.EntryAlloca(type, name);
		m_Ctx.builder().CreateLifetimeStart(temporary);
		return temporary;
	}

	llvm::Value* CallEmitter::Emit(const CallNode* n, llvm::Value* destination)
	{
		ModuleContext& mc = m_Ctx.mctx();
		const Type* retType = ReturnTypeOf(n);
		const bool throughFunc = CallsThroughFunc(n);
		const Abi::Signature sig = SignatureOf(n);
		llvm::Value* func = throughFunc ? m_Expr.Emit(n->target) : nullptr;

		// Hidden parameters first (MSVC x64: `this`, then the sret pointer), then the declared arguments. The
		// receiver is evaluated once: a virtual call reads its vptr from the same `this` it passes. A materialized
		// result is read through its address later, so its temporary lives to the end of the function.
		std::vector<llvm::Value*> args(sig.firstParam, nullptr);
		llvm::Value* sret = nullptr;
		if (sig.sret && destination) sret = destination;
		else if (sig.sret && n->materialize) sret = m_Ctx.EntryAlloca(mc.Lower(retType), "sret");
		else if (sig.sret) sret = ResultTemporary(mc.Lower(retType), "sret");
		llvm::Value* thisArg = nullptr;
		if (sig.thisIndex >= 0)
		{
			thisArg = func ? m_Ctx.builder().CreateExtractValue(func, { 1 }) : n->receiver ? m_Expr.LValueAddress(n->receiver) : nullptr;
			args[sig.thisIndex] = thisArg ? thisArg : llvm::ConstantPointerNull::get(llvm::PointerType::get(mc.context(), 0));
		}
		if (sig.sretIndex >= 0) args[sig.sretIndex] = sret;
		AppendArguments({ n->callee, n->target, n->args }, args);

		// A noexcept callee never unwinds into this function: a plain call, no unwind edge.
		Callee callee = func ? Callee{ FuncEmitter::CodeType(mc, TargetSignature(n->target)), m_Ctx.builder().CreateExtractValue(func, { 0 }) }
			: ResolveCallee(n, thisArg);
		const bool mayThrow = !(n->callee && n->callee->isNoexcept);
		llvm::CallBase* call = m_Ctx.Call(callee.type, callee.target, args, mayThrow);
		if (destination) return nullptr;
		return Result(n, call, sret, retType);
	}

	void CallEmitter::EmitConstructor(const FunctionRecord& ctor, llvm::Value* object, const std::vector<const HIRNode*>& ctorArgs)
	{
		const Abi::Signature sig = m_Ctx.mctx().SignatureOf(ctor);
		std::vector<llvm::Value*> args(sig.firstParam, nullptr);
		if (sig.thisIndex >= 0) args[sig.thisIndex] = object;
		if (sig.sretIndex >= 0) args[sig.sretIndex] = object;
		AppendArguments({ &ctor, nullptr, ctorArgs }, args);
		m_Ctx.Call(m_Ctx.mctx().GetOrCreateFunction(ctor), args, !ctor.isNoexcept);
	}

	// --- arguments ---------------------------------------------------------------------------------------

	std::vector<const Type*> CallEmitter::ParameterTypes(const CallSite& site) const
	{
		std::vector<const Type*> types;
		if (site.callee && site.callee->type)
			for (uint32 i = 0; i < site.callee->type->GetParameterCount(); ++i) types.push_back(site.callee->type->GetParameters()[i].m_Type);
		else if (const SignatureType* fp = TargetSignature(site.target))
			for (uint32 i = 0; i < fp->GetParameterCount(); ++i) types.push_back(fp->GetParameterTypes()[i]);
		return types;
	}

	void CallEmitter::AppendArguments(const CallSite& site, std::vector<llvm::Value*>& args)
	{
		// A variadic callee takes every argument past its named parameters (printf(fmt, x, y)).
		const std::vector<const Type*> paramTys = ParameterTypes(site);
		bool variadic = site.callee && site.callee->type && site.callee->type->IsVariadic();
		size_t count = variadic || paramTys.empty() ? site.args.size() : std::min(paramTys.size(), site.args.size());
		for (uint32_t i = 0; i < count; ++i)
		{
			if (!site.args[i]) continue;
			bool named = i < paramTys.size();
			args.push_back(EmitArgument(site, i, named ? paramTys[i] : site.args[i]->type, variadic && !named));
		}
	}

	llvm::Value* CallEmitter::EmitArgument(const CallSite& site, uint32_t i, const Type* paramType, bool variadicExtra)
	{
		ModuleContext& mc = m_Ctx.mctx();
		Abi& abi = mc.abi();
		const HIRNode* arg = site.args[i];
		llvm::Value* v = m_Expr.Emit(arg);
		if (paramType && paramType->GetKind() == TypeKind::Reference)
		{
			// A reference parameter is passed by address: HIR hands an lvalue argument over as its address (a
			// pointer to the referent, converted to a base sub-object when needed); an rvalue is materialized
			// in a temporary whose address is passed.
			const Type* referent = TypeHandle::UnwrapToValue(static_cast<const ReferenceType*>(paramType)->GetReferentType());
			if (dynamic_cast<const AddressOfNode*>(arg) || IsAddressOf(arg->type, referent)) return v;
			return m_Ctx.Materialize(v, mc.Lower(referent));
		}
		if (paramType && site.callee && site.callee->isLinked)
			if (llvm::Value* msvc = LinkedInitializerList(v, paramType)) return msvc;
		if (!paramType) return v;
		if (variadicExtra) v = PromoteVariadic(v, paramType);
		uint32_t intSize = abi.ByValueIntSize(paramType);
		if (intSize != 0) // aggregate riding in one GPR: reinterpret its bytes as an int of that width
			return m_Ctx.builder().CreateLoad(llvm::IntegerType::get(mc.context(), intSize * 8), m_Ctx.Materialize(v, mc.Lower(paramType)));
		if (abi.PassesByHiddenPtr(paramType)) return m_Ctx.Materialize(v, mc.Lower(paramType));
		return v;
	}

	llvm::Value* CallEmitter::LinkedInitializerList(llvm::Value* list, const Type* paramType)
	{
		// A linked C++ callee takes std::initializer_list<T> as { const T* first, const T* last } by hidden
		// pointer; C4's __init_list<T> is { data, count }. Rebuild it in MSVC form in a temporary and pass its
		// address (the element array lives in this frame, so both pointers outlive the call).
		const Type* pe = paramType->GetKind() == TypeKind::Pointer ? static_cast<const PointerType*>(paramType)->GetPointeeType() : paramType;
		if (!pe || pe->GetKind() != TypeKind::InitList) return nullptr;
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		if (list->getType()->isPointerTy()) list = B.CreateLoad(m_Ctx.mctx().Lower(pe), list); // `__init_list<T>*`: the list behind it
		const Type* elemType = static_cast<const InitListType*>(pe)->GetElementType();
		llvm::Value* data = B.CreateExtractValue(list, { 0 });
		llvm::Value* last = B.CreateInBoundsGEP(m_Ctx.mctx().Storage(elemType), data, B.CreateExtractValue(list, { 1 }));
		auto* msvcList = llvm::StructType::get(ctx, { llvm::PointerType::get(ctx, 0), llvm::PointerType::get(ctx, 0) });
		llvm::Value* agg = B.CreateInsertValue(B.CreateInsertValue(llvm::PoisonValue::get(msvcList), data, { 0 }), last, { 1 });
		llvm::Value* temp = m_Ctx.EntryAlloca(msvcList, "ms_initializer_list");
		B.CreateStore(agg, temp);
		return temp;
	}

	llvm::Value* CallEmitter::PromoteVariadic(llvm::Value* v, const Type* argType)
	{
		// C default argument promotions for "...": float -> double, integrals narrower than int -> int (a bool
		// written as one byte into an 8-byte slot would read back with garbage high bytes).
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		if (argType->GetKind() == TypeKind::Float32) return B.CreateFPExt(v, llvm::Type::getDoubleTy(ctx));
		if (!argType->IsIntegral() || BitWidth(argType) >= 32) return v;
		bool zeroExtend = argType->IsUnsignedInteger() || argType->GetKind() == TypeKind::Bool;
		return zeroExtend ? B.CreateZExt(v, llvm::Type::getInt32Ty(ctx)) : B.CreateSExt(v, llvm::Type::getInt32Ty(ctx));
	}

	// --- callee and result -------------------------------------------------------------------------------

	CallEmitter::Callee CallEmitter::ResolveCallee(const CallNode* n, llvm::Value* thisArg)
	{
		if (!n->callee) return IndirectCallee(n);
		if (n->code) return { m_Ctx.mctx().FunctionTypeOf(*n->callee), m_Expr.Emit(n->code) };
		llvm::Function* fn = m_Ctx.mctx().GetOrCreateFunction(*n->callee);
		if (n->virtualIndex < 0 || !thisArg) return { fn->getFunctionType(), fn };
		// HIR moved the receiver to the sub-object whose vtable holds the slot: its vptr is at offset 0 and
		// the entry expects that sub-object as `this`.
		auto& B = m_Ctx.builder();
		auto* ptr = llvm::PointerType::get(m_Ctx.mctx().context(), 0);
		llvm::Value* vptr = B.CreateLoad(ptr, thisArg, "vptr");
		llvm::Value* slot = B.CreateInBoundsGEP(ptr, vptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Ctx.mctx().context()), n->virtualIndex));
		return { fn->getFunctionType(), B.CreateLoad(ptr, slot, "vfn") };
	}

	CallEmitter::Callee CallEmitter::IndirectCallee(const CallNode* n)
	{
		ModuleContext& mc = m_Ctx.mctx();
		Abi& abi = mc.abi();
		const SignatureType* fp = TargetSignature(n->target);
		const Type* ret = fp ? fp->GetReturnType() : nullptr;
		const Abi::Signature sig = abi.SignatureOf(ret, Abi::CallKind::Free);
		std::vector<llvm::Type*> tys(sig.firstParam, llvm::PointerType::get(mc.context(), 0));
		for (uint32_t i = 0; fp && i < fp->GetParameterCount(); ++i)
		{
			const Type* p = fp->GetParameterTypes()[i];
			llvm::Type* pt = abi.ParamType(p);
			tys.push_back(pt ? pt : mc.Lower(p));
		}
		llvm::Type* rty = sig.sret ? llvm::Type::getVoidTy(mc.context()) : abi.ReturnType(ret);
		if (!rty) rty = mc.Lower(ret);
		return { llvm::FunctionType::get(rty, tys, false), m_Expr.Emit(n->target) };
	}

	llvm::Value* CallEmitter::Result(const CallNode* n, llvm::CallBase* call, llvm::Value* sret, const Type* retType)
	{
		auto& B = m_Ctx.builder();
		ModuleContext& mc = m_Ctx.mctx();
		// A by-value aggregate that returns in a register rides as an integer of its width: unpack it through a
		// temporary. `materialize` (member access on the result) wants the result's address instead: the sret
		// slot, or a temporary holding what came back in a register (an integer or a SIMD vector).
		uint32_t retIntSize = sret || !retType ? 0 : mc.abi().ByValueIntSize(retType);
		if (n->materialize)
		{
			if (sret) return sret;
			llvm::Value* tmp = m_Ctx.EntryAlloca(mc.Lower(retType));
			B.CreateStore(call, tmp);
			return tmp;
		}
		llvm::Value* temporary = sret;
		if (!sret && retIntSize == 0) return call;
		if (!sret)
		{
			temporary = ResultTemporary(mc.Lower(retType), "tmp");
			B.CreateStore(call, temporary);
		}
		llvm::Value* result = B.CreateLoad(mc.Lower(retType), temporary);
		B.CreateLifetimeEnd(temporary);
		return result;
	}
} // namespace Scripting
