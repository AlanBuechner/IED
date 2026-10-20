#include "Backend/CodeGen/VtableEmitter.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include <algorithm>

namespace Scripting
{
	const std::vector<VtableEmitter::VptrSite>& VtableEmitter::VptrSites(const Type* t)
	{
		auto it = m_Sites.find(t);
		if (it != m_Sites.end()) return it->second;
		std::vector<VptrSite> sites = CollectSites(t);
		return m_Sites[t] = std::move(sites);
	}

	std::vector<VtableEmitter::VptrSite> VtableEmitter::CollectSites(const Type* t)
	{
		std::vector<VptrSite> sites;
		const Type* value = TypeHandle::UnwrapToValue(t);
		if (!value) return sites;
		if (value->GetKind() == TypeKind::Object)
			AppendObjectSites(static_cast<const ObjectType*>(value), 0, sites);
		else if (value->GetKind() == TypeKind::Array)
		{
			const auto* arr = static_cast<const ArrayType*>(value);
			const std::vector<VptrSite>& element = VptrSites(arr->GetElementType());
			int64 stride = static_cast<int64>(arr->GetElementType()->GetSize());
			for (uint64 i = 0; !element.empty() && i < arr->GetElementCount(); ++i)
				for (const VptrSite& s : element) sites.push_back({ static_cast<int64>(i) * stride + s.offset, s.vtable });
		}
		return sites;
	}

	void VtableEmitter::AppendObjectSites(const ObjectType* obj, int64 offset, std::vector<VptrSite>& sites)
	{
		for (const VirtualTables::VptrLocation& loc : VirtualTables::VptrLocations(obj))
			sites.push_back({ offset + loc.offset, Vtable(obj, loc) });
		ClassHierarchy::ForEachSubobject(obj, [&](const ObjectType* sub, int64 subOffset) {
			for (const auto& f : sub->GetFields())
			{
				if (f.m_IsStatic || !f.m_Type) continue;
				for (const VptrSite& s : VptrSites(f.m_Type))
					sites.push_back({ offset + subOffset + static_cast<int64>(f.m_Offset) + s.offset, s.vtable });
			}
		});
	}

	void VtableEmitter::StoreVptrs(llvm::IRBuilder<>& B, const Type* t, llvm::Value* addr)
	{
		auto* i8 = llvm::Type::getInt8Ty(m_MCtx.context());
		auto* i64 = llvm::Type::getInt64Ty(m_MCtx.context());
		for (const VptrSite& s : VptrSites(t))
			B.CreateStore(s.vtable, s.offset ? B.CreateInBoundsGEP(i8, addr, llvm::ConstantInt::get(i64, s.offset)) : addr);
	}

	llvm::Constant* VtableEmitter::ZeroImage(const Type* t)
	{
		const std::vector<VptrSite>& sites = VptrSites(t);
		if (sites.empty()) return nullptr;
		llvm::LLVMContext& ctx = m_MCtx.context();
		auto* i8 = llvm::Type::getInt8Ty(ctx);
		std::vector<VptrSite> ordered = sites;
		std::sort(ordered.begin(), ordered.end(), [](const VptrSite& a, const VptrSite& b) { return a.offset < b.offset; });
		std::vector<llvm::Constant*> parts;
		int64 cursor = 0;
		auto zeros = [&](int64 count) {
			if (count > 0) parts.push_back(llvm::ConstantAggregateZero::get(llvm::ArrayType::get(i8, static_cast<uint64>(count))));
		};
		for (const VptrSite& s : ordered)
		{
			zeros(s.offset - cursor);
			parts.push_back(s.vtable);
			cursor = s.offset + static_cast<int64>(sizeof(void*));
		}
		zeros(static_cast<int64>(t->GetSize()) - cursor);
		return llvm::ConstantStruct::getAnon(ctx, parts, true);
	}

	std::string VtableEmitter::VtableSymbol(const ObjectType* complete, int64 vptrOffset) const
	{
		return "__c4_vtable_" + std::string(m_MCtx.model()->TypeSymbol(complete).c_str()) + "_" + std::to_string(vptrOffset);
	}

