#include "Compiler/CodeViewLocations.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugProgramInstruction.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <vector>

namespace Scripting
{
	namespace
	{
		// The locations a function gives one variable: its constant ones, and whether it has any other.
		struct VariableLocations
		{
			std::vector<llvm::DbgVariableRecord*> constants;
			bool hasOther = false;
			bool constantsDisagree = false;
		};

		// A location every operand of which is a constant; a killed one (undef, poison) is not a value.
		bool IsConstant(const llvm::DbgVariableRecord& record)
		{
			if (record.isKillLocation()) return false;
			for (const llvm::Value* operand : record.location_ops())
				if (!llvm::isa<llvm::Constant>(operand) || llvm::isa<llvm::UndefValue>(operand)) return false;
			return true;
		}

		bool SameConstant(const llvm::DbgVariableRecord& a, const llvm::DbgVariableRecord& b)
		{
			return a.getRawLocation() == b.getRawLocation() && a.getExpression() == b.getExpression();
		}

		void Note(VariableLocations& locations, llvm::DbgVariableRecord& record)
		{
			if (!IsConstant(record))
			{
				locations.hasOther = true;
				return;
			}
			if (!locations.constants.empty() && !SameConstant(*locations.constants.front(), record)) locations.constantsDisagree = true;
			locations.constants.push_back(&record);
		}

		// The variable as CodeView sees it: all its fragments are one variable (one constant makes all of it S_CONSTANT).
		llvm::DebugVariable WholeVariable(const llvm::DbgVariableRecord& record)
		{
			return llvm::DebugVariable(record.getVariable(), std::nullopt, record.getDebugLoc().getInlinedAt());
		}

		llvm::DenseMap<llvm::DebugVariable, VariableLocations> LocationsIn(llvm::Function& function)
		{
			llvm::DenseMap<llvm::DebugVariable, VariableLocations> variables;
			for (llvm::BasicBlock& block : function)
				for (llvm::Instruction& instruction : block)
					for (llvm::DbgVariableRecord& record : llvm::filterDbgVars(instruction.getDbgRecordRange()))
						if (record.isDbgValue()) Note(variables[WholeVariable(record)], record);
			return variables;
		}
	}

	void DropPartialConstantLocations(llvm::Module& module)
	{
		if (!module.getCodeViewFlag()) return;
		for (llvm::Function& function : module)
		{
			if (function.isDeclaration()) continue;
			for (auto& entry : LocationsIn(function))
				if (entry.second.hasOther || entry.second.constantsDisagree)
					for (llvm::DbgVariableRecord* record : entry.second.constants) record->setKillLocation();
		}
	}
}
