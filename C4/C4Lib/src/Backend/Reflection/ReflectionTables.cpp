#include "Backend/Reflection/ReflectionTables.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Model/ConstantValue.h"
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

namespace Scripting
{
	ReflectionTables::ReflectionTables(ModuleContext& module) : m_Module(module), m_Context(module.module().getContext())
	{
		llvm::Type* ptr = llvm::PointerType::get(m_Context, 0);
		llvm::Type* u32 = llvm::Type::getInt32Ty(m_Context);
		llvm::Type* u64 = llvm::Type::getInt64Ty(m_Context);
		llvm::Type* f64 = llvm::Type::getDoubleTy(m_Context);
		annotationArg = llvm::StructType::get(m_Context, { u32, u64, f64, ptr });
		annotation = llvm::StructType::get(m_Context, { ptr, u32, ptr });
		param = llvm::StructType::get(m_Context, { ptr, ptr, u32, ptr });
		field = llvm::StructType::get(m_Context, { ptr, ptr, u64, u64, u32, u32, u32, ptr });
		method = llvm::StructType::get(m_Context, { ptr, u32, ptr, ptr, u32, u32, u32, ptr, u32, ptr });
		enumValue = llvm::StructType::get(m_Context, { ptr, u64 });
		typeInfo = llvm::StructType::get(m_Context, { u32, ptr, u64, u64, u32, ptr, u32, ptr, ptr, u32, ptr, u32, ptr, u32, ptr });
		variable = llvm::StructType::get(m_Context, { ptr, ptr, ptr, u64, u64, u32, ptr, u32, ptr, u32, ptr });
		function = llvm::StructType::get(m_Context, { ptr, ptr, ptr, u32, ptr, u32, u32, ptr, u32, ptr, ptr, u32, ptr });
		image = llvm::StructType::get(m_Context, { u32, ptr, u32, ptr, ptr, u32, ptr });
	}

	llvm::Constant* ReflectionTables::String(const std::string& text)
	{
		auto [entry, fresh] = m_Strings.try_emplace(text, nullptr);
		if (fresh) entry->second = m_Module.StringGlobal(ConstantValue::String(text));
		return entry->second;
	}

	llvm::Constant* ReflectionTables::Array(llvm::Type* rowType, const std::vector<llvm::Constant*>& rows)
	{
		if (rows.empty()) return Null();
		auto* type = llvm::ArrayType::get(rowType, rows.size());
		return Global("c4.reflect.rows", llvm::ConstantArray::get(type, rows));
	}

	llvm::Constant* ReflectionTables::Row(llvm::StructType* type, const std::vector<llvm::Constant*>& fields)
	{
		return llvm::ConstantStruct::get(type, fields);
	}

	llvm::Constant* ReflectionTables::U32(uint64 value) { return llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), value); }
	llvm::Constant* ReflectionTables::U64(uint64 value) { return llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), value); }
	llvm::Constant* ReflectionTables::I64(int64 value) { return llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(m_Context), value); }
	llvm::Constant* ReflectionTables::F64(double value) { return llvm::ConstantFP::get(llvm::Type::getDoubleTy(m_Context), value); }
	llvm::Constant* ReflectionTables::Null() { return llvm::ConstantPointerNull::get(llvm::PointerType::get(m_Context, 0)); }

	llvm::GlobalVariable* ReflectionTables::Global(const std::string& name, llvm::Constant* value)
	{
		auto* global = new llvm::GlobalVariable(m_Module.module(), value->getType(), true, llvm::GlobalValue::PrivateLinkage, value, name);
		global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
		return global;
	}
} // namespace Scripting