	llvm::Constant* VtableEmitter::Vtable(const ObjectType* complete, const VirtualTables::VptrLocation& location)
	{
		auto key = std::make_pair(complete, location.offset);
		auto it = m_Vtables.find(key);
		if (it != m_Vtables.end()) return it->second;
		llvm::LLVMContext& ctx = m_MCtx.context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		std::vector<llvm::Constant*> entries;
		for (const VirtualTables::Slot& slot : VirtualTables::PrimarySlots(location.layout))
			entries.push_back(Entry(complete, location, slot));
		auto* entriesTy = llvm::ArrayType::get(ptr, entries.size());
		auto* init = llvm::ConstantStruct::getAnon(ctx, { m_Rtti.CompleteObjectLocator(complete, location.offset),
			llvm::ConstantArray::get(entriesTy, entries) });
		// Named for the type, the same in every program: a script uses its host's vtables (docs/runtime/04 section 4.2).
		auto* table = new llvm::GlobalVariable(m_MCtx.module(), init->getType(), true, llvm::GlobalValue::InternalLinkage, init,
			VtableSymbol(complete, location.offset));
		// The vptr points past the locator, at entry 0; the locator is vtable[-1].
		auto* i32 = llvm::Type::getInt32Ty(ctx);
		llvm::Constant* first = llvm::ConstantExpr::getInBoundsGetElementPtr(init->getType(), table,
			llvm::ArrayRef<llvm::Constant*>{ llvm::ConstantInt::get(i32, 0), llvm::ConstantInt::get(i32, 1), llvm::ConstantInt::get(i32, 0) });
		m_Vtables[key] = first;
		return first;
	}

	llvm::Constant* VtableEmitter::Entry(const ObjectType* complete, const VirtualTables::VptrLocation& location, const VirtualTables::Slot& slot)
	{
		VirtualTables::Overrider ov = VirtualTables::FinalOverrider(complete, location, slot);
		const FunctionRecord* rec = m_MCtx.model() ? m_MCtx.model()->RecordForMethod(ov.owner, ov.method) : nullptr;
		if (!rec || !(rec->bodyNode || rec->isLinked || rec->isHostDefined)) return PureCall();
		int64 delta = ov.ownerOffset - location.offset; // the overrider's `this` relative to this vptr's sub-object
		if (delta == 0) return m_MCtx.GetOrCreateFunction(*rec);
		return Thunk(*rec, delta);
	}

	llvm::Function* VtableEmitter::Thunk(const FunctionRecord& target, int64 delta)
	{
		auto key = std::make_pair(&target, delta);
		auto it = m_Thunks.find(key);
		if (it != m_Thunks.end()) return it->second;
		llvm::Function* callee = m_MCtx.GetOrCreateFunction(target);
		std::string name = "c4.thunk." + callee->getName().str() + "." + (delta < 0 ? "m" + std::to_string(-delta) : std::to_string(delta));
		llvm::Function* thunk = llvm::Function::Create(callee->getFunctionType(), llvm::Function::InternalLinkage, name, m_MCtx.module());
		thunk->setAttributes(callee->getAttributes());
		thunk->addFnAttr(llvm::Attribute::UWTable);
		m_Thunks[key] = thunk;

		llvm::IRBuilder<> B(llvm::BasicBlock::Create(m_MCtx.context(), "entry", thunk));
		const int thisIndex = m_MCtx.SignatureOf(target).thisIndex;
		std::vector<llvm::Value*> args;
		for (llvm::Argument& a : thunk->args()) args.push_back(&a);
		args[thisIndex] = B.CreateInBoundsGEP(llvm::Type::getInt8Ty(m_MCtx.context()), args[thisIndex],
			llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_MCtx.context()), delta));
		llvm::CallInst* call = B.CreateCall(callee, args);
		call->setAttributes(callee->getAttributes());
		call->setTailCall();
		if (call->getType()->isVoidTy()) B.CreateRetVoid();
		else B.CreateRet(call);
		return thunk;
	}

	llvm::Function* VtableEmitter::PureCall()
	{
		llvm::Function* fn = m_MCtx.RuntimeFunction("c4_purecall", llvm::Type::getVoidTy(m_MCtx.context()), {});
		fn->setDoesNotReturn();
		return fn;
	}
} // namespace Scripting
