#include "Backend/Reflection/ReflectionTables.h"
#include "Backend/CodeGen/StableNames.h"
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

namespace Scripting
{
	ReflectionTables::ReflectionTables(llvm::Module& module, Strings strings)
		: m_Module(module), m_Context(module.getContext()), m_Strings(std::move(strings))
	{
		llvm::Type* ptr = llvm::PointerType::get(m_Context, 0);
		llvm::Type* u32 = llvm::Type::getInt32Ty(m_Context);
		llvm::Type* u64 = llvm::Type::getInt64Ty(m_Context);
		llvm::Type* f64 = llvm::Type::getDoubleTy(m_Context);
		m_AnnotationArg = llvm::StructType::get(m_Context, { u32, u64, f64, ptr });
		m_Annotation = llvm::StructType::get(m_Context, { ptr, u32, ptr });
		m_Param = llvm::StructType::get(m_Context, { ptr, ptr, u32, ptr });
		m_Field = llvm::StructType::get(m_Context, { ptr, ptr, u64, u64, u32, u32, u32, ptr });
		m_Method = llvm::StructType::get(m_Context, { ptr, u32, ptr, ptr, u32, u32, u32, ptr, u32, ptr, ptr });
		m_EnumValue = llvm::StructType::get(m_Context, { ptr, u64 });
		m_TypeInfo = llvm::StructType::get(m_Context, { u32, ptr, u64, u64, u32, ptr, u32, ptr, ptr, u32, ptr, u32, ptr, u32, ptr });
		m_Variable = llvm::StructType::get(m_Context, { ptr, ptr, ptr, u64, u64, u32, ptr, u32, ptr, u32, ptr });
		m_Function = llvm::StructType::get(m_Context, { ptr, ptr, ptr, u32, ptr, u32, u32, ptr, u32, ptr, ptr, u32, ptr, ptr, ptr });
		m_Image = llvm::StructType::get(m_Context, { u32, ptr, u32, ptr, ptr, u32, ptr });
	}

	llvm::GlobalVariable* ReflectionTables::Build(const ImageDescription& image)
	{
		if (image.Empty()) return nullptr;
		llvm::Type* ptr = llvm::PointerType::get(m_Context, 0);
		for (const std::string& text : image.Texts()) m_Texts.push_back(String(text));
		for (const ImageSymbol& symbol : image.Symbols()) m_Symbols.push_back(Symbol(symbol));
		for (const ImageTable& table : image.Tables())
		{
			llvm::StructType* type = RowType(table.shape);
			std::vector<llvm::Constant*> rows;
			rows.reserve(table.rows.size());
			for (const ImageRow& row : table.rows) rows.push_back(type ? Row(type, row) : Value(row[0], ptr));
			m_Tables.push_back(Array(type ? static_cast<llvm::Type*>(type) : ptr, rows));
		}
		return new llvm::GlobalVariable(m_Module, m_Image, true, llvm::GlobalValue::ExternalLinkage, Row(m_Image, image.Image()),
			"__c4_reflect_image");
	}

	llvm::StructType* ReflectionTables::RowType(ImageShape shape) const
	{
		switch (shape)
		{
		case ImageShape::AnnotationArg: return m_AnnotationArg;
		case ImageShape::Annotation: return m_Annotation;
		case ImageShape::Param: return m_Param;
		case ImageShape::Field: return m_Field;
		case ImageShape::Method: return m_Method;
		case ImageShape::EnumValue: return m_EnumValue;
		case ImageShape::TypeInfo: return m_TypeInfo;
		case ImageShape::Variable: return m_Variable;
		case ImageShape::Function: return m_Function;
		case ImageShape::Image: return m_Image;
		case ImageShape::Pointer: return nullptr;
		}
		return nullptr;
	}

	llvm::Constant* ReflectionTables::Value(const ImageValue& value, llvm::Type* field) const
	{
		switch (value.kind)
		{
		case ImageValue::Kind::Integer: return llvm::ConstantInt::get(field, value.integer);
		case ImageValue::Kind::Real: return llvm::ConstantFP::get(field, value.real);
		case ImageValue::Kind::Text: return m_Texts[value.integer];
		case ImageValue::Kind::Table: return m_Tables[value.integer];
		case ImageValue::Kind::Symbol: return m_Symbols[value.integer];
		case ImageValue::Kind::Null: break;
		}
		return llvm::ConstantPointerNull::get(llvm::PointerType::get(m_Context, 0));
	}

	llvm::Constant* ReflectionTables::Row(llvm::StructType* type, const ImageRow& row) const
	{
		std::vector<llvm::Constant*> fields;
		fields.reserve(row.size());
		for (size_t i = 0; i < row.size(); ++i) fields.push_back(Value(row[i], type->getElementType(static_cast<unsigned>(i))));
		return llvm::ConstantStruct::get(type, fields);
	}

	llvm::Constant* ReflectionTables::String(const std::string& text)
	{
		if (m_Strings) return m_Strings(text);
		const std::string name = StableNames::ForBytes(".str.1", text);
		if (llvm::GlobalVariable* same = m_Module.getNamedGlobal(name)) return same;
		llvm::Constant* data = llvm::ConstantDataArray::getString(m_Context, text, true);
		auto* global = new llvm::GlobalVariable(m_Module, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data, name);
		global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
		return global;
	}

	// The module's own when it has the name (an image built where its program is), else a declaration: only the
	// address is taken, so a function is `void()` and a global a byte, as a script host's table declares them.
	llvm::Constant* ReflectionTables::Symbol(const ImageSymbol& symbol)
	{
		if (llvm::GlobalValue* own = m_Module.getNamedValue(symbol.name)) return own;
		llvm::GlobalValue* declared = nullptr;
		if (symbol.function)
			declared = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context), false), llvm::GlobalValue::ExternalLinkage,
				symbol.name, m_Module);
		else
			declared = new llvm::GlobalVariable(m_Module, llvm::Type::getInt8Ty(m_Context), false, llvm::GlobalValue::ExternalLinkage, nullptr,
				symbol.name);
		if (symbol.hidden) declared->setVisibility(llvm::GlobalValue::HiddenVisibility);
		return declared;
	}

	llvm::Constant* ReflectionTables::Array(llvm::Type* rowType, const std::vector<llvm::Constant*>& rows)
	{
		auto* table = llvm::ConstantArray::get(llvm::ArrayType::get(rowType, rows.size()), rows);
		// Named after its rows; LLVM keeps one copy of each distinct constant, so the same pointer is the same table.
		const std::string name = m_Namer.Name("c4.reflect.rows", *table);
		if (llvm::GlobalVariable* same = m_Module.getGlobalVariable(name, true); same && same->getInitializer() == table) return same;
		auto* global = new llvm::GlobalVariable(m_Module, table->getType(), true, llvm::GlobalValue::PrivateLinkage, table, name);
		global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
		return global;
	}
} // namespace Scripting
