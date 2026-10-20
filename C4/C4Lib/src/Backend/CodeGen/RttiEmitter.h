#pragma once
#include "Model/Types.h"
#include "Model/Types/ClassHierarchy.h"
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <map>
#include <string>
#include <unordered_map>

namespace Scripting
{
	/// MSVC x64 RTTI (D-10, docs/compiler/04): a TypeDescriptor per decorated name, and for each polymorphic
	/// type its ClassHierarchyDescriptor (the base class array) and the CompleteObjectLocator that sits at
	/// vtable[-1] of each of its vtables. The frame handler, C++ `dynamic_cast` and C4Runtime/RttiRuntimeC.c
	/// all read these tables.
	class RttiEmitter
	{
	public:
		explicit RttiEmitter(llvm::Module& module) : m_Module(module) {}

		llvm::GlobalVariable* TypeDescriptor(const Type* t);
		llvm::GlobalVariable* TypeDescriptor(const std::string& decoratedName);

		/// The locator of the vtable at `vptrOffset` inside a complete `type` object.
		llvm::GlobalVariable* CompleteObjectLocator(const ObjectType* type, int64 vptrOffset);

	private:
		llvm::GlobalVariable* ClassHierarchyDescriptor(const ObjectType* type);
		llvm::Constant* BaseClassArray(const std::string& typeName, const std::vector<ClassHierarchy::Subobject>& subobjects);

		llvm::Module& m_Module;
		std::unordered_map<std::string, llvm::GlobalVariable*> m_Descriptors;
		std::unordered_map<const ObjectType*, llvm::GlobalVariable*> m_Hierarchies;
		std::map<std::pair<const ObjectType*, int64>, llvm::GlobalVariable*> m_Locators;
	};
} // namespace Scripting
