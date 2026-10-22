#pragma once
#include "Core/Core.h"
#include "Backend/CodeGen/StableNames.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	class ModuleContext;

	/// The reflection wire format (C4Runtime/ReflectABI.h) as LLVM constants: one struct type per table row, in
	/// the field order and C layout of its C struct, plus the strings and arrays the rows point at. The rows
	/// are private constants of the module; the runtime copies what it keeps, so they may live in an image that
	/// is unloaded later.
	class ReflectionTables
	{
	public:
		explicit ReflectionTables(ModuleContext& module);

		// --- row types, field for field as ReflectABI.h declares them -----------------------------------
		llvm::StructType* annotationArg;  // c4_annotation_arg
		llvm::StructType* annotation;     // c4_annotation_info
		llvm::StructType* param;          // c4_param_info
		llvm::StructType* field;          // c4_field_info
		llvm::StructType* method;         // c4_method_info
		llvm::StructType* enumValue;      // c4_enum_value
		llvm::StructType* typeInfo;       // c4_type_info
		llvm::StructType* variable;       // c4_variable_info
		llvm::StructType* function;       // c4_function_info
		llvm::StructType* image;          // c4_reflect_image

		/// A pointer to `text` NUL-terminated, one global per distinct text.
		llvm::Constant* String(const std::string& text);
		/// A pointer to a private array of `rows` of `rowType`, or null for none.
		llvm::Constant* Array(llvm::Type* rowType, const std::vector<llvm::Constant*>& rows);
		llvm::Constant* Row(llvm::StructType* type, const std::vector<llvm::Constant*>& fields);
		llvm::Constant* U32(uint64 value);
		llvm::Constant* U64(uint64 value);
		llvm::Constant* I64(int64 value);
		llvm::Constant* F64(double value);
		llvm::Constant* Null();

		/// A private constant named `name` holding `value`.
		llvm::GlobalVariable* Global(const std::string& name, llvm::Constant* value);

	private:
		ModuleContext& m_Module;
		llvm::LLVMContext& m_Context;
		std::unordered_map<std::string, llvm::Constant*> m_Strings;
		StableNames::ConstantNamer m_Namer; // names each table after its rows
	};
} // namespace Scripting
