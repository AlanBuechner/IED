#include "Backend/CodeGen/IRContext.h"
#include "Backend/CodeGen/DebugInfo.h"
#include "Backend/CodeGen/VtableEmitter.h"

namespace Scripting
{
	IRContext::IRContext(ModuleContext& mctx, const HIRFunction& hir) : m_MCtx(mctx), m_Hir(hir) {}

	void IRContext::StartAt(llvm::BasicBlock& entry)
	{
		m_B = std::make_unique<llvm::IRBuilder<>>(m_MCtx.context());
		m_B->SetInsertPoint(&entry);
	}

	void IRContext::CreateSlotAllocas()
	{
		uint32 count = m_Hir.SlotCount();
		m_SlotAllocas.resize(count, nullptr);
		for (uint32 i = 0; i < count; ++i)
			m_SlotAllocas[i] = m_Hir.GetSlot(i).cls == SlotClass::Static ? CreateStaticSlot(m_Hir.GetSlot(i)) : CreateLocalSlot(m_Hir.GetSlot(i));
	}

	llvm::Value* IRContext::CreateLocalSlot(const HIRFunction::Slot& slot)
	{
		// Declarations are zero-initialized (no local ever holds stack garbage) and every vptr of a polymorphic
		// object points at its type's vtables.
		llvm::AllocaInst* storage = m_B->CreateAlloca(m_MCtx.Lower(slot.type), nullptr, "slot");
		m_MCtx.StoreZero(*m_B, slot.type, storage);
		m_MCtx.vtables().StoreVptrs(*m_B, slot.type, storage);
		return storage;
	}

	void IRContext::ResetSlot(uint32 slot)
	{
		const Type* t = m_Hir.GetSlot(slot).type;
		m_MCtx.StoreZero(*m_B, t, m_SlotAllocas[slot]);
		m_MCtx.vtables().StoreVptrs(*m_B, t, m_SlotAllocas[slot]);
	}

	llvm::Value* IRContext::CreateStaticSlot(const HIRFunction::Slot& slot)
	{
		// A static local is program-lifetime storage: a module global that starts zeroed with its vptrs set, or
		// holding its constant start. The name embeds the function's symbol so two functions' statics differ.
		const Core::String fnMangled = m_Hir.record ? m_Hir.record->mangledName : "fn";
		if (m_MCtx.Importing()) // the partition that defines the function has the storage
			return m_MCtx.DeclaredLocal(m_MCtx.LocalName("static." + std::string(fnMangled.c_str()) + "." + slot.name.c_str()),
				m_MCtx.Lower(slot.type), slot.isThreadLocal);
		llvm::Constant* init = slot.initial ? m_MCtx.ConstantOf(slot.initial->value, slot.type) : m_MCtx.vtables().ZeroImage(slot.type);
		if (!init) init = llvm::Constant::getNullValue(m_MCtx.Lower(slot.type));
		auto* storage = new llvm::GlobalVariable(m_MCtx.module(), init->getType(), false,
			llvm::GlobalValue::InternalLinkage, init, m_MCtx.LocalName("static." + std::string(fnMangled.c_str()) + "." + slot.name.c_str()));
		m_MCtx.MakeLocal(*storage); // the GC roots and a script host's table name it
		storage->setThreadLocal(slot.isThreadLocal);
		if (slot.type && slot.type->GetAlignment() > 1) storage->setAlignment(llvm::Align(slot.type->GetAlignment()));
		m_MCtx.NoteStaticStorage(storage, slot.type);
		return storage;
	}

	llvm::AllocaInst* IRContext::EntryAlloca(llvm::Type* ty, const char* name)
	{
		llvm::BasicBlock& entry = m_B->GetInsertBlock()->getParent()->getEntryBlock();
		llvm::IRBuilder<> atEntry(&entry, entry.begin());
		return atEntry.CreateAlloca(ty, nullptr, name);
	}

	llvm::Value* IRContext::Materialize(llvm::Value* v, llvm::Type* ty)
	{
		llvm::AllocaInst* tmp = EntryAlloca(ty);
		m_B->CreateStore(v, tmp);
		return tmp;
	}

	llvm::AllocaInst* IRContext::ReturnSlot(llvm::Type* ty)
	{
		if (!m_ReturnSlot) m_ReturnSlot = EntryAlloca(ty, "c4.ret");
		return m_ReturnSlot;
	}

	llvm::Value* IRContext::EnsureThrowSlot(const Type* t)
	{
		auto it = m_ThrowSlots.find(t);
		if (it != m_ThrowSlots.end()) return it->second;
		llvm::Value* slot = EntryAlloca(m_MCtx.Lower(t), "c4.throw_val");
		m_ThrowSlots[t] = slot;
		return slot;
	}

	llvm::BasicBlock* IRContext::UnwindTargetBelow(size_t index, size_t floor)
	{
		for (size_t i = index; i-- > floor; )
		{
			const CleanupEntry& e = m_Cleanups.At(i);
			if (e.kind == CleanupEntry::Kind::Try) return e.dispatch;
			if (e.HasPad()) return m_Region ? m_Region->CleanupPadFor(i) : nullptr;
		}
		return nullptr;
	}

	llvm::FuncletPadInst* IRContext::CurrentFunclet() const
	{
		if (llvm::CatchPadInst* handler = m_Cleanups.InnermostHandler(m_Floor)) return handler;
		return m_FloorFunclet;
	}

	IRContext::FuncletState IRContext::EnterCleanupFunclet(size_t floor, llvm::FuncletPadInst* pad)
	{
		FuncletState saved{ m_Floor, m_FloorFunclet };
		m_Floor = floor;
		m_FloorFunclet = pad;
		return saved;
	}

	llvm::CallBase* IRContext::Call(llvm::FunctionType* fty, llvm::Value* callee, llvm::ArrayRef<llvm::Value*> args, bool mayThrow)
	{
		std::vector<llvm::OperandBundleDef> bundles;
		if (llvm::FuncletPadInst* funclet = CurrentFunclet())
			bundles.emplace_back("funclet", std::vector<llvm::Value*>{ funclet });
		llvm::BasicBlock* unwind = mayThrow ? UnwindTarget() : nullptr;
		if (!unwind) return m_B->CreateCall(fty, callee, args, bundles);
		llvm::BasicBlock* normal = llvm::BasicBlock::Create(m_MCtx.context(), "invoke.cont", m_B->GetInsertBlock()->getParent());
		llvm::CallBase* invoke = m_B->CreateInvoke(fty, callee, normal, unwind, args, bundles);
		m_B->SetInsertPoint(normal);
		return invoke;
	}

	void IRContext::At(const SourceLocation& location)
	{
		if (!m_DebugScope) return;
		if (llvm::DILocation* at = m_MCtx.Debug()->At(location, m_DebugScope)) m_B->SetCurrentDebugLocation(at);
	}
} // namespace Scripting
