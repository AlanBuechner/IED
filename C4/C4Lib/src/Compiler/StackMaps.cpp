#include "Compiler/StackMaps.h"
#include "Compiler/StackMapFormat.h"
#include "Compiler/StackMapRecords.h"
#include "Compiler/StackMapSlots.h"
#include "Compiler/StackMapValues.h"
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/CodeGen/GCMetadataPrinter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GCStrategy.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <mutex>
#include <vector>

namespace C4Compiler
{
	namespace
	{
		constexpr const char* c_StackMapsFlag = "c4.stack_maps";

		// The collector never moves an object, so a statepoint relocates nothing: every value it names is a deopt operand,
		// which code generation records where it is (a register, a spill slot) and the code goes on using as it was. No
		// pointer is a managed one to LLVM, so none is spilled for a relocation that never comes. Its metadata is the
		// records, which C4Printer writes.
		class C4Strategy : public llvm::GCStrategy
		{
		public:
			C4Strategy()
			{
				UseStatepoints = true;
				UsesMetadata = true;
			}

			std::optional<bool> isGCManagedPointer(const llvm::Type*) const override { return false; }
		};

		// Writes the records in C4's format (Compiler/StackMapFormat) instead of LLVM's.
		class C4Printer : public llvm::GCMetadataPrinter
		{
		public:
			bool emitStackMaps(llvm::StackMaps& maps, llvm::AsmPrinter& printer) override
			{
				EmitStackMaps(maps, printer);
				return true;
			}
		};

		const llvm::GCRegistry::Add<C4Strategy> s_Strategy(c_StackMapsGc, "C4: statepoints recording live values in place (R16)");
		const llvm::GCMetadataPrinterRegistry::Add<C4Printer> s_Printer(c_StackMapsGc, "C4: stack maps in C4's format (R16)");

		// A deopt operand LLVM may keep in a register (callee-saved where it lives across the call) instead of spilling it
		// to a slot before every call: LLVM's `use-registers-for-deopt-values`, set once for the process.
		void KeepDeoptValuesInRegisters()
		{
			static std::once_flag s_Set;
			std::call_once(s_Set, [] {
				auto& options = llvm::cl::getRegisteredOptions();
				const auto found = options.find("use-registers-for-deopt-values");
				if (found != options.end()) static_cast<llvm::cl::opt<bool>*>(found->second)->setValue(true);
			});
		}

		// --- calls left as they are ---------------------------------------------------------------------------------

		// An intrinsic is no call a record could describe.
		bool IsIntrinsic(const llvm::CallBase& call)
		{
			const llvm::Function* callee = call.getCalledFunction();
			return callee && callee->isIntrinsic();
		}

		// Whether code generation may make `call` a sibling call: marked `tail`, and the function returns nothing or what the
		// call returns, with nothing between but no-op casts of it and a branch to a block that only returns (code generation
		// copies that return back into the call's block).
		bool InTailPosition(const llvm::CallInst& call)
		{
			if (!call.isTailCall()) return false;
			const llvm::Value* result = &call;
			const llvm::Instruction* next = call.getNextNode();
			while (const auto* cast = llvm::dyn_cast<llvm::CastInst>(next))
			{
				if (!cast->isNoopCast(call.getDataLayout()) || cast->getOperand(0) != result) return false;
				result = cast;
				next = next->getNextNode();
			}
			const llvm::BasicBlock* from = nullptr;
			if (const auto* branch = llvm::dyn_cast<llvm::BranchInst>(next); branch && branch->isUnconditional())
			{
				from = call.getParent();
				next = &*branch->getSuccessor(0)->getFirstNonPHIIt();
			}
			const auto* ret = llvm::dyn_cast<llvm::ReturnInst>(next);
			if (!ret) return false;
			const llvm::Value* returned = ret->getReturnValue();
			if (const auto* phi = llvm::dyn_cast_or_null<llvm::PHINode>(returned); phi && from && phi->getParent() == ret->getParent())
				returned = phi->getIncomingValueForBlock(from);
			return !returned || returned == result;
		}

		// See InsertStatepoints: what no record could describe, and the C library functions code generation knows by name
		// (it expands `memcmp`, `memchr`, `sqrt` in place), which never collect.
		bool StaysLeaf(const llvm::CallBase& call, const llvm::TargetLibraryInfo& library)
		{
			if (call.isInlineAsm() || call.getFunctionType()->isVarArg()) return true;
			if (llvm::isa<llvm::CallBrInst>(call)) return true;
			// A Windows funclet's slots are its parent frame's, which its record could not name.
			if (call.getOperandBundle(llvm::LLVMContext::OB_funclet)) return true;
			if (call.doesNotReturn() || (!call.isTerminator() && llvm::isa<llvm::UnreachableInst>(call.getNextNode()))) return true;
			if (const auto* plain = llvm::dyn_cast<llvm::CallInst>(&call); plain && (plain->isMustTailCall() || InTailPosition(*plain))) return true;
			if (call.hasFnAttr(llvm::Attribute::ReturnsTwice)) return true;
			for (unsigned i = 0; i < call.arg_size(); ++i)
				if (call.isByValArgument(i)) return true;
			llvm::LibFunc known;
			if (const llvm::Function* callee = call.getCalledFunction(); callee && library.getLibFunc(*callee, known) && library.hasOptimizedCodeGen(known))
				return true;
			return false;
		}

		// The calls that get a record (one that has its record already is left as it is).
		std::vector<llvm::CallBase*> Statepoints(llvm::Function& function, const llvm::TargetLibraryInfoImpl& libraryOfTarget)
		{
			const llvm::TargetLibraryInfo library(libraryOfTarget, &function);
			std::vector<llvm::CallBase*> calls;
			for (llvm::Instruction& instruction : llvm::instructions(function))
				if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction); call && !IsIntrinsic(*call) && !StaysLeaf(*call, library) &&
					!call->getOperandBundle(llvm::LLVMContext::OB_deopt))
					calls.push_back(call);
			return calls;
		}
	}

	void MarkForStackMaps(llvm::Module& module)
	{
		if (!UsesStackMaps(module)) module.addModuleFlag(llvm::Module::Error, c_StackMapsFlag, 1);
	}

	bool UsesStackMaps(const llvm::Module& module)
	{
		const auto* flag = llvm::mdconst::extract_or_null<llvm::ConstantInt>(module.getModuleFlag(c_StackMapsFlag));
		return flag && !flag->isZero();
	}

	void InsertStatepoints(llvm::Module& module, llvm::CodeGenOptLevel level)
	{
		if (!UsesStackMaps(module)) return;
		KeepDeoptValuesInRegisters();
		const llvm::TargetLibraryInfoImpl library(module.getTargetTriple());
		const RecordedValues where = level == llvm::CodeGenOptLevel::None ? RecordedValues::InSlots : RecordedValues::InPlace;
		for (llvm::Function& function : module)
		{
			if (function.isDeclaration() || function.hasFnAttribute(llvm::Attribute::Naked)) continue;
			const std::vector<llvm::CallBase*> calls = Statepoints(function, library);
			if (!calls.empty()) RecordCalls(function, calls, ValuesLiveAcross(function, calls), FrameSlotsAt(function, calls), where);
			function.setGC(c_StackMapsGc);
		}
	}
}
