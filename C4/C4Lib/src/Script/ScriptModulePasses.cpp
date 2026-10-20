#include "Script/ScriptModulePasses.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/EHPersonalities.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ReplaceConstant.h>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	namespace
	{
		llvm::PointerType* Ptr(llvm::Module& module) { return llvm::PointerType::get(module.getContext(), 0); }

		// Calls a pass places among a function's instructions. One inside a catch or cleanup funclet names the
		// funclet's pad, or WinEH preparation takes it for a stray call and makes it unreachable.
		class PlacedCalls
		{
		public:
			llvm::CallInst* Before(llvm::Instruction* at, llvm::FunctionCallee callee)
			{
				llvm::IRBuilder<> B(at);
				if (llvm::FuncletPadInst* pad = FuncletOf(at->getParent()))
					return B.CreateCall(callee, {}, { llvm::OperandBundleDef("funclet", pad) });
				return B.CreateCall(callee);
			}

		private:
			llvm::FuncletPadInst* FuncletOf(llvm::BasicBlock* block)
			{
				llvm::Function* function = block->getParent();
				if (!function->hasPersonalityFn()) return nullptr;
				auto colors = m_Colors.find(function);
				if (colors == m_Colors.end()) colors = m_Colors.emplace(function, llvm::colorEHFunclets(*function)).first;
				auto color = colors->second.find(block);
				if (color == colors->second.end() || color->second.empty()) return nullptr;
				return llvm::dyn_cast<llvm::FuncletPadInst>(&*color->second.front()->getFirstNonPHIIt());
			}

			std::unordered_map<llvm::Function*, llvm::DenseMap<llvm::BasicBlock*, llvm::ColorVector>> m_Colors;
		};

		// Replaces each use of `variable` in `user` with a call of `accessor` placed where the value is needed: before
		// the instruction, or for a phi at the end of the block the value comes from.
		void CallAccessorFor(PlacedCalls& calls, llvm::Instruction* user, llvm::GlobalVariable* variable, llvm::FunctionCallee accessor)
		{
			if (auto* intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(user); intrinsic && intrinsic->getIntrinsicID() == llvm::Intrinsic::threadlocal_address)
			{
				intrinsic->replaceAllUsesWith(calls.Before(intrinsic, accessor));
				intrinsic->eraseFromParent();
				return;
			}
			if (auto* phi = llvm::dyn_cast<llvm::PHINode>(user))
			{
				for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
					if (phi->getIncomingValue(i) == variable) phi->setIncomingValue(i, calls.Before(phi->getIncomingBlock(i)->getTerminator(), accessor));
				return;
			}
			user->replaceUsesOfWith(variable, calls.Before(user, accessor));
		}

		// A runtime declaration, replaced by `replacement` everywhere and removed.
		void Redirect(llvm::Module& module, const char* name, llvm::Function* replacement)
		{
			llvm::Function* declared = module.getFunction(name);
			if (!declared || declared == replacement) return;
			declared->replaceAllUsesWith(replacement);
			declared->eraseFromParent();
		}

		// `void hook(fn, object)`: `target(module record, fn, object)`, so code written against c4_at_exit's signature
		// registers with the script's lists.
		llvm::Function* ExitHook(llvm::Module& module, const char* hookName, const char* target, llvm::GlobalVariable* record)
		{
			llvm::Type* ptr = Ptr(module);
			llvm::Type* voidType = llvm::Type::getVoidTy(module.getContext());
			llvm::FunctionCallee runtime = module.getOrInsertFunction(target, llvm::FunctionType::get(voidType, { ptr, ptr, ptr }, false));
			if (llvm::Function* made = module.getFunction(hookName)) return made;
			auto* hook = llvm::Function::Create(llvm::FunctionType::get(voidType, { ptr, ptr }, false), llvm::GlobalValue::InternalLinkage, hookName, module);
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(module.getContext(), "entry", hook));
			B.CreateCall(runtime, { B.CreateLoad(ptr, record), hook->getArg(0), hook->getArg(1) });
			B.CreateRetVoid();
			return hook;
		}
	}

	std::string ThreadLocalAccessor(const std::string& symbol) { return "__c4_tls_" + symbol; }

	void UseHostDefinitions(llvm::Module& module, const std::unordered_set<std::string>& hostSymbols)
	{
		for (llvm::Function& f : module)
		{
			// A host's file-static function (internal in the host, BUG-289) is only declared here, and binds to the
			// host table's entry by name: a declaration cannot be internal.
			if (f.isDeclaration() && f.hasLocalLinkage()) f.setLinkage(llvm::GlobalValue::ExternalLinkage);
			if (f.isDeclaration() || !hostSymbols.count(f.getName().str())) continue;
			f.setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
			f.setVisibility(llvm::GlobalValue::DefaultVisibility);
			f.setComdat(nullptr);
		}
		for (llvm::GlobalVariable& g : module.globals())
		{
			if (g.isDeclaration() || !hostSymbols.count(g.getName().str())) continue;
			g.setInitializer(nullptr);
			g.setLinkage(llvm::GlobalValue::ExternalLinkage);
			g.setVisibility(llvm::GlobalValue::DefaultVisibility);
			g.setComdat(nullptr);
		}
	}

	void UseHostThreadStorage(llvm::Module& module, const std::unordered_set<std::string>& hostSymbols)
	{
		std::vector<llvm::GlobalVariable*> hosts;
		for (llvm::GlobalVariable& g : module.globals())
			if (g.isThreadLocal() && g.hasName() && hostSymbols.count(ThreadLocalAccessor(g.getName().str()))) hosts.push_back(&g);
		auto* accessorType = llvm::FunctionType::get(Ptr(module), false);
		PlacedCalls calls;
		for (llvm::GlobalVariable* variable : hosts)
		{
			llvm::FunctionCallee accessor = module.getOrInsertFunction(ThreadLocalAccessor(variable->getName().str()), accessorType);
			llvm::convertUsersOfConstantsToInstructions({ variable });
			for (llvm::User* user : llvm::make_early_inc_range(variable->users()))
				if (auto* instruction = llvm::dyn_cast<llvm::Instruction>(user)) CallAccessorFor(calls, instruction, variable, accessor);
			if (variable->use_empty()) variable->eraseFromParent();
		}
	}

	void UseScriptRuntime(llvm::Module& module)
	{
		llvm::GlobalVariable* record = module.getNamedGlobal(kScriptModuleSymbol);
		if (!record)
			record = new llvm::GlobalVariable(module, Ptr(module), false, llvm::GlobalValue::ExternalLinkage,
				llvm::ConstantPointerNull::get(Ptr(module)), kScriptModuleSymbol);
		if (module.getFunction("c4_at_exit"))
			Redirect(module, "c4_at_exit", ExitHook(module, "__c4_script_exit_hook", "c4_script_at_exit", record));
		if (module.getFunction("c4_thread_at_exit"))
			Redirect(module, "c4_thread_at_exit", ExitHook(module, "__c4_script_thread_exit_hook", "c4_script_thread_at_exit", record));
		if (llvm::Function* thrower = module.getFunction("_CxxThrowException"))
		{
			llvm::FunctionCallee scriptThrow = module.getOrInsertFunction("c4_script_throw", thrower->getFunctionType());
			if (auto* replacement = llvm::dyn_cast<llvm::Function>(scriptThrow.getCallee())) replacement->setAttributes(thrower->getAttributes());
			Redirect(module, "_CxxThrowException", llvm::cast<llvm::Function>(scriptThrow.getCallee()));
		}
	}
} // namespace Scripting
