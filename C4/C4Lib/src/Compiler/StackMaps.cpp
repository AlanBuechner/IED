#include "Compiler/StackMaps.h"
#include "Compiler/StackMapFormat.h"
#include "Compiler/StackMapPlacement.h"
#include "Compiler/StackMapRecords.h"
#include "Compiler/StackMapSlots.h"
#include "Compiler/StackMapValues.h"
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/CodeGen/GCMetadataPrinter.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/Passes.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GCStrategy.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Target/RegisterTargetPassConfigCallback.h>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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

		// Whether `call` has its record already: a deopt list, or a stackmap after it (Compiler/StackMapRecords, -O0).
		bool Recorded(llvm::CallBase& call)
		{
			if (call.getOperandBundle(llvm::LLVMContext::OB_deopt)) return true;
			const auto* next = llvm::dyn_cast_or_null<llvm::IntrinsicInst>(RecordPlace(call));
			if (!next || next->getIntrinsicID() != llvm::Intrinsic::experimental_stackmap) return false;
			const auto* id = llvm::dyn_cast<llvm::ConstantInt>(next->getArgOperand(0));
			return id && id->getZExtValue() == c_CallRecordId;
		}

		// --- calls that cannot collect ------------------------------------------------------------------------------

		// Whether a call may start a collection on its thread before it returns, as far as the call itself says: one of a
		// function defined here as the function may (NeverCollecting); a declaration's may - C4's runtime allocates, other
		// code may call back into C4 - but a C library function's never, nor an intrinsic's.
		enum class Collects { Never, Maybe, AsTheCallee };

		Collects CallCollects(const llvm::CallBase& call, const llvm::TargetLibraryInfo& library)
		{
			if (call.isInlineAsm()) return Collects::Maybe;
			const llvm::Function* callee = call.getCalledFunction();
			if (!callee) return Collects::Maybe;
			if (callee->isIntrinsic()) return Collects::Never;
			if (!callee->isDeclarationForLinker()) return Collects::AsTheCallee;
			llvm::LibFunc known;
			return library.getLibFunc(*callee, known) ? Collects::Never : Collects::Maybe;
		}

		// The functions defined in `module` that never start a collection: none of their calls may, nor do the functions
		// defined here they call.
		std::unordered_set<const llvm::Function*> NeverCollecting(llvm::Module& module, const llvm::TargetLibraryInfoImpl& libraryOfTarget)
		{
			std::unordered_map<const llvm::Function*, std::vector<const llvm::Function*>> callersOf;
			std::unordered_set<const llvm::Function*> collecting;
			std::vector<const llvm::Function*> pending;
			for (llvm::Function& function : module)
			{
				if (function.isDeclarationForLinker()) continue;
				const llvm::TargetLibraryInfo library(libraryOfTarget, &function);
				bool collects = false;
				for (const llvm::Instruction& instruction : llvm::instructions(function))
					if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
						switch (CallCollects(*call, library))
						{
						case Collects::Maybe: collects = true; break;
						case Collects::AsTheCallee: callersOf[call->getCalledFunction()].push_back(&function); break;
						case Collects::Never: break;
						}
				if (collects && collecting.insert(&function).second) pending.push_back(&function);
			}
			while (!pending.empty())
			{
				const llvm::Function* function = pending.back();
				pending.pop_back();
				for (const llvm::Function* caller : callersOf[function])
					if (collecting.insert(caller).second) pending.push_back(caller);
			}
			std::unordered_set<const llvm::Function*> never;
			for (const llvm::Function& function : module)
				if (!function.isDeclarationForLinker() && !collecting.count(&function)) never.insert(&function);
			return never;
		}

		// The calls that get a record: those that may collect (one that has its record already is left as it is).
		std::vector<llvm::CallBase*> Statepoints(llvm::Function& function, const llvm::TargetLibraryInfoImpl& libraryOfTarget,
			const std::unordered_set<const llvm::Function*>& never)
		{
			const llvm::TargetLibraryInfo library(libraryOfTarget, &function);
			std::vector<llvm::CallBase*> calls;
			for (llvm::Instruction& instruction : llvm::instructions(function))
			{
				auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
				if (!call || IsIntrinsic(*call) || StaysLeaf(*call, library) || Recorded(*call)) continue;
				const Collects collects = CallCollects(*call, library);
				if (collects == Collects::Maybe || (collects == Collects::AsTheCallee && !never.count(call->getCalledFunction()))) calls.push_back(call);
			}
			return calls;
		}
	}

	// The placement pass is registered here, not by a static object: LLVM's list of pipeline callbacks is a static of
	// another file, whose construction may come after ours and empty it.
	void PrepareCodeGeneration()
	{
		static std::once_flag s_Set;
		std::call_once(s_Set, [] {
			auto& options = llvm::cl::getRegisteredOptions();
			const auto found = options.find("use-registers-for-deopt-values");
			if (found != options.end()) static_cast<llvm::cl::opt<bool>*>(found->second)->setValue(true);
			static const llvm::RegisterTargetPassConfigCallback s_Placement([](llvm::TargetMachine&, llvm::PassManagerBase&, llvm::TargetPassConfig* config) {
				config->insertPass(&llvm::StackMapLivenessID, llvm::IdentifyingPassPtr(CreateStackMapPlacement()));
			});
		});
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
		PrepareCodeGeneration();
		if (!UsesStackMaps(module)) return;
		const llvm::TargetLibraryInfoImpl library(module.getTargetTriple());
		const RecordedValues where = level == llvm::CodeGenOptLevel::None ? RecordedValues::InSlots : RecordedValues::InPlace;
		const std::unordered_set<const llvm::Function*> never = NeverCollecting(module, library);
		for (llvm::Function& function : module)
		{
			if (function.isDeclaration() || function.hasFnAttribute(llvm::Attribute::Naked)) continue;
			const std::vector<llvm::CallBase*> calls = Statepoints(function, library, never);
			if (calls.empty()) continue;
			RecordCalls(function, calls, ValuesLiveAcross(function, calls), FrameSlotsAt(function, calls), where);
			function.setGC(c_StackMapsGc);
		}
	}
}
