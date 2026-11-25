#include "Compiler/StackMapPlacement.h"
#include "Compiler/StackMapFormat.h"
#include <llvm/CodeGen/MachineBasicBlock.h>
#include <llvm/CodeGen/MachineFrameInfo.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/MachineInstr.h>
#include <llvm/CodeGen/TargetInstrInfo.h>
#include <llvm/CodeGen/TargetOpcodes.h>
#include <llvm/CodeGen/TargetSubtargetInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/Target/TargetMachine.h>
#include <algorithm>
#include <iterator>
#include <optional>

namespace C4Compiler
{
	namespace
	{
		bool IsCallRecord(const llvm::MachineInstr& instruction)
		{
			return instruction.getOpcode() == llvm::TargetOpcode::STACKMAP && instruction.getOperand(0).isImm() &&
				static_cast<uint64_t>(instruction.getOperand(0).getImm()) == c_CallRecordId;
		}

		// A call that leaves a return address: no stackmap, patchpoint or statepoint (each of those is a call to LLVM too).
		bool IsPlainCall(const llvm::MachineInstr& instruction)
		{
			const unsigned opcode = instruction.getOpcode();
			return instruction.isCall() && opcode != llvm::TargetOpcode::STACKMAP && opcode != llvm::TargetOpcode::PATCHPOINT &&
				opcode != llvm::TargetOpcode::STATEPOINT;
		}

		// The last call of `block` before `end`: nullopt when there is none, null when it is no plain one.
		std::optional<llvm::MachineInstr*> LastCallBefore(llvm::MachineBasicBlock& block, llvm::MachineBasicBlock::iterator end)
		{
			for (auto at = end; at != block.begin();)
			{
				--at;
				if (at->isCall()) return IsPlainCall(*at) ? &*at : nullptr;
			}
			return std::nullopt;
		}

		// The call `record` belongs to: the nearest call before it in its block; for an invoke's, which starts the invoke's
		// normal destination, the last call of the block before - its only predecessor, which an exception leaves for a
		// landing pad. Null when that is no plain call.
		llvm::MachineInstr* CallOf(llvm::MachineInstr& record)
		{
			llvm::MachineBasicBlock& block = *record.getParent();
			if (const std::optional<llvm::MachineInstr*> call = LastCallBefore(block, record.getIterator())) return *call;
			if (block.pred_size() != 1) return nullptr;
			llvm::MachineBasicBlock& invoking = **block.pred_begin();
			if (std::none_of(invoking.succ_begin(), invoking.succ_end(), [](const llvm::MachineBasicBlock* successor) { return successor->isEHPad(); }))
				return nullptr;
			return LastCallBefore(invoking, invoking.end()).value_or(nullptr);
		}

		// Whether code generation would put a nop right after the call `labelled` (a call record, or a statepoint) is the
		// label of (X86AsmPrinter::maybeEmitNopAfterCallForWindowsEH, for Windows' unwinder): when the next instruction that
		// makes code, past pseudo instructions and across blocks, begins an epilogue, or follows an EH label of a function
		// with exception handling. That nop would come before the label (a statepoint emits it right after its call), one
		// byte past the return address; a nop of our own right after the label makes the same code, and code generation
		// sees it as the instruction after the call.
		bool WantsNopAfter(const llvm::MachineInstr& labelled)
		{
			const llvm::MachineFunction& function = *labelled.getMF();
			const llvm::MCAsmInfo& asmInfo = *function.getTarget().getMCAsmInfo();
			if (asmInfo.getExceptionHandlingType() != llvm::ExceptionHandling::WinEH || asmInfo.getWinEHEncodingType() != llvm::WinEH::EncodingType::Itanium)
				return false;
			const bool handlesExceptions = function.getWinEHFuncInfo() != nullptr;
			const llvm::TargetInstrInfo& instructions = *function.getSubtarget().getInstrInfo();
			auto block = labelled.getParent()->getIterator();
			auto at = std::next(labelled.getIterator());
			for (;;)
			{
				for (; at != block->instr_end(); ++at)
				{
					if (at->isEHLabel())
					{
						if (handlesExceptions) return true;
						continue;
					}
					if (instructions.getName(at->getOpcode()) == "SEH_BeginEpilogue") return true;
					if (!at->isPseudo() && !at->isMetaInstruction()) return false;
				}
				if (++block == function.end()) return handlesExceptions;
				at = block->instr_begin();
			}
		}

		class StackMapPlacement : public llvm::MachineFunctionPass
		{
		public:
			static char ID;

			StackMapPlacement() : llvm::MachineFunctionPass(ID) {}

			llvm::StringRef getPassName() const override { return "C4 stack map placement"; }

			void getAnalysisUsage(llvm::AnalysisUsage& usage) const override
			{
				usage.setPreservesCFG();
				llvm::MachineFunctionPass::getAnalysisUsage(usage);
			}

			bool runOnMachineFunction(llvm::MachineFunction& function) override
			{
				if (!function.getFunction().hasGC()) return false;
				const llvm::TargetInstrInfo& instructions = *function.getSubtarget().getInstrInfo();
				bool changed = false;
				for (llvm::MachineBasicBlock& block : function)
					for (auto at = block.begin(); at != block.end();)
					{
						llvm::MachineInstr& instruction = *at++;
						if (IsCallRecord(instruction))
						{
							changed = true;
							llvm::MachineInstr* call = CallOf(instruction);
							if (!call)
							{
								instruction.eraseFromParent();
								continue;
							}
							call->getParent()->splice(std::next(call->getIterator()), &block, instruction.getIterator());
						}
						else if (instruction.getOpcode() != llvm::TargetOpcode::STATEPOINT) continue;
						if (!WantsNopAfter(instruction)) continue;
						instructions.insertNoop(block, std::next(instruction.getIterator()));
						changed = true;
					}
				return changed;
			}
		};

		char StackMapPlacement::ID = 0;
	}

	llvm::MachineFunctionPass* CreateStackMapPlacement() { return new StackMapPlacement(); }
}
