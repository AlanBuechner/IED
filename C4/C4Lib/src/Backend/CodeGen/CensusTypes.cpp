#include "Backend/CodeGen/CensusTypes.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Model/Types/NativeMemory.h"
#include "Model/ProgramModel.h"
#include "Model/Types/QualifiedName.h"

namespace Scripting
{
	llvm::Constant* CensusTypes::ObjectRecord(const Type* type, const std::string& name)
	{
		return Record(type, false, name, [&] { return NativeMemory::Holds(type) ? Counter(type, 0) : NoCounter(); });
	}

	llvm::Constant* CensusTypes::ArrayRecord(const Type* element, const std::string& name, uint64 countBytes)
	{
		return Record(element, true, name, [&] { return countBytes && NativeMemory::Holds(element) ? Counter(element, countBytes) : NoCounter(); });
	}

	// One private { name, counter } per type in the module; the runtime merges records of one name (a type's in a
	// program and its scripts, lambdas of overloads).
	llvm::Constant* CensusTypes::Record(const Type* type, bool array, const std::string& name, const std::function<llvm::Constant*()>& counter)
	{
		if (auto known = m_Records.find({ type, array }); known != m_Records.end()) return known->second;
		llvm::Module& module = m_MCtx.module();
		auto* ptr = llvm::PointerType::get(module.getContext(), 0);
		auto* layout = llvm::StructType::get(module.getContext(), { ptr, ptr });
		llvm::Constant* fields[] = { m_MCtx.StringGlobal(ConstantValue::String(name.c_str())), counter() };
		auto* record = new llvm::GlobalVariable(module, layout, true, llvm::GlobalValue::PrivateLinkage,
			llvm::ConstantStruct::get(layout, fields), "c4.census_type");
		m_Records.emplace(std::make_pair(type, array), record);
		return record;
	}

	// `int64(void* object)`, declared now and lowered by the driver.
	llvm::Constant* CensusTypes::Counter(const Type* type, uint64 countBytes)
	{
		FunctionRecord& record = m_Functions.emplace_back();
		record.type = m_MCtx.CensusCounterType();
		record.mangledName = "__c4_native_bytes." + QualifiedName(type) + (countBytes ? "[]" : "");
		llvm::Function* function = m_MCtx.GetOrCreateFunction(record);
		function->setLinkage(llvm::GlobalValue::InternalLinkage);
		m_New.push_back({ type, countBytes, &record });
		return function;
	}

	llvm::Constant* CensusTypes::NoCounter()
	{
		return llvm::ConstantPointerNull::get(llvm::PointerType::get(m_MCtx.module().getContext(), 0));
	}
} // namespace Scripting
