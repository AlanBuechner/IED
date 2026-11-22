#include "Compiler/StackMaps.h"
#include "Compiler/StackMapSlots.h"
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/GCStrategy.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/Scalar/RewriteStatepointsForGC.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace Scripting
{
	namespace
	{
		constexpr const char* c_StackMapsFlag = "c4.stack_maps";
		constexpr const char* c_LeafCall = "gc-leaf-function";

		class C4Strategy : public llvm::GCStrategy
		{
		public:
			C4Strategy()
			{
				UseStatepoints = true;
				UseRS4GC = true;
			}

			std::optional<bool> isGCManagedPointer(const llvm::Type* type) const override
			{
				const auto* pointer = llvm::dyn_cast<llvm::PointerType>(type);
				return pointer && pointer->getAddressSpace() == c_ManagedAddressSpace;
			}
		};

		const llvm::GCRegistry::Add<C4Strategy> s_Strategy(c_StackMapsGc, "C4: statepoints over the managed address space (R16)");

		bool IsManaged(const llvm::Type* type)
		{
			const auto* pointer = llvm::dyn_cast<llvm::PointerType>(type->getScalarType());
			return pointer && pointer->getAddressSpace() == c_ManagedAddressSpace;
		}

		bool HoldsManaged(const llvm::Type* type)
		{
			if (IsManaged(type)) return true;
			if (const auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			{
				for (const llvm::Type* element : structure->elements())
					if (HoldsManaged(element)) return true;
				return false;
			}
			if (const auto* array = llvm::dyn_cast<llvm::ArrayType>(type)) return HoldsManaged(array->getElementType());
			return false;
		}

		bool TakesManaged(const llvm::FunctionType& type)
		{
			return HoldsManaged(type.getReturnType()) || std::any_of(type.param_begin(), type.param_end(), HoldsManaged);
		}

		// --- calls left as they are ---------------------------------------------------------------------------------

		// An intrinsic is no call RewriteStatepointsForGC makes a statepoint of.
		bool IsIntrinsic(const llvm::CallBase& call)
		{
			const llvm::Function* callee = call.getCalledFunction();
			return callee && callee->isIntrinsic();
		}

		// See InsertStatepoints: what RewriteStatepointsForGC cannot take, and what no record could describe. Inline
		// assembly is no call of a function at all; a statepoint of a variadic call loses what the convention does for
		// one (Windows passes a variadic double in both its registers).
		bool StaysLeaf(const llvm::CallBase& call)
		{
			if (call.isInlineAsm() || call.getFunctionType()->isVarArg()) return true;
			if (llvm::isa<llvm::InvokeInst>(call) || llvm::isa<llvm::CallBrInst>(call)) return true;
			if (call.getOperandBundle(llvm::LLVMContext::OB_funclet)) return true;
			if (call.doesNotReturn() || llvm::isa<llvm::UnreachableInst>(call.getNextNode())) return true;
			if (const auto* plain = llvm::dyn_cast<llvm::CallInst>(&call); plain && plain->isMustTailCall()) return true;
			// A call in tail position keeps its sibling-call optimization: its frame is gone while the callee runs, or read whole.
			if (const auto* plain = llvm::dyn_cast<llvm::CallInst>(&call); plain && plain->isTailCall())
				if (const auto* ret = llvm::dyn_cast<llvm::ReturnInst>(call.getNextNode()); ret && (!ret->getReturnValue() || ret->getReturnValue() == &call))
					return true;
			if (call.hasFnAttr(llvm::Attribute::ReturnsTwice)) return true;
			for (unsigned i = 0; i < call.arg_size(); ++i)
				if (call.isByValArgument(i)) return true;
			return false;
		}

		// The calls that become statepoints; the others are marked leaves.
		std::vector<llvm::CallBase*> Statepoints(llvm::Function& function)
		{
			std::vector<llvm::CallBase*> calls;
			for (llvm::Instruction& instruction : llvm::instructions(function))
			{
				auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
				if (!call || IsIntrinsic(*call) || call->hasFnAttr(c_LeafCall)) continue;
				if (StaysLeaf(*call)) call->addFnAttr(llvm::Attribute::get(function.getContext(), c_LeafCall));
				else calls.push_back(call);
			}
			return calls;
		}

		// --- raw addresses made managed pointers --------------------------------------------------------------------

		// A stack slot's or a global's address made a managed pointer: RewriteStatepointsForGC follows an addrspacecast to
		// its raw operand and takes that for the base, which it cannot relocate; an integer made a pointer is a base of
		// its own. It is made again right before each use (on a phi's incoming edge, at the end of that block), so it never
		// lives across a call: no object is there for a record to name, and a spill of it would cost every call.
		void CastsThroughIntegers(llvm::Function& function)
		{
			const llvm::DataLayout& layout = function.getDataLayout();
			std::vector<llvm::AddrSpaceCastInst*> casts;
			for (llvm::Instruction& instruction : llvm::instructions(function))
				if (auto* cast = llvm::dyn_cast<llvm::AddrSpaceCastInst>(&instruction); cast && IsManaged(cast->getType()))
					casts.push_back(cast);
			for (llvm::AddrSpaceCastInst* cast : casts)
			{
				llvm::Value* raw = cast->getPointerOperand();
				std::vector<llvm::Use*> uses;
				for (llvm::Use& use : cast->uses()) uses.push_back(&use);
				for (llvm::Use* use : uses)
				{
					auto* user = llvm::cast<llvm::Instruction>(use->getUser());
					auto* phi = llvm::dyn_cast<llvm::PHINode>(user);
					llvm::IRBuilder<> B(phi ? phi->getIncomingBlock(*use)->getTerminator() : user);
					llvm::Value* address = B.CreatePtrToInt(raw, layout.getIntPtrType(raw->getType()));
					use->set(B.CreateIntToPtr(address, cast->getType(), cast->getName()));
				}
				cast->eraseFromParent();
			}
		}

		// --- aggregates holding managed pointers ---------------------------------------------------------------------

		void Leaves(llvm::Type* type, std::vector<unsigned>& path, std::vector<std::vector<unsigned>>& leaves)
		{
			if (auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			{
				for (unsigned i = 0; i < structure->getNumElements(); ++i)
				{
					path.push_back(i);
					Leaves(structure->getElementType(i), path, leaves);
					path.pop_back();
				}
				return;
			}
			if (auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
			{
				for (unsigned i = 0; i < array->getNumElements(); ++i)
				{
					path.push_back(i);
					Leaves(array->getElementType(), path, leaves);
					path.pop_back();
				}
				return;
			}
			leaves.push_back(path);
		}

		// Where an aggregate's parts are taken: right after it; after its block's phis; at an invoke's normal successor,
		// its edge split when that block has other ways in; at the entry for an argument.
		llvm::BasicBlock::iterator AfterDefinition(llvm::Value* value)
		{
			if (auto* argument = llvm::dyn_cast<llvm::Argument>(value)) return argument->getParent()->getEntryBlock().getFirstInsertionPt();
			auto* instruction = llvm::cast<llvm::Instruction>(value);
			if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(instruction))
			{
				llvm::BasicBlock* normal = invoke->getNormalDest();
				if (!normal->getSinglePredecessor()) normal = llvm::SplitEdge(invoke->getParent(), normal);
				return normal->getFirstInsertionPt();
			}
			if (llvm::isa<llvm::PHINode>(instruction)) return instruction->getParent()->getFirstInsertionPt();
			return std::next(instruction->getIterator());
		}

		// A first-class aggregate holding managed pointers lives as its parts: RewriteStatepointsForGC tracks no pointer
		// inside one. Its parts are taken where it is made, and it is made again from them right before each use that
		// needs it whole (on a phi's incoming edge, at the end of that block), so none lives across a call.
		void SplitAggregate(llvm::Value* aggregate)
		{
			llvm::Type* type = aggregate->getType();
			std::vector<std::vector<unsigned>> leaves;
			std::vector<unsigned> path;
			Leaves(type, path, leaves);
			std::vector<llvm::Use*> uses;
			for (llvm::Use& use : aggregate->uses()) uses.push_back(&use);
			llvm::IRBuilder<> B(type->getContext());
			B.SetInsertPoint(AfterDefinition(aggregate));
			std::vector<llvm::Value*> parts;
			for (const std::vector<unsigned>& leaf : leaves) parts.push_back(B.CreateExtractValue(aggregate, leaf));
			for (llvm::Use* use : uses)
			{
				auto* user = llvm::cast<llvm::Instruction>(use->getUser());
				if (auto* extract = llvm::dyn_cast<llvm::ExtractValueInst>(user))
				{
					const auto leaf = std::find_if(leaves.begin(), leaves.end(), [&](const std::vector<unsigned>& l) { return llvm::ArrayRef<unsigned>(l) == extract->getIndices(); });
					if (leaf != leaves.end())
					{
						extract->replaceAllUsesWith(parts[leaf - leaves.begin()]);
						extract->eraseFromParent();
						continue;
					}
				}
				auto* phi = llvm::dyn_cast<llvm::PHINode>(user);
				B.SetInsertPoint(phi ? phi->getIncomingBlock(*use)->getTerminator() : user);
				llvm::Value* whole = llvm::PoisonValue::get(type);
				for (size_t i = 0; i < leaves.size(); ++i) whole = B.CreateInsertValue(whole, parts[i], leaves[i]);
				use->set(whole);
			}
		}

		void SplitAggregates(llvm::Function& function)
		{
			std::vector<llvm::Value*> aggregates;
			for (llvm::Argument& argument : function.args())
				if (argument.getType()->isAggregateType() && HoldsManaged(argument.getType())) aggregates.push_back(&argument);
			for (llvm::Instruction& instruction : llvm::instructions(function))
				if (instruction.getType()->isAggregateType() && HoldsManaged(instruction.getType())) aggregates.push_back(&instruction);
			for (llvm::Value* aggregate : aggregates) SplitAggregate(aggregate);
		}

		void ListFrameSlots(llvm::CallBase* call, const std::vector<llvm::Value*>& slots)
		{
			llvm::CallBase* listed = llvm::CallBase::addOperandBundle(call, llvm::LLVMContext::OB_deopt, llvm::OperandBundleDef("deopt", slots),
				call->getIterator());
			listed->takeName(call);
			call->replaceAllUsesWith(listed);
			call->eraseFromParent();
		}

		void Prepare(llvm::Function& function)
		{
			const std::vector<llvm::CallBase*> calls = Statepoints(function);
			CastsThroughIntegers(function);
			SplitAggregates(function);
			for (auto& [call, slots] : FrameSlotsAt(function, calls))
				if (!call->getOperandBundle(llvm::LLVMContext::OB_deopt)) ListFrameSlots(call, slots);
		}

		void Rewrite(llvm::Module& module, llvm::TargetMachine* machine)
		{
			llvm::LoopAnalysisManager loops;
			llvm::FunctionAnalysisManager functions;
			llvm::CGSCCAnalysisManager sccs;
			llvm::ModuleAnalysisManager modules;
			llvm::PassBuilder builder(machine);
			builder.registerModuleAnalyses(modules);
			builder.registerCGSCCAnalyses(sccs);
			builder.registerFunctionAnalyses(functions);
			builder.registerLoopAnalyses(loops);
			builder.crossRegisterProxies(loops, functions, sccs, modules);
			llvm::ModulePassManager passes;
			passes.addPass(llvm::RewriteStatepointsForGC());
			passes.run(module, modules);
		}


		// --- ELF -------------------------------------------------------------------------------------------------------

		template <typename T> T Read(const std::string& bytes, uint64_t at)
		{
			T value{};
			std::memcpy(&value, bytes.data() + at, sizeof(T));
			return value;
		}

		template <typename T> void Write(std::string& bytes, uint64_t at, T value) { std::memcpy(bytes.data() + at, &value, sizeof(T)); }
	}

	void MarkForStackMaps(llvm::Module& module)
	{
		if (!UsesStackMaps(module)) module.addModuleFlag(llvm::Module::Error, c_StackMapsFlag, 1);
		module.setDataLayout(StackMapsLayout(module.getDataLayoutStr()));
	}

	bool UsesStackMaps(const llvm::Module& module)
	{
		const auto* flag = llvm::mdconst::extract_or_null<llvm::ConstantInt>(module.getModuleFlag(c_StackMapsFlag));
		return flag && !flag->isZero();
	}

	std::string StackMapsLayout(const std::string& layout)
	{
		return layout.find("-ni:") == std::string::npos ? layout + "-ni:" + std::to_string(c_ManagedAddressSpace) : layout;
	}

	void KeepLibraryCallsAsWritten(llvm::Module& module)
	{
		if (!UsesStackMaps(module)) return;
		const llvm::TargetLibraryInfoImpl library(module.getTargetTriple());
		llvm::LibFunc known;
		for (llvm::Function& function : module)
			if (TakesManaged(*function.getFunctionType()) && library.getLibFunc(function, known))
				function.addFnAttr(llvm::Attribute::NoBuiltin);
	}

	void InsertStatepoints(llvm::Module& module, llvm::TargetMachine* machine)
	{
		if (!UsesStackMaps(module)) return;
		bool any = false;
		for (llvm::Function& function : module)
		{
			if (function.isDeclaration() || function.hasFnAttribute(llvm::Attribute::Naked)) continue;
			Prepare(function);
			function.setGC(c_StackMapsGc);
			any = true;
		}
		if (any) Rewrite(module, machine);
	}

	bool PrepareElfStackMaps(std::string& object)
	{
		constexpr uint64_t c_HeaderBytes = 64, c_SectionBytes = 64;
		constexpr uint64_t c_Write = 0x1, c_Retain = 0x200000; // SHF_WRITE, SHF_GNU_RETAIN
		if (object.size() < c_HeaderBytes || std::memcmp(object.data(), "\x7f" "ELF", 4) != 0 || object[4] != 2 || object[5] != 1) return false;
		const uint64_t table = Read<uint64_t>(object, 0x28);
		const uint64_t entryBytes = Read<uint16_t>(object, 0x3A);
		uint64_t count = Read<uint16_t>(object, 0x3C);
		uint64_t names = Read<uint16_t>(object, 0x3E);
		if (entryBytes < c_SectionBytes || table + entryBytes > object.size()) return false;
		if (count == 0) count = Read<uint64_t>(object, table + 32);      // e_shnum past SHN_LORESERVE: section 0's sh_size
		if (names == 0xFFFF) names = Read<uint32_t>(object, table + 40); // SHN_XINDEX: section 0's sh_link
		if (table + count * entryBytes > object.size() || names >= count) return false;
		const uint64_t namesAt = Read<uint64_t>(object, table + names * entryBytes + 24);
		const uint64_t namesSize = Read<uint64_t>(object, table + names * entryBytes + 32);
		const std::string wanted = ".llvm_stackmaps";
		for (uint64_t i = 0; i < count; ++i)
		{
			const uint64_t header = table + i * entryBytes;
			const uint32_t name = Read<uint32_t>(object, header);
			if (name + wanted.size() + 1 > namesSize || namesAt + name + wanted.size() + 1 > object.size()) continue;
			if (std::memcmp(object.data() + namesAt + name, wanted.c_str(), wanted.size() + 1) != 0) continue;
			Write<uint32_t>(object, header, name + 1); // "llvm_stackmaps": the same string past its dot
			Write<uint64_t>(object, header + 8, Read<uint64_t>(object, header + 8) | c_Write | c_Retain);
		}
		return true;
	}
}
