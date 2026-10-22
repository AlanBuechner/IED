#pragma once
#include "Core/Core.h"
#include "Backend/CodeGen/StableNames.h"
#include "Backend/Reflection/ImageDescription.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <functional>
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// The reflection wire format (C4Runtime/ReflectABI.h) as LLVM constants: one struct type per table row, in
	/// the field order and C layout of its C struct, plus the strings and arrays the rows point at. The rows
	/// are private constants of the module; the runtime copies what it keeps, so they may live in an image that
	/// is unloaded later. It reads no model, so an image is built on any thread, in a module of its own (D-82).
	class ReflectionTables
	{
	public:
		/// A pointer to `text` NUL-terminated, from the module's own strings.
		using Strings = std::function<llvm::Constant*(const std::string& text)>;

		/// `strings` null: the tables make their own private strings.
		explicit ReflectionTables(llvm::Module& module, Strings strings = nullptr);

		/// `image` in the module: the external constant `__c4_reflect_image` and every table it points at. What it
		/// points at in the program is the module's own when the module has the name, else declared by its name.
		llvm::GlobalVariable* Build(const ImageDescription& image);

	private:
		llvm::StructType* RowType(ImageShape shape) const;
		llvm::Constant* Value(const ImageValue& value, llvm::Type* field) const;
		llvm::Constant* String(const std::string& text);
		llvm::Constant* Symbol(const ImageSymbol& symbol);
		/// A pointer to a private array of `rows` of `rowType`, the same constant for the same rows.
		llvm::Constant* Array(llvm::Type* rowType, const std::vector<llvm::Constant*>& rows);
		llvm::Constant* Row(llvm::StructType* type, const ImageRow& row) const;

		llvm::Module& m_Module;
		llvm::LLVMContext& m_Context;
		Strings m_Strings;
		std::vector<llvm::Constant*> m_Texts;
		std::vector<llvm::Constant*> m_Symbols;
		std::vector<llvm::Constant*> m_Tables;
		StableNames::ConstantNamer m_Namer; // names each table after its rows

		// --- row types, field for field as ReflectABI.h declares them -----------------------------------
		llvm::StructType* m_AnnotationArg;  // c4_annotation_arg
		llvm::StructType* m_Annotation;     // c4_annotation_info
		llvm::StructType* m_Param;          // c4_param_info
		llvm::StructType* m_Field;          // c4_field_info
		llvm::StructType* m_Method;         // c4_method_info
		llvm::StructType* m_EnumValue;      // c4_enum_value
		llvm::StructType* m_TypeInfo;       // c4_type_info
		llvm::StructType* m_Variable;       // c4_variable_info
		llvm::StructType* m_Function;       // c4_function_info
		llvm::StructType* m_Image;          // c4_reflect_image
	};
} // namespace Scripting
