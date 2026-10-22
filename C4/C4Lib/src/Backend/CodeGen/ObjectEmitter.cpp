#include "Backend/CodeGen/ObjectEmitter.h"
#include "Backend/CodeGen/ArrayStorage.h"
#include "Backend/CodeGen/CallEmitter.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/StableNames.h"
#include "Backend/CodeGen/VtableEmitter.h"
#include "Model/TypeHandle.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/AggregateSlots.h"
#include <llvm/IR/Intrinsics.h>
#include <algorithm>

namespace Scripting
{
	namespace
	{
		constexpr size_t kUnlaidObjectSize = 64; // layout not computed: never under-allocate a managed object
		constexpr size_t kDefaultAlignment = 8;

		const ObjectType* AsObject(const Type* t)
		{
			return t && t->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(t) : nullptr;
		}
	}

	llvm::Value* ObjectEmitter::EmitNew(const NewNode* n)
	{
		// c4_alloc returns zeroed managed storage; placement storage is zeroed here. A heap object's
		// [operator("delete")] is registered as the GC finalizer, so it runs exactly once on reclamation
		// (explicit delete or the collector).
		if (n->count) return EmitNewArray(n);
		const Type* objT = n->objectType;
		llvm::Value* object = n->placement ? ZeroedPlacement(n) : Allocate(objT);
		Initialize(objT, object, n->ctor, n->aggregateInit, n->args);
		if (n->finalizer) RegisterFinalizer(object, *n->finalizer);
		return object;
	}

	void ObjectEmitter::RegisterFinalizer(llvm::Value* object, const FunctionRecord& finalizer)
	{
		ModuleContext& mc = m_Ctx.mctx();
		auto* ptr = llvm::PointerType::get(mc.context(), 0);
		auto* regFn = mc.RuntimeFunction("c4_gc_register_finalizer", llvm::Type::getVoidTy(mc.context()), { ptr, ptr });
		m_Ctx.Call(regFn, { object, mc.GetOrCreateFunction(finalizer) }, false);
	}

	llvm::Value* ObjectEmitter::Allocate(const Type* objT)
	{
		const size_t size = objT && objT->GetSize() ? objT->GetSize() : kUnlaidObjectSize;
		const size_t align = objT && objT->GetAlignment() ? objT->GetAlignment() : kDefaultAlignment;
		const std::string name = objT ? QualifiedName(objT).c_str() : "";
		return AllocateTyped(objT, name, m_Ctx.builder().getInt64(size), align);
	}

	llvm::Value* ObjectEmitter::AllocateTyped(const Type* type, const std::string& name, llvm::Value* size, uint64 align)
	{
		// A pointer-free object is never scanned by the collector (docs/runtime/01).
		const char* allocator = TypeHandle::MayHoldPointers(type) ? "c4_alloc_typed" : "c4_alloc_typed_noscan";
		auto& B = m_Ctx.builder();
		auto* allocFn = m_Ctx.mctx().RuntimeFunction(allocator, B.getPtrTy(), { B.getInt64Ty(), B.getInt64Ty(), B.getPtrTy() });
		return m_Ctx.Call(allocFn, { size, B.getInt64(align), TypeName(name) }, false);
	}

	llvm::Constant* ObjectEmitter::TypeName(const std::string& name)
	{
		llvm::Module& m = m_Ctx.mctx().module();
		auto* text = llvm::ConstantDataArray::getString(m.getContext(), name, true);
		const std::string symbol = StableNames::ForConstant("c4.typename", *text);
		if (llvm::GlobalVariable* same = m.getGlobalVariable(symbol, true)) return same;
		auto* global = new llvm::GlobalVariable(m, text->getType(), true, llvm::GlobalValue::LinkOnceODRLinkage, text, symbol);
		global->setComdat(m.getOrInsertComdat(symbol));
		return global;
	}

