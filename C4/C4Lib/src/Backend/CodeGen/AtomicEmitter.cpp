#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/IRContext.h"
#include "Backend/CodeGen/ModuleContext.h"
#include <llvm/IR/Instructions.h>
#include <utility>
#include <vector>

namespace Scripting
{
	namespace
	{
		using Op = AtomicNode::Op;

		llvm::AtomicOrdering Ordering(AtomicNode::Order order)
		{
			switch (order)
			{
			case AtomicNode::Order::Relaxed: return llvm::AtomicOrdering::Monotonic;
			case AtomicNode::Order::Acquire: return llvm::AtomicOrdering::Acquire;
			case AtomicNode::Order::Release: return llvm::AtomicOrdering::Release;
			case AtomicNode::Order::AcqRel: return llvm::AtomicOrdering::AcquireRelease;
			default: return llvm::AtomicOrdering::SequentiallyConsistent;
			}
		}

		llvm::AtomicRMWInst::BinOp Combine(Op op)
		{
			switch (op)
			{
			case Op::FetchAdd: return llvm::AtomicRMWInst::Add;
			case Op::FetchSub: return llvm::AtomicRMWInst::Sub;
			case Op::FetchAnd: return llvm::AtomicRMWInst::And;
			case Op::FetchOr: return llvm::AtomicRMWInst::Or;
			case Op::FetchXor: return llvm::AtomicRMWInst::Xor;
			default: return llvm::AtomicRMWInst::Xchg;
			}
		}

		bool IsBool(const Type* t) { return t->GetKind() == TypeKind::Bool; }

		using Order = AtomicNode::Order;
		constexpr uint32 Bit(Order order) { return 1u << static_cast<uint32>(order); }

		// The orders each operation can use; any other run-time value acts as SeqCst.
		constexpr uint32 kLoadOrders = Bit(Order::Relaxed) | Bit(Order::Acquire) | Bit(Order::SeqCst);
		constexpr uint32 kStoreOrders = Bit(Order::Relaxed) | Bit(Order::Release) | Bit(Order::SeqCst);
		constexpr uint32 kEveryOrder = kLoadOrders | kStoreOrders | Bit(Order::AcqRel);
		constexpr uint32 kFenceOrders = kEveryOrder & ~Bit(Order::Relaxed);

		uint32 OrdersFor(Op op)
		{
			switch (op)
			{
			case Op::Load: return kLoadOrders;
			case Op::Store: return kStoreOrders;
			case Op::Fence: return kFenceOrders;
			default: return kEveryOrder;
			}
		}

		const char* OrderBlockName(Order order)
		{
			switch (order)
			{
			case Order::Relaxed: return "order.relaxed";
			case Order::Acquire: return "order.acquire";
			case Order::Release: return "order.release";
			case Order::AcqRel: return "order.acqrel";
			default: return "order.seqcst";
			}
		}
	}

