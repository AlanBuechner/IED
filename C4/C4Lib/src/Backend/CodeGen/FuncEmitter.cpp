#include "Backend/CodeGen/FuncEmitter.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/ObjectEmitter.h"
#include "Model/Types/SignatureType.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>

namespace Scripting
{
	namespace
	{
		llvm::FunctionType* SignatureFunctionType(ModuleContext& mc, const SignatureType* signature, Abi::CallKind kind)
		{
			Abi& abi = mc.abi();
			const Type* ret = signature->GetReturnType();
			const Abi::Signature s = abi.SignatureOf(ret, kind);
			std::vector<llvm::Type*> params(s.firstParam, llvm::PointerType::get(mc.context(), 0));
			for (const Type* p : signature->GetParameterTypes())
			{
				llvm::Type* pt = abi.ParamType(p);
				params.push_back(pt ? pt : mc.Lower(p));
			}
			llvm::Type* result = s.sret ? llvm::Type::getVoidTy(mc.context()) : abi.ReturnType(ret, kind);
			return llvm::FunctionType::get(result ? result : mc.Lower(ret), params, false);
		}

		std::string ThunkName(const SignatureType* signature)
		{
			std::string name = "__c4_func_thunk_" + std::string(signature->GetName().c_str());
			for (char& c : name)
				if (!isalnum(static_cast<unsigned char>(c))) c = '_';
			return name;
		}

		// thunk(env, [sret], args...) = ((funcptr)env)([sret], args...): the funcptr's own return convention
		// may hand a small object back in a register where the member convention returns it through sret.
		void BuildThunkBody(ModuleContext& mc, const SignatureType* signature, llvm::Function* thunk)
		{
			Abi& abi = mc.abi();
			const Type* ret = signature->GetReturnType();
			const Abi::Signature member = abi.SignatureOf(ret, Abi::CallKind::Member);
			const Abi::Signature free = abi.SignatureOf(ret, Abi::CallKind::Free);
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(mc.context(), "entry", thunk));
			llvm::Value* target = thunk->getArg(static_cast<unsigned>(member.thisIndex));
			llvm::Value* result = member.sret ? thunk->getArg(static_cast<unsigned>(member.sretIndex)) : nullptr;

			std::vector<llvm::Value*> args(free.firstParam, nullptr);
			if (free.sret) args[static_cast<size_t>(free.sretIndex)] = result;
			for (unsigned i = member.firstParam; i < thunk->arg_size(); ++i) args.push_back(thunk->getArg(i));
			llvm::CallInst* call = B.CreateCall(SignatureFunctionType(mc, signature, Abi::CallKind::Free), target, args);

			if (member.sret && !free.sret) B.CreateStore(call, result);
			if (thunk->getReturnType()->isVoidTy()) B.CreateRetVoid();
			else B.CreateRet(call);
		}
	}

	static llvm::Function* OnePointerAdapter(ModuleContext& mc, const FunctionRecord& call, const Type* closure)
	{
		// adapter(pointer, [sret], args...) builds the closure { pointer } on its own stack and calls the call
		// method with it: a func holds a one-pointer closure without allocating.
		llvm::Function* method = mc.GetOrCreateFunction(call);
		const std::string name = std::string(call.mangledName.c_str()) + "_onepointer";
		if (llvm::Function* existing = mc.module().getFunction(name)) return existing;
		llvm::Function* adapter = llvm::Function::Create(method->getFunctionType(), llvm::Function::InternalLinkage, name, &mc.module());
		adapter->addFnAttr(llvm::Attribute::UWTable);
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(mc.context(), "entry", adapter));
		llvm::Value* object = B.CreateAlloca(mc.Lower(closure), nullptr, "closure");
		B.CreateStore(adapter->getArg(0), object); // the closure's one field, at offset 0
		std::vector<llvm::Value*> args{ object };
		for (unsigned i = 1; i < adapter->arg_size(); ++i) args.push_back(adapter->getArg(i));
		llvm::CallInst* result = B.CreateCall(method, args);
		if (adapter->getReturnType()->isVoidTy()) B.CreateRetVoid();
		else B.CreateRet(result);
		return adapter;
	}

	llvm::FunctionType* FuncEmitter::CodeType(ModuleContext& mc, const SignatureType* signature)
	{
		return SignatureFunctionType(mc, signature, Abi::CallKind::Member);
	}

	llvm::Function* FuncThunk(ModuleContext& mc, const SignatureType* signature)
	{
		const std::string name = ThunkName(signature);
		if (llvm::Function* existing = mc.module().getFunction(name)) return existing;
		llvm::Function* thunk = llvm::Function::Create(FuncEmitter::CodeType(mc, signature), llvm::Function::InternalLinkage, name, &mc.module());
		thunk->addFnAttr(llvm::Attribute::UWTable); // a throw from the funcptr unwinds through it
		BuildThunkBody(mc, signature, thunk);
		return thunk;
	}

	llvm::Value* FuncEmitter::EmitMakeFunc(const MakeFuncNode* n)
	{
		ModuleContext& mc = m_Ctx.mctx();
		const auto* signature = static_cast<const SignatureType*>(n->type);
		switch (n->from)
		{
		case MakeFuncNode::From::Null:
			return llvm::Constant::getNullValue(mc.Lower(n->type));
		case MakeFuncNode::From::Pointer:
			return Pack(FuncThunk(mc, signature), m_Expr.Emit(n->source));
		case MakeFuncNode::From::PointerClosure:
		{
			llvm::Value* pointer = m_Ctx.builder().CreateExtractValue(m_Expr.Emit(n->source), { 0 });
			return Pack(OnePointerAdapter(mc, *n->code, n->source->type), pointer);
		}
		case MakeFuncNode::From::HeapClosure:
		{
			// The environment is built first: emitting the closure may emit other code before the call method.
			llvm::Value* environment = HeapEnvironment(n);
			return Pack(mc.GetOrCreateFunction(*n->code), environment);
		}
		}
		return nullptr;
	}

	llvm::Value* FuncEmitter::HeapEnvironment(const MakeFuncNode* n)
	{
		// A GC copy of the closure, finalized by the closure type's delete when it has one.
		const Type* closure = n->source->type;
		llvm::Value* value = m_Expr.Emit(n->source);
		ObjectEmitter objects(m_Ctx, m_Expr);
		llvm::Value* environment = objects.Allocate(closure);
		if (n->finalizer) objects.RegisterFinalizer(environment, *n->finalizer);
		m_Ctx.builder().CreateAlignedStore(value, environment, llvm::Align(closure->GetAlignment() ? closure->GetAlignment() : 1));
		return environment;
	}

	llvm::Value* FuncEmitter::Pack(llvm::Value* code, llvm::Value* environment)
	{
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		auto* funcTy = llvm::StructType::get(ctx, { ptr, ptr });
		llvm::Value* packed = B.CreateInsertValue(llvm::PoisonValue::get(funcTy), code, { 0 });
		return B.CreateInsertValue(packed, environment, { 1 });
	}
}
