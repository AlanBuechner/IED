#include "Backend/CodeGen/RttiEmitter.h"
#include "Backend/CodeGen/MsvcNames.h"
#include "Backend/CodeGen/PeTables.h"

namespace Scripting
{
	namespace
	{
		constexpr int64 kLocatorSignatureX64 = 1;   // image-relative fields
		constexpr int64 kHierarchyMultiple = 1;     // some class in the hierarchy has more than one base
		constexpr int64 kHierarchyAmbiguous = 4;    // some base occurs as more than one sub-object
		constexpr int64 kBaseAmbiguous = 2;
		constexpr int64 kBaseHasHierarchy = 0x40;   // pClassDescriptor is valid
		constexpr int64 kNoVirtualBase = -1;        // PMD.pdisp

		int64 HierarchyAttributes(const std::vector<ClassHierarchy::Subobject>& subobjects)
		{
			int64 attributes = 0;
			for (const auto& s : subobjects)
			{
				if (s.type->GetBaseTypes().Count() > 1) attributes |= kHierarchyMultiple;
				if (s.ambiguous) attributes |= kHierarchyAmbiguous;
			}
			return attributes;
		}
	}

	llvm::GlobalVariable* RttiEmitter::TypeDescriptor(const Type* t)
	{
		return TypeDescriptor(MsvcNames::TypeDescriptorName(t));
	}

	llvm::GlobalVariable* RttiEmitter::TypeDescriptor(const std::string& decoratedName)
	{
		auto it = m_Descriptors.find(decoratedName);
		if (it != m_Descriptors.end()) return it->second;
		// { type_info vftable, spare, name }. The vftable is null: C4 links no C++ type_info, and matching
		// compares the descriptor address or its name, never the vftable. The spare is written by the C++
		// runtime's type_info::name(), so the descriptor is not constant.
		llvm::LLVMContext& ctx = m_Module.getContext();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		auto* name = llvm::ConstantDataArray::getString(ctx, decoratedName, true);
		auto* init = llvm::ConstantStruct::getAnon(ctx, { llvm::ConstantPointerNull::get(ptr), llvm::ConstantPointerNull::get(ptr), name });
		const std::string symbol = "??_R0" + decoratedName.substr(1) + "@8";
		auto* td = m_Module.getGlobalVariable(symbol);
		if (!td)
		{
			td = new llvm::GlobalVariable(m_Module, init->getType(), false, llvm::GlobalValue::LinkOnceODRLinkage, init, symbol);
			td->setComdat(m_Module.getOrInsertComdat(symbol));
		}
		m_Descriptors[decoratedName] = td;
		return td;
	}

	llvm::GlobalVariable* RttiEmitter::CompleteObjectLocator(const ObjectType* type, int64 vptrOffset)
	{
		auto key = std::make_pair(type, vptrOffset);
		auto it = m_Locators.find(key);
		if (it != m_Locators.end()) return it->second;
		llvm::LLVMContext& ctx = m_Module.getContext();
		auto* i32 = llvm::Type::getInt32Ty(ctx);
		// { signature, offset of this vptr, constructor displacement, TypeDescriptor, hierarchy, self }. The
		// self entry lets a reader find the image base from the locator alone.
		auto* layout = llvm::StructType::get(ctx, { i32, i32, i32, i32, i32, i32 });
		auto* col = new llvm::GlobalVariable(m_Module, layout, true, llvm::GlobalValue::PrivateLinkage, nullptr,
			"c4.rtti.col." + std::to_string(++m_Counter));
		col->setInitializer(llvm::ConstantStruct::get(layout, {
			PeTables::I32(ctx, kLocatorSignatureX64), PeTables::I32(ctx, vptrOffset), PeTables::I32(ctx, 0),
			PeTables::ImageRelative(m_Module, TypeDescriptor(type)),
			PeTables::ImageRelative(m_Module, ClassHierarchyDescriptor(type)),
			PeTables::ImageRelative(m_Module, col) }));
		m_Locators[key] = col;
		return col;
	}

	llvm::GlobalVariable* RttiEmitter::ClassHierarchyDescriptor(const ObjectType* type)
	{
		auto it = m_Hierarchies.find(type);
		if (it != m_Hierarchies.end()) return it->second;
		llvm::LLVMContext& ctx = m_Module.getContext();
		auto* i32 = llvm::Type::getInt32Ty(ctx);
		// { signature, attributes, base count, base class array }. Created before its array so a base
		// descriptor of this type can point back at it.
		auto* layout = llvm::StructType::get(ctx, { i32, i32, i32, i32 });
		auto* chd = new llvm::GlobalVariable(m_Module, layout, true, llvm::GlobalValue::PrivateLinkage, nullptr,
			"c4.rtti.chd." + std::to_string(++m_Counter));
		m_Hierarchies[type] = chd;
		std::vector<ClassHierarchy::Subobject> subobjects = ClassHierarchy::Subobjects(type);
		llvm::Constant* array = BaseClassArray(subobjects);
		chd->setInitializer(llvm::ConstantStruct::get(layout, {
			PeTables::I32(ctx, 0), PeTables::I32(ctx, HierarchyAttributes(subobjects)),
			PeTables::I32(ctx, static_cast<int64>(subobjects.size())),
			PeTables::ImageRelative(m_Module, llvm::cast<llvm::GlobalValue>(array)) }));
		return chd;
	}

	llvm::Constant* RttiEmitter::BaseClassArray(const std::vector<ClassHierarchy::Subobject>& subobjects)
	{
		llvm::LLVMContext& ctx = m_Module.getContext();
		std::vector<llvm::Constant*> entries;
		for (const ClassHierarchy::Subobject& s : subobjects)
		{
			// { TypeDescriptor, contained bases, PMD { mdisp, pdisp, vdisp }, attributes, hierarchy }.
			int64 attributes = kBaseHasHierarchy | (s.ambiguous ? kBaseAmbiguous : 0);
			auto* bcd = llvm::ConstantStruct::getAnon(ctx, {
				PeTables::ImageRelative(m_Module, TypeDescriptor(s.type)), PeTables::I32(ctx, s.containedBases),
				PeTables::I32(ctx, s.offset), PeTables::I32(ctx, kNoVirtualBase), PeTables::I32(ctx, 0),
				PeTables::I32(ctx, attributes), PeTables::ImageRelative(m_Module, ClassHierarchyDescriptor(s.type)) });
			auto* g = PeTables::Private(m_Module, "c4.rtti.bcd." + std::to_string(++m_Counter), bcd);
			entries.push_back(PeTables::ImageRelative(m_Module, g));
		}
		entries.push_back(PeTables::I32(ctx, 0));
		auto* arrayTy = llvm::ArrayType::get(llvm::Type::getInt32Ty(ctx), entries.size());
		return PeTables::Private(m_Module, "c4.rtti.bca." + std::to_string(++m_Counter), llvm::ConstantArray::get(arrayTy, entries));
	}
} // namespace Scripting
