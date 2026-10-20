#pragma once
#include "Backend/CodeGen/RttiEmitter.h"
#include "Model/Types/VirtualTables.h"
#include "Model/SymbolTable.h"
#include <llvm/IR/IRBuilder.h>
#include <map>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class ModuleContext;

	/// The vtables of complete objects (D-09, docs/compiler/04) and the vptrs that point at them. A complete
	/// object has one vtable per vptr location: `{ CompleteObjectLocator, entries... }` with the vptr pointing
	/// at the first entry. An entry whose final overrider lives in another sub-object is a thunk that moves
	/// `this` there; a slot with no implementation calls c4_purecall.
	class VtableEmitter
	{
	public:
		VtableEmitter(ModuleContext& mctx, RttiEmitter& rtti) : m_MCtx(mctx), m_Rtti(rtti) {}

		/// Every vptr inside a complete object of type `t`, nested members and array elements included.
		struct VptrSite { int64 offset = 0; llvm::Constant* vtable = nullptr; };
		const std::vector<VptrSite>& VptrSites(const Type* t);
		bool HasVptrs(const Type* t) { return !VptrSites(t).empty(); }

		/// Point every vptr of the complete `t` object at `addr` at its vtable.
		void StoreVptrs(llvm::IRBuilder<>& B, const Type* t, llvm::Value* addr);

		/// The initial bytes of a zeroed `t` object with its vptrs set (a static's initializer); null when
		/// `t` has no vptr.
		llvm::Constant* ZeroImage(const Type* t);

	private:
		std::vector<VptrSite> CollectSites(const Type* t);
		void AppendObjectSites(const ObjectType* obj, int64 offset, std::vector<VptrSite>& sites);
		llvm::Constant* Vtable(const ObjectType* complete, const VirtualTables::VptrLocation& location);
		/// `__c4_vtable_<type symbol>_<vptr offset>`.
		std::string VtableSymbol(const ObjectType* complete, int64 vptrOffset) const;
		llvm::Constant* Entry(const ObjectType* complete, const VirtualTables::VptrLocation& location, const VirtualTables::Slot& slot);
		llvm::Function* Thunk(const FunctionRecord& target, int64 delta);
		llvm::Function* PureCall();

		ModuleContext& m_MCtx;
		RttiEmitter& m_Rtti;
		std::unordered_map<const Type*, std::vector<VptrSite>> m_Sites;
		std::map<std::pair<const ObjectType*, int64>, llvm::Constant*> m_Vtables;
		std::map<std::pair<const FunctionRecord*, int64>, llvm::Function*> m_Thunks;
	};
} // namespace Scripting