	llvm::Value* ObjectEmitter::EmitNewArray(const NewNode* n)
	{
		// Zeroed elements with their vptrs; the address of the first goes to `elements`, `initialize` starts them, and
		// only then does a heap array get the finalizer that destroys every element.
		auto& B = m_Ctx.builder();
		const Type* element = n->objectType;
		const uint64 stride = element->GetSize();
		llvm::Value* count = m_Expr.Emit(n->count);
		llvm::Value* bytes = B.CreateMul(count, B.getInt64(stride));
		llvm::Value* first = n->placement ? ZeroedRun(n, bytes) : AllocateRun(n, count, bytes);
		if (m_Ctx.mctx().vtables().HasVptrs(element))
			ArrayStorage::ForEach(B, first, count, stride, [&](llvm::Value* e) { m_Ctx.mctx().vtables().StoreVptrs(B, element, e); });
		llvm::Value* elements = m_Expr.LValueAddress(n->elements);
		B.CreateStore(first, elements);
		m_Ctx.EmitNestedBlock(n->initialize);
		first = B.CreateLoad(B.getPtrTy(), elements);
		if (n->finalizer)
		{
			const ArrayCount layout = Layout(element);
			llvm::Value* base = B.CreateGEP(B.getInt8Ty(), first, B.getInt64(-static_cast<int64>(ArrayStorage::CountBytes(element->GetAlignment()))));
			llvm::Function* finalizer = ArrayStorage::Finalizer(m_Ctx.mctx(), *n->finalizer, layout.perElement, layout.stride,
				ArrayStorage::CountBytes(element->GetAlignment()));
			auto* regFn = m_Ctx.mctx().RuntimeFunction("c4_gc_register_finalizer", B.getVoidTy(), { B.getPtrTy(), B.getPtrTy() });
			m_Ctx.Call(regFn, { base, finalizer }, false);
		}
		return first;
	}

	llvm::Value* ObjectEmitter::AllocateRun(const NewNode* n, llvm::Value* count, llvm::Value* bytes)
	{
		// A counted array keeps its element count before the first element, for its finalizer. At least one byte is
		// allocated, so the pointer to the first element always points into the allocation.
		auto& B = m_Ctx.builder();
		const Type* element = n->objectType;
		const uint64 countBytes = n->finalizer ? ArrayStorage::CountBytes(element->GetAlignment()) : 0;
		const uint64 align = std::max<uint64>(element->GetAlignment(), kDefaultAlignment);
		llvm::Value* size = B.CreateAdd(B.CreateBinaryIntrinsic(llvm::Intrinsic::umax, bytes, B.getInt64(1)), B.getInt64(countBytes));
		llvm::Value* base = AllocateTyped(element, std::string(QualifiedName(element).c_str()) + "[]", size, align);
		if (!countBytes) return base;
		B.CreateStore(count, base);
		return B.CreateGEP(B.getInt8Ty(), base, B.getInt64(countBytes));
	}

	llvm::Value* ObjectEmitter::ZeroedRun(const NewNode* n, llvm::Value* bytes)
	{
		llvm::Value* storage = m_Expr.Emit(n->placement);
		const llvm::Align align(n->objectType->GetAlignment() ? n->objectType->GetAlignment() : 1);
		m_Ctx.builder().CreateMemSet(storage, m_Ctx.builder().getInt8(0), bytes, align);
		return storage;
	}

	ObjectEmitter::ArrayCount ObjectEmitter::Layout(const Type* element)
	{
		// The innermost elements a finalizer destroys: a `T[2]` element is two T.
		ArrayCount layout{ 1, element->GetSize() };
		const Type* inner = TypeHandle::UnwrapToValue(element);
		while (const auto* array = dynamic_cast<const ArrayType*>(inner))
		{
			layout.perElement *= array->GetElementCount();
			inner = TypeHandle::UnwrapToValue(array->GetElementType());
			layout.stride = inner->GetSize();
		}
		return layout;
	}

	llvm::Value* ObjectEmitter::ZeroedPlacement(const NewNode* n)
	{
		llvm::Value* storage = m_Expr.Emit(n->placement);
		m_Ctx.mctx().StoreZero(m_Ctx.builder(), n->objectType, storage);
		return storage;
	}

