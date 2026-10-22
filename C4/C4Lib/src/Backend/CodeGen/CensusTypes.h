#pragma once
#include "Model/SymbolTable.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Scripting
{
	class ModuleContext;

	/// The census's record of each type `new` makes (c4_gc_type, docs/runtime/01): its name and the function that
	/// counts the native memory one of its objects holds, null for a type that holds none. The functions are declared
	/// here and lowered by the driver (TakeFunctions), with the bodies emitted code reaches.
	class CensusTypes
	{
	public:
		explicit CensusTypes(ModuleContext& mctx) : m_MCtx(mctx) {}

		/// The record of one object of `type`, named `name`.
		llvm::Constant* ObjectRecord(const Type* type, const std::string& name);
		/// The record of an array of `element`s named `name`, whose element count sits `countBytes` before the first
		/// element; 0: the count is not kept, so its native memory cannot be counted.
		llvm::Constant* ArrayRecord(const Type* element, const std::string& name, uint64 countBytes);

		struct Function
		{
			const Type* type = nullptr;
			uint64 countBytes = 0;                 // > 0: of a counted array of `type`
			const FunctionRecord* record = nullptr;
		};
		/// The functions the records declared since the last call, for the driver to lower.
		std::vector<Function> TakeFunctions() { return std::move(m_New); }

	private:
		// The record of `type` (`array`: of arrays of it), made by `counter` the first time.
		llvm::Constant* Record(const Type* type, bool array, const std::string& name, const std::function<llvm::Constant*()>& counter);
		llvm::Constant* Counter(const Type* type, uint64 countBytes);
		llvm::Constant* NoCounter();

		ModuleContext& m_MCtx;
		std::map<std::pair<const Type*, bool>, llvm::GlobalVariable*> m_Records;   // by type, and whether of arrays
		std::deque<FunctionRecord> m_Functions;
		std::vector<Function> m_New;
	};
} // namespace Scripting
