#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/NormalizedConversion.h"
#include "Model/Types/SwizzleType.h"
#include "Backend/CodeGen/IntegerPower.h"
#include "Backend/CodeGen/CallEmitter.h"
#include "Backend/CodeGen/ObjectEmitter.h"
#include "Backend/CodeGen/FuncEmitter.h"
#include "Backend/CodeGen/VtableEmitter.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/TypeHandle.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h> // CreateVAStart / CreateVAEnd free functions

namespace Scripting
{
	llvm::Value* ExprEmitter::Emit(const HIRNode* node)
	{
		if (!node) return nullptr;
		switch (node->kind)
		{
		case HIRNode::Kind::Constant:           return EmitConstant(static_cast<const ConstantNode*>(node));
		case HIRNode::Kind::Load:               return EmitLoad(static_cast<const LoadNode*>(node));
		case HIRNode::Kind::Store:              return EmitStore(static_cast<const StoreNode*>(node));
		case HIRNode::Kind::IntrinsicOp:        return EmitIntrinsicOp(static_cast<const IntrinsicOpNode*>(node));
		case HIRNode::Kind::Convert:            return EmitConvert(static_cast<const ConvertNode*>(node));
		case HIRNode::Kind::AddressOf:          return EmitAddressOf(static_cast<const AddressOfNode*>(node));
		case HIRNode::Kind::AddressArithmetic:  return EmitAddressArithmetic(static_cast<const AddressArithmeticNode*>(node));
		case HIRNode::Kind::Call:               return CallEmitter(m_Ctx, *this).Emit(static_cast<const CallNode*>(node));
		case HIRNode::Kind::New:                return ObjectEmitter(m_Ctx, *this).EmitNew(static_cast<const NewNode*>(node));
		case HIRNode::Kind::Delete:             return ObjectEmitter(m_Ctx, *this).EmitDelete(static_cast<const DeleteNode*>(node));
		case HIRNode::Kind::Construct:          return ObjectEmitter(m_Ctx, *this).EmitConstruct(static_cast<const ConstructNode*>(node));
		case HIRNode::Kind::InitList:           return ObjectEmitter(m_Ctx, *this).EmitInitList(static_cast<const InitListNode*>(node));
		case HIRNode::Kind::DynamicCast:        return EmitDynamicCast(static_cast<const DynamicCastNode*>(node));
		case HIRNode::Kind::Relocate:           return ObjectEmitter(m_Ctx, *this).EmitRelocate(static_cast<const RelocateNode*>(node));
		case HIRNode::Kind::FunctionAddress:    return EmitFunctionAddress(static_cast<const FunctionAddressNode*>(node));
		case HIRNode::Kind::MakeFunc:           return FuncEmitter(m_Ctx, *this).EmitMakeFunc(static_cast<const MakeFuncNode*>(node));
		case HIRNode::Kind::FuncCode:           return m_Ctx.builder().CreateExtractValue(Emit(static_cast<const FuncCodeNode*>(node)->func), { 0 });
		case HIRNode::Kind::VectorBuild:        return EmitVectorBuild(static_cast<const VectorBuildNode*>(node));
		case HIRNode::Kind::Sequence:           return EmitSequence(static_cast<const SequenceNode*>(node));
		case HIRNode::Kind::Transfer:           return Emit(static_cast<const TransferNode*>(node)->copy); // a copy, or LastUseMoves' move
		case HIRNode::Kind::Va:                 return EmitVa(static_cast<const VaNode*>(node));
		case HIRNode::Kind::Atomic:             return EmitAtomic(static_cast<const AtomicNode*>(node));
		case HIRNode::Kind::Simd:               return EmitSimd(static_cast<const SimdNode*>(node));
		case HIRNode::Kind::Ternary:            return EmitTernary(static_cast<const TernaryNode*>(node));
		default:
			// Region/ternary/new/delete/is-as and statement nodes are not rvalue expressions here; the
			// RegionEmitter or StmtEmitter owns them. Reaching one is an emission-shape bug.
			return nullptr;
		}
	}

	llvm::Value* ExprEmitter::GlobalAddress(const SymbolInfo& sym)
	{
		llvm::Function* start = m_Ctx.mctx().FirstUseStart(sym);
		if (start && start != m_Ctx.builder().GetInsertBlock()->getParent()) m_Ctx.Call(start, {}, true); // not from its own start
		return m_Ctx.mctx().GlobalAddress(sym);
	}