	llvm::Value* ObjectEmitter::EmitRelocate(const RelocateNode* n)
	{
		// The bytes move (objects are trivially relocatable, D-05); the source is left zero with its own vptrs.
		auto& B = m_Ctx.builder();
		llvm::Value* dst = m_Expr.Emit(n->destination);
		llvm::Value* src = m_Expr.Emit(n->source);
		const uint64_t size = n->objectType->GetSize();
		const llvm::Align align(n->objectType->GetAlignment() ? n->objectType->GetAlignment() : 1);
		B.CreateMemMove(dst, align, src, align, size);
		B.CreateMemSet(src, B.getInt8(0), size, align);
		m_Ctx.mctx().vtables().StoreVptrs(B, n->objectType, src);
		return llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Ctx.mctx().context()), 0);
	}

	llvm::Value* ObjectEmitter::EmitConstruct(const ConstructNode* n)
	{
		// The object lives in a zeroed stack temporary and is yielded by value.
		llvm::Type* objTy = m_Ctx.mctx().Lower(n->objectType);
		llvm::Value* temp = m_Ctx.EntryAlloca(objTy, "construct");
		m_Ctx.mctx().StoreZero(m_Ctx.builder(), n->objectType, temp);
		Initialize(n->objectType, temp, n->ctor, n->aggregateInit, n->args);
		return m_Ctx.builder().CreateLoad(objTy, temp);
	}

	void ObjectEmitter::Initialize(const Type* objectType, llvm::Value* storage, const FunctionRecord* ctor, bool aggregateInit,
		const std::vector<const HIRNode*>& args)
	{
		// The vptrs are set before the constructor runs, so a virtual call during construction dispatches to
		// the type being built.
		m_Ctx.mctx().vtables().StoreVptrs(m_Ctx.builder(), objectType, storage);
		const ObjectType* obj = AsObject(objectType);
		if (ctor) CallEmitter(m_Ctx, m_Expr).EmitConstructor(*ctor, storage, args);
		else if (aggregateInit && obj && obj->IsPurePackedVector()) FillLanes(obj, storage, args);
		else if (aggregateInit && obj) FillFields(obj, storage, args);
	}

	// The arguments are the aggregate's slots in order (AggregateSlots), each stored at its own offset.
	void ObjectEmitter::FillFields(const ObjectType* obj, llvm::Value* storage, const std::vector<const HIRNode*>& args)
	{
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		const std::vector<AggregateSlots::Slot> slots = AggregateSlots::Of(*obj);
		for (size_t i = 0; i < slots.size() && i < args.size() && args[i]; ++i)
		{
			llvm::Value* addr = B.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx), storage,
				llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), slots[i].offset));
			m_Expr.GcStore(addr, m_Expr.Emit(args[i]), slots[i].type);
		}
	}

	void ObjectEmitter::FillLanes(const ObjectType* vector, llvm::Value* storage, const std::vector<const HIRNode*>& args)
	{
		// A pure packed vector IS its SIMD <N x T>: the braced elements are its lanes.
		auto& B = m_Ctx.builder();
		llvm::Type* vecTy = m_Ctx.mctx().Lower(vector);
		for (uint32_t i = 0; i < args.size() && args[i]; ++i)
		{
			llvm::Value* vec = B.CreateLoad(vecTy, storage);
			llvm::Value* lane = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Ctx.mctx().context()), i);
			B.CreateStore(B.CreateInsertElement(vec, m_Expr.Emit(args[i]), lane), storage);
		}
	}

	llvm::Value* ObjectEmitter::EmitInitList(const InitListNode* n)
	{
		// The elements go to a temporary array in this frame (valid for the call that receives the list and
		// for any later range-for over a stored copy); the value is the { const T* data, uint64 size } aggregate.
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		ModuleContext& mc = m_Ctx.mctx();
		const Type* elemType = static_cast<const InitListType*>(n->type)->GetElementType();
		llvm::Type* elemTy = mc.Storage(elemType);
		llvm::Value* arr = m_Ctx.EntryAlloca(llvm::ArrayType::get(elemTy, n->elements.size()), "inittmp");
		for (size_t i = 0; i < n->elements.size(); ++i)
		{
			llvm::Value* slotAddr = B.CreateInBoundsGEP(elemTy, arr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), i));
			m_Expr.GcStore(slotAddr, m_Expr.Emit(n->elements[i]), elemType);
		}
		llvm::StructType* listTy = llvm::cast<llvm::StructType>(mc.Lower(n->type));
		llvm::Value* agg = B.CreateInsertValue(llvm::PoisonValue::get(listTy), arr, { 0 });
		return B.CreateInsertValue(agg, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), n->elements.size()), { 1 });
	}

	llvm::Value* ObjectEmitter::EmitDelete(const DeleteNode* n)
	{
		// c4_free runs the [operator("delete")] finalizer registered at allocation, then frees; null is a no-op.
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		llvm::Value* object = m_Expr.Emit(n->operand);
		auto* freeFn = m_Ctx.mctx().RuntimeFunction("c4_free", llvm::Type::getVoidTy(ctx), { llvm::PointerType::get(ctx, 0) });
		m_Ctx.Call(freeFn, { object }, true);
		return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);
	}
} // namespace Scripting