	llvm::Value* ExprEmitter::EmitOrdered(const HIRNode* runtime, Order constant, uint32 allowed, const std::function<llvm::Value*(Order)>& emit)
	{
		if (!runtime) return emit(constant);
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& context = m_Ctx.mctx().context();
		llvm::Function* fn = B.GetInsertBlock()->getParent();
		llvm::Value* order = Emit(runtime);
		llvm::BasicBlock* join = llvm::BasicBlock::Create(context, "order.done", fn);
		llvm::BasicBlock* strongest = llvm::BasicBlock::Create(context, OrderBlockName(Order::SeqCst), fn);
		llvm::SwitchInst* chooser = B.CreateSwitch(order, strongest);
		std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> results;
		const auto emitCase = [&](llvm::BasicBlock* block, Order o) {
			B.SetInsertPoint(block);
			llvm::Value* value = emit(o);
			results.emplace_back(value, B.GetInsertBlock());
			B.CreateBr(join);
		};
		for (Order o : { Order::Relaxed, Order::Acquire, Order::Release, Order::AcqRel })
		{
			if (!(allowed & Bit(o))) continue;
			llvm::BasicBlock* block = llvm::BasicBlock::Create(context, OrderBlockName(o), fn, strongest);
			chooser->addCase(llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(order->getType(), static_cast<uint64>(o))), block);
			emitCase(block, o);
		}
		emitCase(strongest, Order::SeqCst);
		B.SetInsertPoint(join);
		if (!results.front().first) return nullptr;
		llvm::PHINode* merged = B.CreatePHI(results.front().first->getType(), static_cast<unsigned>(results.size()));
		for (const auto& [value, block] : results) merged->addIncoming(value, block);
		return merged;
	}

	// LLVM's atomics need a byte-wide type: a bool is accessed as the byte that holds it, and a compare-exchange
	// compares the bits of a float.
	llvm::Type* ExprEmitter::AtomicMemoryType(const Type* value, bool comparesBits)
	{
		llvm::LLVMContext& context = m_Ctx.mctx().context();
		if (IsBool(value)) return llvm::Type::getInt8Ty(context);
		if (comparesBits && value->IsFloatingPoint()) return llvm::IntegerType::get(context, static_cast<unsigned>(value->GetSize() * 8));
		return m_Ctx.mctx().Lower(value);
	}

	llvm::Value* ExprEmitter::ToAtomicMemory(llvm::Value* v, llvm::Type* memory)
	{
		auto& B = m_Ctx.builder();
		if (v->getType() == memory) return v;
		if (v->getType()->isIntegerTy(1)) return B.CreateZExt(v, memory);
		return B.CreateBitCast(v, memory);
	}

	llvm::Value* ExprEmitter::FromAtomicMemory(llvm::Value* v, const Type* value)
	{
		auto& B = m_Ctx.builder();
		llvm::Type* own = m_Ctx.mctx().Lower(value);
		if (v->getType() == own) return v;
		if (IsBool(value)) return B.CreateICmpNE(v, llvm::ConstantInt::get(v->getType(), 0));
		return B.CreateBitCast(v, own);
	}

	llvm::Value* ExprEmitter::EmitAtomic(const AtomicNode* n)
	{
		auto& B = m_Ctx.builder();
		const uint32 allowed = OrdersFor(n->op);
		if (n->op == Op::Fence)
			return EmitOrdered(n->orderValue, n->order, allowed, [&](Order o) -> llvm::Value* { B.CreateFence(Ordering(o)); return nullptr; });
		const llvm::Align align(n->valueType->GetSize());
		llvm::Value* address = Emit(n->address);
		if (n->op == Op::CompareExchange || n->op == Op::CompareExchangeWeak) return EmitCompareExchange(n, address, align);
		llvm::Type* memory = AtomicMemoryType(n->valueType, false);
		if (n->op == Op::Load)
		{
			llvm::Value* loaded = EmitOrdered(n->orderValue, n->order, allowed, [&](Order o) -> llvm::Value* {
				llvm::LoadInst* load = B.CreateAlignedLoad(memory, address, align);
				load->setAtomic(Ordering(o));
				return load;
			});
			return FromAtomicMemory(loaded, n->valueType);
		}
		llvm::Value* operand = ToAtomicMemory(Emit(n->value), memory);
		if (n->op == Op::Store)
			return EmitOrdered(n->orderValue, n->order, allowed, [&](Order o) -> llvm::Value* {
				B.CreateAlignedStore(operand, address, align)->setAtomic(Ordering(o));
				return nullptr;
			});
		llvm::Value* old = EmitOrdered(n->orderValue, n->order, allowed, [&](Order o) -> llvm::Value* {
			return B.CreateAtomicRMW(Combine(n->op), address, operand, align, Ordering(o));
		});
		return FromAtomicMemory(old, n->valueType);
	}

	// True when `*address` held `*expected` and now holds the desired value; otherwise `*expected` gets what it held.
	llvm::Value* ExprEmitter::EmitCompareExchange(const AtomicNode* n, llvm::Value* address, llvm::Align align)
	{
		auto& B = m_Ctx.builder();
		llvm::Type* bits = AtomicMemoryType(n->valueType, true);
		llvm::Value* expectedAddress = Emit(n->expected);
		llvm::Value* expected = B.CreateAlignedLoad(bits, expectedAddress, align);
		llvm::Value* desired = ToAtomicMemory(Emit(n->value), bits);
		llvm::Value* exchange = EmitOrdered(n->orderValue, n->order, kEveryOrder, [&](Order success) -> llvm::Value* {
			return EmitOrdered(n->failureOrderValue, n->failureOrder, kLoadOrders, [&](Order failure) -> llvm::Value* {
				auto* cmpxchg = B.CreateAtomicCmpXchg(address, expected, desired, align, Ordering(success), Ordering(failure));
				cmpxchg->setWeak(n->op == Op::CompareExchangeWeak);
				return cmpxchg;
			});
		});
		llvm::Value* succeeded = B.CreateExtractValue(exchange, 1);
		llvm::Function* fn = B.GetInsertBlock()->getParent();
		llvm::BasicBlock* found = llvm::BasicBlock::Create(m_Ctx.mctx().context(), "cmpxchg.found", fn);
		llvm::BasicBlock* done = llvm::BasicBlock::Create(m_Ctx.mctx().context(), "cmpxchg.done", fn);
		B.CreateCondBr(succeeded, done, found);
		B.SetInsertPoint(found);
		B.CreateAlignedStore(B.CreateExtractValue(exchange, 0), expectedAddress, align);
		B.CreateBr(done);
		B.SetInsertPoint(done);
		return succeeded;
	}
} // namespace Scripting