	llvm::Value* ExprEmitter::LValueAddress(const LValue* loc)
	{
		auto& B = m_Ctx.builder();
		switch (loc->kind)
		{
		case LValue::Kind::Slot:   return m_Ctx.SlotAlloca(loc->slot);
		case LValue::Kind::This:   return m_Ctx.This();
		case LValue::Kind::Global: return GlobalAddress(*loc->sym);
		case LValue::Kind::Deref:  return Emit(loc->addr); // the pointer rvalue IS the address
		case LValue::Kind::Field:
		{
			// Byte-offset GEP via the i8 idiom: the field offset is relative to the object that declares it,
			// which `base` names (HIR moves an inherited field's base to its declaring sub-object).
			llvm::Value* base = LValueAddress(loc->base);
			uint64_t off = loc->field ? loc->field->m_Offset : 0;
			if (off == 0) return base;
			return B.CreateInBoundsGEP(llvm::Type::getInt8Ty(m_Ctx.mctx().context()), base,
				llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Ctx.mctx().context()), off));
		}
		case LValue::Kind::Base:
		{
			llvm::Value* base = LValueAddress(loc->base);
			if (loc->baseOffset == 0) return base;
			return B.CreateInBoundsGEP(llvm::Type::getInt8Ty(m_Ctx.mctx().context()), base,
				llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Ctx.mctx().context()), loc->baseOffset));
		}
		case LValue::Kind::Lane:
			// HIR binds no reference to a lane (D-37, BUG-429); should anything ask, a packed view's lanes are contiguous
			// elements with no padding, so a lane is at the vector's address plus its index - never the vector's first lane.
			return B.CreateInBoundsGEP(m_Ctx.mctx().Lower(loc->type), LValueAddress(loc->base),
				llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Ctx.mctx().context()), loc->laneIndex));
		case LValue::Kind::Swizzle:
			// A multi-lane run is a value of the expression, not storage: a reference to it is to a temporary holding its lanes.
			return m_Ctx.Materialize(LoadRun(loc), m_Ctx.mctx().Lower(loc->type));
		case LValue::Kind::VectorLaneDyn:
			return DynamicLaneAddress(loc);
		case LValue::Kind::IndexOp:
			// A by-value indexer result has no storage of its own: its address is a temporary holding the
			// getter's result (binding it to a const reference); HIR rejects writes through it.
			return m_Ctx.Materialize(Emit(loc->read), m_Ctx.mctx().Lower(loc->type));
		}
		return nullptr;
	}

	llvm::Value* ExprEmitter::EmitConstant(const ConstantNode* n)
	{
		return m_Ctx.mctx().ConstantOf(n->value, n->type);
	}

	llvm::Value* ExprEmitter::EmitLoad(const LoadNode* n)
	{
		if (n->loc->kind == LValue::Kind::IndexOp)
		{
			// A by-value overloaded index: the "load" IS the getter call (already built at lowering).
			return Emit(n->loc->read);
		}
		if (n->loc->kind == LValue::Kind::Lane)
		{
			// SIMD lane: load the whole vector, then extract the element. A lane has no address.
			auto& B = m_Ctx.builder();
			const LValue* vector = n->loc->base;
			llvm::Value* vec = Load(m_Ctx.mctx().Lower(vector->type), LValueAddress(vector), AccessOf(vector, vector->type)); // the full <N x T>
			auto idx = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Ctx.mctx().context()), n->loc->laneIndex);
			return B.CreateExtractElement(vec, idx);
		}
		if (n->loc->kind == LValue::Kind::Swizzle) return LoadRun(n->loc);
		if (n->loc->kind == LValue::Kind::VectorLaneDyn)
		{
			// a.data[i], v.zyx[i]: the vector's (or the run's) lanes, extracted at the RUNTIME index.
			auto& B = m_Ctx.builder();
			const LValue* vector = n->loc->base;
			llvm::Value* lanes = vector->kind == LValue::Kind::Swizzle ? LoadRun(vector)
				: Load(m_Ctx.mctx().Lower(vector->type), LValueAddress(vector), AccessOf(vector, vector->type));
			return B.CreateExtractElement(lanes, Emit(n->loc->index));
		}
		llvm::Value* addr = LValueAddress(n->loc);
		llvm::Value* value = Load(m_Ctx.mctx().Lower(n->type), addr, AccessOf(n->loc, n->type));
		if (MayBeSubobject(n->loc) && m_Ctx.mctx().vtables().HasVptrs(n->type)) return WithOwnVptrs(value, n->type);
		return value;
	}

	// A multi-lane run (`v.zyx`): the full <N x T> loaded, its named lanes gathered into a fresh <K x T>.
	llvm::Value* ExprEmitter::LoadRun(const LValue* run)
	{
		auto& B = m_Ctx.builder();
		llvm::Type* i32 = llvm::Type::getInt32Ty(m_Ctx.mctx().context());
		llvm::Value* vec = Load(m_Ctx.mctx().Lower(run->base->type), LValueAddress(run->base), AccessOf(run->base, run->base->type));
		llvm::Value* result = llvm::PoisonValue::get(m_Ctx.mctx().Lower(run->type));
		for (size_t i = 0; i < run->laneIndices.size(); ++i)
		{
			llvm::Value* elem = B.CreateExtractElement(vec, llvm::ConstantInt::get(i32, run->laneIndices[i]));
			result = B.CreateInsertElement(result, elem, llvm::ConstantInt::get(i32, i));
		}
		return result;
	}

	// `a.data[i]`, `v.zyx[i]`: the lane at the runtime index (through a run, the vector lane the run names there), at its
	// place among the vector's contiguous elements.
	llvm::Value* ExprEmitter::DynamicLaneAddress(const LValue* loc)
	{
		auto& B = m_Ctx.builder();
		const LValue* run = loc->base->kind == LValue::Kind::Swizzle ? loc->base : nullptr;
		const LValue* vector = run ? run->base : loc->base;
		llvm::Value* lane = Emit(loc->index);
		if (run) lane = B.CreateExtractElement(RunLaneTable(run), B.CreateIntCast(lane, llvm::Type::getInt32Ty(m_Ctx.mctx().context()), true));
		lane = B.CreateIntCast(lane, llvm::Type::getInt64Ty(m_Ctx.mctx().context()), !run);
		return B.CreateInBoundsGEP(m_Ctx.mctx().Lower(loc->type), LValueAddress(vector), lane);
	}

	// The lanes a run names, as a constant <K x i32>: element i is the vector lane the run's i-th lane is.
	llvm::Value* ExprEmitter::RunLaneTable(const LValue* run)
	{
		std::vector<uint32_t> lanes(run->laneIndices.begin(), run->laneIndices.end());
		return llvm::ConstantDataVector::get(m_Ctx.mctx().context(), lanes);
	}

	bool ExprEmitter::MayBeSubobject(const LValue* loc)
	{
		// Storage reached through a pointer, `this` or a base view can be part of a more-derived object, whose
		// vptrs a copy must not take and an assignment must not replace. Slots, globals and fields are
		// complete objects of their declared type.
		return loc->kind == LValue::Kind::Deref || loc->kind == LValue::Kind::This || loc->kind == LValue::Kind::Base;
	}

	llvm::Value* ExprEmitter::WithOwnVptrs(llvm::Value* value, const Type* t)
	{
		llvm::Value* temp = m_Ctx.Materialize(value, value->getType());
		m_Ctx.mctx().vtables().StoreVptrs(m_Ctx.builder(), t, temp);
		return m_Ctx.builder().CreateLoad(value->getType(), temp);
	}

	llvm::Value* ExprEmitter::StoreKeepingVptrs(llvm::Value* addr, llvm::Value* value, const Type* t, MemoryAccess access)
	{
		// Assignment into an object that may be a sub-object: its vptrs belong to the complete object.
		auto& B = m_Ctx.builder();
		auto* i8 = llvm::Type::getInt8Ty(m_Ctx.mctx().context());
		auto* i64 = llvm::Type::getInt64Ty(m_Ctx.mctx().context());
		auto* ptr = llvm::PointerType::get(m_Ctx.mctx().context(), 0);
		std::vector<std::pair<llvm::Value*, llvm::Value*>> saved;
		for (const VtableEmitter::VptrSite& site : m_Ctx.mctx().vtables().VptrSites(t))
		{
			llvm::Value* at = site.offset ? B.CreateInBoundsGEP(i8, addr, llvm::ConstantInt::get(i64, site.offset)) : addr;
			saved.push_back({ at, B.CreateLoad(ptr, at) });
		}
		GcStore(addr, value, t, access);
		for (const auto& [at, vptr] : saved) B.CreateStore(vptr, at);
		return value;
	}

	llvm::Value* ExprEmitter::EmitStore(const StoreNode* n)
	{
		if (n->target->kind == LValue::Kind::Lane)
		{
			// SIMD lane: load the whole vector, insert the element, store the vector back. A lane has no address.
			auto& B = m_Ctx.builder();
			const LValue* vector = n->target->base;
			const MemoryAccess access = AccessOf(vector, vector->type);
			llvm::Value* vecAddr = LValueAddress(vector);
			llvm::Value* vec = Load(m_Ctx.mctx().Lower(vector->type), vecAddr, access); // the full <N x T>
			llvm::Value* val = Emit(n->value);
			auto idx = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Ctx.mctx().context()), n->target->laneIndex);
			llvm::Value* newVec = B.CreateInsertElement(vec, val, idx);
			return GcStore(vecAddr, newVec, vector->type, access);
		}
		if (n->target->kind == LValue::Kind::Swizzle)
		{
			// Multi-lane run: load the full <N x T>, scatter the incoming <K x T> lanes into their
			// positions, store the vector back. Other lanes are untouched.
			auto& B = m_Ctx.builder();
			llvm::Type* i32 = llvm::Type::getInt32Ty(m_Ctx.mctx().context());
			const LValue* vector = n->target->base;
			const MemoryAccess access = AccessOf(vector, vector->type);
			llvm::Value* vecAddr = LValueAddress(vector);
			llvm::Value* vec = Load(m_Ctx.mctx().Lower(vector->type), vecAddr, access); // the full <N x T>
			llvm::Value* val = Emit(n->value);                              // the sub-vector <K x T>
			for (size_t i = 0; i < n->target->laneIndices.size(); ++i)
			{
				auto srcIdx = llvm::ConstantInt::get(i32, i);
				llvm::Value* elem = B.CreateExtractElement(val, srcIdx);
				vec = B.CreateInsertElement(vec, elem, llvm::ConstantInt::get(i32, n->target->laneIndices[i]));
			}
			return GcStore(vecAddr, vec, vector->type, access);
		}
		if (n->target->kind == LValue::Kind::VectorLaneDyn)
		{
			// a.data[i] = v, v.zyx[i] = s: load the full <N x T>, insert at the RUNTIME index - through a run, the
			// lane it names there - and store the vector back.
			auto& B = m_Ctx.builder();
			const LValue* run = n->target->base->kind == LValue::Kind::Swizzle ? n->target->base : nullptr;
			const LValue* vector = run ? run->base : n->target->base;
			const MemoryAccess access = AccessOf(vector, vector->type);
			llvm::Value* vecAddr = LValueAddress(vector);
			llvm::Value* vec = Load(m_Ctx.mctx().Lower(vector->type), vecAddr, access);
			llvm::Value* val = Emit(n->value);
			llvm::Value* lane = Emit(n->target->index);
			if (run) lane = B.CreateExtractElement(RunLaneTable(run), lane);
			vec = B.CreateInsertElement(vec, val, lane);
			return GcStore(vecAddr, vec, vector->type, access);
		}
		llvm::Value* addr = LValueAddress(n->target);
		llvm::Value* val = Emit(n->value);
		const Type* t = n->target->type;
		const MemoryAccess access = AccessOf(n->target, t);
		if (!m_Ctx.mctx().vtables().HasVptrs(t)) return GcStore(addr, val, t, access);
		// A sub-object keeps the vptrs of the object it is part of; complete storage always holds its own
		// type's (a zeroed value, as left by a relocation, has none).
		if (MayBeSubobject(n->target)) return StoreKeepingVptrs(addr, val, t, access);
		GcStore(addr, val, t, access);
		m_Ctx.mctx().vtables().StoreVptrs(m_Ctx.builder(), t, addr);
		return val;
	}

	llvm::Value* ExprEmitter::VaIntrinsic(const char* name, std::vector<llvm::Value*> args)
	{
		ModuleContext& mc = m_Ctx.mctx();
		auto* ptr = llvm::PointerType::get(mc.context(), 0);
		llvm::Function* fn = mc.module().getFunction(name);
		if (!fn)
			fn = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(mc.context()), std::vector<llvm::Type*>(args.size(), ptr), false),
				llvm::Function::ExternalLinkage, name, &mc.module());
		return m_Ctx.builder().CreateCall(fn, args);
	}

	llvm::Value* ExprEmitter::EmitVa(const VaNode* n)
	{
		llvm::Value* list = Emit(n->list);
		if (m_Ctx.mctx().System() == TargetSystem::Linux) return EmitSystemVVa(n, list);
		switch (n->op)
		{
		case VaNode::Op::Start: return VaIntrinsic("llvm.va_start.p0", { list });
		case VaNode::Op::End: return VaIntrinsic("llvm.va_end.p0", { list });
		case VaNode::Op::Copy: return VaIntrinsic("llvm.va_copy.p0", { list, Emit(n->source) });
		default: break;
		}
		// x86_64-pc-windows-msvc: every variadic argument takes one 8-byte slot (llvm.va_arg does not advance
		// the list there). An aggregate that is not 1, 2, 4 or 8 bytes arrives as a pointer to a copy.
		auto& B = m_Ctx.builder();
		ModuleContext& mc = m_Ctx.mctx();
		auto* ptr = llvm::PointerType::get(mc.context(), 0);
		llvm::Value* slot = B.CreateLoad(ptr, list);
		B.CreateStore(B.CreateInBoundsGEP(llvm::Type::getInt8Ty(mc.context()), slot, llvm::ConstantInt::get(llvm::Type::getInt64Ty(mc.context()), 8)), list);
		const bool byPointer = n->type->GetKind() == TypeKind::Object && mc.abi().PassesByHiddenPtr(n->type);
		llvm::Value* at = byPointer ? B.CreateLoad(ptr, slot) : slot;
		return B.CreateLoad(mc.Lower(n->type), at);
	}

	// System V: a va_list is { i32 gp_offset, i32 fp_offset, ptr overflow_arg_area, ptr reg_save_area }, which C passes as
	// a pointer to it. C4's va_list is that pointer: va_start and va_copy point it at a list in the function's frame, so a
	// C function (vprintf) takes it as it is. va_arg reads as clang reads (SystemVVaArg): a scalar or an aggregate's
	// eightbytes from the registers they came in while they are left, else from the stack, as a larger aggregate is.
	llvm::Value* ExprEmitter::EmitSystemVVa(const VaNode* n, llvm::Value* list)
	{
		auto& B = m_Ctx.builder();
		ModuleContext& mc = m_Ctx.mctx();
		llvm::LLVMContext& ctx = mc.context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		auto* i32 = llvm::Type::getInt32Ty(ctx);
		llvm::StructType* tag = llvm::StructType::get(ctx, { i32, i32, ptr, ptr });
		llvm::Type* tagArray = llvm::ArrayType::get(tag, 1);
		switch (n->op)
		{
		case VaNode::Op::Start:
		{
			llvm::Value* opened = m_Ctx.EntryAlloca(tagArray, "va_list");
			B.CreateStore(opened, list);
			return VaIntrinsic("llvm.va_start.p0", { opened });
		}
		case VaNode::Op::End: return VaIntrinsic("llvm.va_end.p0", { B.CreateLoad(ptr, list) });
		case VaNode::Op::Copy:
		{
			llvm::Value* copy = m_Ctx.EntryAlloca(tagArray, "va_list");
			B.CreateStore(copy, list);
			return VaIntrinsic("llvm.va_copy.p0", { copy, B.CreateLoad(ptr, Emit(n->source)) });
		}
		default: break;
		}
		llvm::Value* opened = B.CreateLoad(ptr, list);
		llvm::Type* object = mc.Lower(n->type);
		llvm::Type* registers = mc.abi().RegisterType(n->type);
		if (registers)
		{
			llvm::Value* read = B.CreateLoad(registers, SystemVVaArg(opened, registers, tag));
			return B.CreateLoad(object, m_Ctx.Materialize(read, mc.abi().CoercionSlot(object, registers)));
		}
		return B.CreateLoad(object, SystemVVaArg(opened, mc.abi().IsByValueAggregate(n->type) ? nullptr : object, tag, object));
	}

	// The address of the next argument in the System V list `opened`, the list moved past it. `registers` is the type it
	// came in registers as (a scalar, or an aggregate's one or two eightbytes); an aggregate in memory has none and
	// `memory` is its type. While the registers its eightbytes need are left in the save area (6 general ones of 8
	// bytes, 8 vector ones of 16) it is read from there, a two-eightbyte one gathered into a temporary; else from the
	// stack, 8-byte slots, aligned to 16 for a type that needs it. LLVM's own va_arg instruction is not used: x86-64
	// lowers it wrongly at -O0, and clang never emits it there.
	llvm::Value* ExprEmitter::SystemVVaArg(llvm::Value* opened, llvm::Type* registers, llvm::StructType* tag, llvm::Type* memory)
	{
		auto& B = m_Ctx.builder();
		const llvm::DataLayout& layout = m_Ctx.mctx().layout();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		const uint32_t gpEnd = 6 * 8;
		const uint32_t fpEnd = gpEnd + 8 * 16;
		std::vector<llvm::Type*> pieces;
		if (auto* pair = registers ? llvm::dyn_cast<llvm::StructType>(registers) : nullptr) pieces.assign(pair->element_begin(), pair->element_end());
		else if (registers) pieces.push_back(registers);
		auto isVector = [](llvm::Type* piece) { return piece->isFloatingPointTy() || piece->isVectorTy(); };
		uint32_t gpNeeded = 0, fpNeeded = 0;
		for (llvm::Type* piece : pieces) (isVector(piece) ? fpNeeded : gpNeeded) += 1;

		llvm::Function* fn = B.GetInsertBlock()->getParent();
		llvm::BasicBlock* inRegisters = llvm::BasicBlock::Create(ctx, "va.register", fn);
		llvm::BasicBlock* onStack = llvm::BasicBlock::Create(ctx, "va.stack", fn);
		llvm::BasicBlock* join = llvm::BasicBlock::Create(ctx, "va.end", fn);
		llvm::Value* gpAt = B.CreateStructGEP(tag, opened, 0);
		llvm::Value* fpAt = B.CreateStructGEP(tag, opened, 1);
		llvm::Value* gp = B.CreateLoad(B.getInt32Ty(), gpAt, "va.gp");
		llvm::Value* fp = B.CreateLoad(B.getInt32Ty(), fpAt, "va.fp");
		llvm::Value* fits = B.getInt1(!pieces.empty());
		if (gpNeeded) fits = B.CreateAnd(fits, B.CreateICmpULE(gp, B.getInt32(gpEnd - 8 * gpNeeded)));
		if (fpNeeded) fits = B.CreateAnd(fits, B.CreateICmpULE(fp, B.getInt32(fpEnd - 16 * fpNeeded)));
		B.CreateCondBr(fits, inRegisters, onStack);

		B.SetInsertPoint(inRegisters);
		llvm::Value* saveArea = B.CreateLoad(ptr, B.CreateStructGEP(tag, opened, 3));
		auto at = [&](llvm::Value* offset) { return B.CreateInBoundsGEP(B.getInt8Ty(), saveArea, B.CreateZExt(offset, B.getInt64Ty())); };
		llvm::Value* fromRegisters = nullptr;
		if (pieces.size() == 1) fromRegisters = at(isVector(pieces[0]) ? fp : gp);
		else if (!pieces.empty())
		{
			fromRegisters = m_Ctx.EntryAlloca(registers, "va.pair");
			llvm::Value* gpCursor = gp;
			llvm::Value* fpCursor = fp;
			for (unsigned i = 0; i < pieces.size(); ++i)
			{
				llvm::Value*& cursor = isVector(pieces[i]) ? fpCursor : gpCursor;
				B.CreateStore(B.CreateLoad(pieces[i], at(cursor)), B.CreateStructGEP(registers, fromRegisters, i));
				cursor = B.CreateAdd(cursor, B.getInt32(isVector(pieces[i]) ? 16 : 8));
			}
		}
		if (gpNeeded) B.CreateStore(B.CreateAdd(gp, B.getInt32(8 * gpNeeded)), gpAt);
		if (fpNeeded) B.CreateStore(B.CreateAdd(fp, B.getInt32(16 * fpNeeded)), fpAt);
		B.CreateBr(join);

		B.SetInsertPoint(onStack);
		llvm::Type* stacked = memory ? memory : registers;
		const uint64_t bytes = (layout.getTypeAllocSize(stacked) + 7) & ~uint64_t(7);
		llvm::Value* areaAt = B.CreateStructGEP(tag, opened, 2);
		llvm::Value* area = B.CreateLoad(ptr, areaAt);
		if (layout.getABITypeAlign(stacked).value() > 8)
		{
			// A 16-byte aligned argument starts at the next 16-byte slot.
			llvm::Value* address = B.CreatePtrToInt(area, B.getInt64Ty());
			address = B.CreateAnd(B.CreateAdd(address, B.getInt64(15)), B.getInt64(~uint64_t(15)));
			area = B.CreateIntToPtr(address, ptr);
		}
		B.CreateStore(B.CreateConstInBoundsGEP1_64(B.getInt8Ty(), area, bytes), areaAt);
		B.CreateBr(join);

		B.SetInsertPoint(join);
		llvm::PHINode* address = B.CreatePHI(ptr, 2, "va.at");
		address->addIncoming(fromRegisters ? fromRegisters : llvm::ConstantPointerNull::get(ptr), inRegisters);
		address->addIncoming(area, onStack);
		return address;
	}

	llvm::Value* ExprEmitter::GcStore(llvm::Value* addr, llvm::Value* value, const Type* elemType, MemoryAccess access)
	{
		(void)elemType; // the collector needs no barrier today; a generational or concurrent one would add it here
		Attach(m_Ctx.builder().CreateStore(value, addr, access.isVolatile), access.aliasTag);
		return value;
	}

	bool ExprEmitter::BuildsInPlace(const HIRNode* value, const Type* type)
	{
		if (!value || !type || TypeHandle::UnwrapToValue(value->type) != TypeHandle::UnwrapToValue(type)) return false;
		if (value->kind == HIRNode::Kind::Construct) return true;
		return value->kind == HIRNode::Kind::Call && CallEmitter(m_Ctx, *this).ReturnsInMemory(static_cast<const CallNode*>(value));
	}

	void ExprEmitter::EmitInPlace(const HIRNode* value, llvm::Value* storage)
	{
		if (value->kind == HIRNode::Kind::Construct) ObjectEmitter(m_Ctx, *this).ConstructAt(static_cast<const ConstructNode*>(value), storage);
		else CallEmitter(m_Ctx, *this).EmitInto(static_cast<const CallNode*>(value), storage);
	}

	llvm::Value* ExprEmitter::Load(llvm::Type* type, llvm::Value* addr, MemoryAccess access)
	{
		return Attach(m_Ctx.builder().CreateLoad(type, addr, access.isVolatile), access.aliasTag);
	}

	MemoryAccess ExprEmitter::AccessOf(const LValue* loc, const Type* accessed) const
	{
		return { IsVolatile(loc), m_Ctx.mctx().aliasTags().OfAccess(loc, accessed) };
	}

	bool ExprEmitter::IsVolatile(const LValue* loc) const
	{
		switch (loc->kind)
		{
		case LValue::Kind::Slot: return m_Ctx.hir().GetSlot(loc->slot).isVolatile;
		case LValue::Kind::Global: return loc->sym && loc->sym->isVolatile;
		case LValue::Kind::Field: return (loc->field && loc->field->m_IsVolatile) || (loc->base && IsVolatile(loc->base));
		case LValue::Kind::Base:
		case LValue::Kind::Lane:
		case LValue::Kind::Swizzle:
		case LValue::Kind::VectorLaneDyn: return loc->base && IsVolatile(loc->base);
		default: return false;
		}
	}

	llvm::Value* ExprEmitter::EmitAddressOf(const AddressOfNode* n)
	{
		return LValueAddress(n->loc);
	}

	llvm::Value* ExprEmitter::EmitAddressArithmetic(const AddressArithmeticNode* n)
	{
		auto& B = m_Ctx.builder();
		auto& ctx = m_Ctx.mctx().context();
		llvm::Value* base = Emit(n->base);
		llvm::Value* offset = Emit(n->offset); // already in bytes (the lowerer multiplied by element size)
		return B.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx), base, offset);
	}

	llvm::Value* ExprEmitter::EmitFunctionAddress(const FunctionAddressNode* n)
	{
		return m_Ctx.mctx().GetOrCreateFunction(*n->fn); // the Function* IS a pointer-to-code value
	}

	// Evaluate each step in order (side effects run), then return the last step's value. A block step is
	// statements, which the region emitter runs in place.
	llvm::Value* ExprEmitter::EmitSequence(const SequenceNode* n)
	{
		llvm::Value* result = nullptr;
		for (const HIRNode* s : n->steps)
		{
			if (s->kind == HIRNode::Kind::Block)
			{
				m_Ctx.EmitNestedBlock(static_cast<const BlockNode*>(s));
				result = nullptr;
			}
			else result = Emit(s);
		}
		return result;
	}

	// Build a SIMD vector <K x T> from K scalar rvalues: insert each into a fresh (poison) register.
	llvm::Value* ExprEmitter::EmitVectorBuild(const VectorBuildNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::Type* i32 = llvm::Type::getInt32Ty(m_Ctx.mctx().context());
		llvm::Type* vecTy = m_Ctx.mctx().Lower(n->type); // the <K x T>
		llvm::Value* result = llvm::PoisonValue::get(vecTy);
		for (size_t i = 0; i < n->elems.size(); ++i)
		{
			llvm::Value* elem = Emit(n->elems[i]);
			result = B.CreateInsertElement(result, elem, llvm::ConstantInt::get(i32, i));
		}
		return result;
	}

	// A ternary is an rvalue but needs three blocks (cond -> then/else -> join). We create them appended at
	// the end of the function, branch from the current block, emit each arm in its own block, and PHIs the two
	// results in the join. The insert point is LEFT at the join so the enclosing expression continues after
	// both arms have run - exactly how a region leaves the builder at its exit block.
	llvm::Value* ExprEmitter::EmitTernary(const TernaryNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::Function* fn = B.GetInsertBlock()->getParent();
		auto newBB = [&](const char* name) { return llvm::BasicBlock::Create(m_Ctx.mctx().context(), name, fn); };

		// Coerce the condition to i1 via the shared truthiness rule (i1 pass-through, float/ptr/int != 0).
		llvm::Value* cond = IRContext::AsBool(B, Emit(n->cond));

		llvm::BasicBlock* thenBB = newBB("tern.then");
		llvm::BasicBlock* elseBB = newBB("tern.else");
		llvm::BasicBlock* joinBB = newBB("tern.join");
		B.CreateCondBr(cond, thenBB, elseBB);

		llvm::PHINode* phi = llvm::PHINode::Create(m_Ctx.mctx().Lower(n->type), 2, "tern", joinBB);
		// An arm may itself contain a nested ternary (or other block-spawning expression): Emit leaves the
		// builder at whatever block that arm ENDED in, so the incoming predecessor and the branch to join must
		// be recorded from THAT block, not the arm's entry block. Hardcoding thenBB/elseBB breaks dominance.
		{
			InsertGuard g(B, thenBB);
			llvm::Value* tv = Emit(n->thenExpr);
			llvm::BasicBlock* end = B.GetInsertBlock();
			phi->addIncoming(tv, end);
			B.CreateBr(joinBB);
		}
		{
			InsertGuard g(B, elseBB);
			llvm::Value* ev = Emit(n->elseExpr);
			llvm::BasicBlock* end = B.GetInsertBlock();
			phi->addIncoming(ev, end);
			B.CreateBr(joinBB);
		}
		B.SetInsertPoint(joinBB); // continue the enclosing expression after both arms
		return phi;
	}

	llvm::Value* ExprEmitter::EmitDynamicCast(const DynamicCastNode* n)
	{
		// The runtime walks the object's RTTI (C4Runtime/RttiRuntimeC.c): the complete object from the
		// locator at vtable[-1], then the base class array for `target`. A null operand yields null / false.
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		ModuleContext& mc = m_Ctx.mctx();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		llvm::Value* object = Emit(n->operand);
		llvm::Value* target = mc.rtti().TypeDescriptor(n->target);
		if (n->testOnly)
		{
			llvm::Function* test = mc.RuntimeFunction("c4_instance_of", llvm::Type::getInt32Ty(ctx), { ptr, ptr });
			return m_Ctx.builder().CreateICmpNE(m_Ctx.Call(test, { object, target }, false), llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0));
		}
		llvm::Function* cast = mc.RuntimeFunction("c4_dynamic_cast", ptr, { ptr, ptr });
		return m_Ctx.Call(cast, { object, target }, false);
	}
} // namespace Scripting
