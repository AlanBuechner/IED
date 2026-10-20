#include "Backend/CodeGen/EhTables.h"
#include "Backend/CodeGen/MsvcNames.h"
#include "Backend/CodeGen/PeTables.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"

namespace Scripting
{
	namespace
	{
		constexpr uint32_t kCatchableSimpleType = 1;  // scalar or pointer: copied as bits, a pointer is adjusted
		constexpr uint32_t kThrowConst = 1;           // the thrown pointer's pointee is const
		constexpr uint32_t kHandlerConst = 1;
		constexpr uint32_t kHandlerReference = 8;
		constexpr uint32_t kHandlerCatchAll = 64;      // `catch (...)`
		constexpr int64 kNoVirtualBase = -1;
		constexpr uint32_t kPointerSize = 8;

		const ObjectType* AsObject(const Type* t)
		{
			t = TypeHandle::UnwrapToValue(t);
			return t && t->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(t) : nullptr;
		}

		const PointerType* AsPointer(const Type* t)
		{
			t = TypeHandle::UnwrapToValue(t);
			return t && t->GetKind() == TypeKind::Pointer ? static_cast<const PointerType*>(t) : nullptr;
		}

		// Each base a handler may name: a base that occurs twice is ambiguous and matches no handler.
		std::vector<ClassHierarchy::Subobject> UnambiguousSubobjects(const ObjectType* type)
		{
			std::vector<ClassHierarchy::Subobject> out;
			for (const auto& s : ClassHierarchy::Subobjects(type))
				if (!s.ambiguous) out.push_back(s);
			return out;
		}
	}

	std::vector<EhTables::Catchable> EhTables::CatchableTypes(const Type* thrown, const ObjectLifecycle& lifecycle) const
	{
		std::vector<Catchable> list;
		if (const ObjectType* obj = AsObject(thrown))
		{
			auto copyOf = [&](const ObjectType* t) -> llvm::Function* {
				for (const auto& [type, rec] : lifecycle.copies)
					if (type == t && rec) return m_FunctionFor(*rec);
				return nullptr;
			};
			for (const auto& s : UnambiguousSubobjects(obj))
				list.push_back({ MsvcNames::TypeDescriptorName(s.type), s.offset, static_cast<uint32_t>(s.type->GetSize()), false, copyOf(s.type) });
			return list;
		}
		if (const PointerType* ptr = AsPointer(thrown))
		{
			if (const ObjectType* pointee = AsObject(ptr->GetPointeeType()))
				for (const auto& s : UnambiguousSubobjects(pointee))
					list.push_back({ MsvcNames::PointerTo(s.type), s.offset, kPointerSize, true });
			else
				list.push_back({ MsvcNames::TypeDescriptorName(ptr), 0, kPointerSize, true });
			list.push_back({ ".PEAX", 0, kPointerSize, true });
			return list;
		}
		const Type* value = TypeHandle::UnwrapToValue(thrown);
		bool simple = value && (value->IsNumeric() || value->GetKind() == TypeKind::Bool || value->GetKind() == TypeKind::Enum ||
			value->GetKind() == TypeKind::FunPtr);
		list.push_back({ MsvcNames::TypeDescriptorName(value), 0, static_cast<uint32_t>(value ? value->GetSize() : 0), simple });
		return list;
	}

	// Shared by every throw that can be caught as it, and named after what it describes (as MSVC names its _CT
	// records), so no name depends on the order the program's throws were lowered in.
	llvm::Constant* EhTables::CatchableType(const Catchable& c)
	{
		const std::string name = "c4.eh.ct." + c.name + "." + std::to_string(c.offset) + "." + std::to_string(c.size) +
			(c.simple ? ".s" : "") + (c.copy ? "." + c.copy->getName().str() : "");
		if (auto* existing = m_Module.getGlobalVariable(name, true)) return PeTables::ImageRelative(m_Module, existing);
		// { properties, TypeDescriptor, PMD { mdisp, pdisp, vdisp }, size, copy function }. The copy function
		// is the type's copy (member convention: this, source); 0 when the handler may copy the bytes.
		llvm::LLVMContext& ctx = m_Module.getContext();
		auto* init = llvm::ConstantStruct::getAnon(ctx, {
			PeTables::I32(ctx, c.simple ? kCatchableSimpleType : 0), PeTables::ImageRelative(m_Module, m_Rtti.TypeDescriptor(c.name)),
			PeTables::I32(ctx, c.offset), PeTables::I32(ctx, kNoVirtualBase), PeTables::I32(ctx, 0),
			PeTables::I32(ctx, c.size), FunctionOffset(c.copy) });
		return PeTables::ImageRelative(m_Module, PeTables::Private(m_Module, name, init));
	}

	llvm::Constant* EhTables::FunctionOffset(llvm::Function* fn)
	{
		return fn ? PeTables::ImageRelative(m_Module, fn) : PeTables::I32(m_Module.getContext(), 0);
	}

	llvm::GlobalVariable* EhTables::ThrowInfo(const Type* thrown, const ObjectLifecycle& lifecycle)
	{
		auto it = m_ThrowInfos.find(thrown);
		if (it != m_ThrowInfos.end()) return it->second;
		llvm::LLVMContext& ctx = m_Module.getContext();
		std::vector<llvm::Constant*> types;
		const std::vector<Catchable> catchables = CatchableTypes(thrown, lifecycle);
		for (const Catchable& c : catchables) types.push_back(CatchableType(c));
		const std::string thrownName = catchables.empty() ? std::string("void") : catchables.front().name;
		// CatchableTypeArray { count, CatchableType[count] }.
		auto* arrayTy = llvm::ArrayType::get(llvm::Type::getInt32Ty(ctx), types.size());
		auto* cta = PeTables::Private(m_Module, "c4.eh.cta." + thrownName,
			llvm::ConstantStruct::getAnon(ctx, { PeTables::I32(ctx, static_cast<int64>(types.size())), llvm::ConstantArray::get(arrayTy, types) }));
		// ThrowInfo { attributes, destructor, forward compat, CatchableTypeArray }: the destructor runs on the
		// exception object when the last handler is done with it.
		const PointerType* ptr = AsPointer(thrown);
		uint32_t attributes = ptr && ptr->IsConstReferent() ? kThrowConst : 0;
		llvm::Function* destroy = lifecycle.destroy ? m_FunctionFor(*lifecycle.destroy) : nullptr;
		auto* ti = PeTables::Private(m_Module, "c4.eh.ti." + thrownName, llvm::ConstantStruct::getAnon(ctx, {
			PeTables::I32(ctx, attributes), FunctionOffset(destroy), PeTables::I32(ctx, 0), PeTables::ImageRelative(m_Module, cta) }));
		m_ThrowInfos[thrown] = ti;
		return ti;
	}

	EhTables::Handler EhTables::HandlerFor(const TryNode::CatchClause& clause)
	{
		Handler h;
		if (clause.catchAll)
		{
			h.typeDescriptor = llvm::ConstantPointerNull::get(llvm::PointerType::get(m_Module.getContext(), 0));
			h.adjectives = kHandlerCatchAll;
			return h;
		}
		h.typeDescriptor = m_Rtti.TypeDescriptor(clause.type);
		if (clause.byReference) h.adjectives |= kHandlerReference;
		if (clause.constQualified) h.adjectives |= kHandlerConst;
		return h;
	}
} // namespace Scripting
