#pragma once
#include "Backend/CodeGen/PointerMaps.h"
#include "Model/SymbolTable.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Scripting
{
	class ModuleContext;
	struct CensusFunction;
	struct LoweredProgram;

	/// The record of each type `new` makes (c4_gc_type, docs/runtime/01): its name and the function that counts the
	/// native memory one of its objects holds, null for a type that holds none, for the census; its pointer map
	/// (PointerMaps.h), which the collector reads its objects by (R16). The counters are the ones lowering made
	/// (LoweredProgram::census, from what the code allocates); each module declares the ones its code calls as locals of
	/// its own, and its emitter defines them (TakeFunctions).
	class CensusTypes
	{
	public:
		explicit CensusTypes(ModuleContext& mctx) : m_MCtx(mctx) {}

		/// The counters this module's code may call.
		void Use(const LoweredProgram& program) { m_Program = &program; }

		/// Whether `type`'s record has a pointer map (its objects are allocated with c4_alloc_precise).
		bool HasPointerMap(const Type* type) { return MapOf(type).has_value(); }

		/// The record of one object of `type`, named `name`.
		llvm::Constant* ObjectRecord(const Type* type, const std::string& name);
		/// The record of an array of `element`s named `name`, whose element count sits `countBytes` before the first
		/// element; 0: the count is not kept, so its native memory cannot be counted.
		llvm::Constant* ArrayRecord(const Type* element, const std::string& name, uint64 countBytes);

		/// The counters the records declared since the last call, for the module to define.
		std::vector<const CensusFunction*> TakeFunctions() { return std::move(m_New); }
		/// The types code allocated whose counter lowering did not make (an internal error: lowering and codegen disagree
		/// about what the code allocates); their records count nothing.
		const std::vector<const Type*>& Unplanned() const { return m_Unplanned; }

	private:
		// What a record describes: one object, or an array of them whose first element is `countBytes` in.
		struct Shape
		{
			bool array;
			uint64 countBytes;
		};

		// The record of `type` (an array's: of its elements), made by `counter` the first time.
		llvm::Constant* Record(const Type* type, Shape shape, const std::string& name, const std::function<llvm::Constant*()>& counter);
		llvm::Constant* Counter(const Type* type, uint64 countBytes);
		llvm::Constant* NoCounter();
		const std::optional<PointerMap>& MapOf(const Type* type);

		ModuleContext& m_MCtx;
		const LoweredProgram* m_Program = nullptr;
		std::map<std::pair<const Type*, bool>, llvm::GlobalVariable*> m_Records;   // by type, and whether of arrays
		std::map<const Type*, std::optional<PointerMap>> m_Maps;
		std::vector<const CensusFunction*> m_New;
		std::vector<const Type*> m_Unplanned;
	};
} // namespace Scripting
