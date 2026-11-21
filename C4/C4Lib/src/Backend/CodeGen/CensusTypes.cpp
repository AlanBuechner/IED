#include "Backend/CodeGen/CensusTypes.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/CodeGen/PointerMaps.h"
#include "Backend/Lowering/LoweredProgram.h"
#include "Model/Types/NativeMemory.h"

namespace Scripting
{
	llvm::Constant* CensusTypes::ObjectRecord(const Type* type, const std::string& name)
	{
		return Record(type, Shape{ Form::Object, 0 }, name, [&] { return NativeMemory::Holds(type) ? Counter(type, 0) : NoCounter(); });
	}

	llvm::Constant* CensusTypes::MapRecord(const Type* type, const std::string& name)
	{
		return Record(type, Shape{ Form::MapOnly, 0 }, name, [&] { return NoCounter(); });
	}

	llvm::Constant* CensusTypes::ArrayRecord(const Type* element, const std::string& name, uint64 countBytes)
	{
		return Record(element, Shape{ Form::Array, countBytes }, name, [&] { return countBytes && NativeMemory::Holds(element) ? Counter(element, countBytes) : NoCounter(); });
	}

	// One private c4_gc_type per type in the module: { name, counter, pointer map, words, array, first element's words }.
	// The runtime merges census records of one name (a type's in a program and its scripts, lambdas of overloads).
	llvm::Constant* CensusTypes::Record(const Type* type, Shape shape, const std::string& name, const std::function<llvm::Constant*()>& counter)
	{
		if (auto known = m_Records.find({ type, shape.form }); known != m_Records.end()) return known->second;
		llvm::Module& module = m_MCtx.module();
		llvm::LLVMContext& context = module.getContext();
		auto* ptr = llvm::PointerType::get(context, 0);
		auto* i32 = llvm::Type::getInt32Ty(context);
		auto* i16 = llvm::Type::getInt16Ty(context);
		auto* layout = llvm::StructType::get(context, { ptr, ptr, ptr, i32, i16, i16 });
		const std::optional<PointerMap>& map = MapOf(type);
		llvm::Constant* fields[] = { m_MCtx.StringGlobal(ConstantValue::String(name.c_str())), counter(),
			map ? map->Emit(module) : NoCounter(), llvm::ConstantInt::get(i32, map ? map->words : 0),
			llvm::ConstantInt::get(i16, shape.form == Form::Array ? 1 : 0), llvm::ConstantInt::get(i16, shape.countBytes / 8) };
		auto* record = new llvm::GlobalVariable(module, layout, true, llvm::GlobalValue::PrivateLinkage,
			llvm::ConstantStruct::get(layout, fields), "c4.census_type");
		m_Records.emplace(std::make_pair(type, shape.form), record);
		return record;
	}

	const std::optional<PointerMap>& CensusTypes::MapOf(const Type* type)
	{
		auto known = m_Maps.find(type);
		if (known == m_Maps.end()) known = m_Maps.emplace(type, PointerMap::Of(type)).first;
		return known->second;
	}

	// `int64(void* object)`, lowered with the program and defined in this module, as a local of its own.
	llvm::Constant* CensusTypes::Counter(const Type* type, uint64 countBytes)
	{
		const CensusFunction* planned = m_Program ? m_Program->CensusOf({ type, countBytes }) : nullptr;
		if (!planned)
		{
			m_Unplanned.push_back(type);
			return NoCounter();
		}
		llvm::Function* function = m_MCtx.GetOrCreateFunction(*planned->record);
		function->setLinkage(llvm::GlobalValue::InternalLinkage);
		m_New.push_back(planned);
		return function;
	}

	llvm::Constant* CensusTypes::NoCounter()
	{
		return llvm::ConstantPointerNull::get(llvm::PointerType::get(m_MCtx.module().getContext(), 0));
	}
} // namespace Scripting
