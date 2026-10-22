#include "Backend/CodeGen/DebugInfo.h"
#include "Backend/CodeGen/IRContext.h"
#include "Model/Types.h"
#include "Model/TypeHandle.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/SignatureType.h"
#include "Model/Types/SwizzleType.h"
#include "Model/Types/UnionType.h"
#include "Compiler/Version.h"
#include <llvm/BinaryFormat/Dwarf.h>
#include <filesystem>

namespace Scripting
{
	namespace
	{
		constexpr uint64 kBitsPerByte = 8;
		constexpr uint64 kPointerBits = 64;

		// `name` as the program spells it from outside `scope`: "Ns::Type::name".
		std::string ScopedName(const Scope* scope, const Core::String& name)
		{
			std::string scoped;
			for (const Core::String& part : EnclosingNames(scope)) scoped += std::string(part.c_str()) + "::";
			return scoped + name.c_str();
		}

		// "Ns::Type::Method" or "Ns::Free": what the debugger shows for the function.
		std::string FunctionName(const FunctionRecord& record)
		{
			if (record.containingType) return std::string(QualifiedName(record.containingType).c_str()) + "::" + record.name.c_str();
			return ScopedName(record.scope, record.name);
		}

		bool HasDebugInfo(const llvm::GlobalVariable& global)
		{
			llvm::SmallVector<llvm::DIGlobalVariableExpression*, 1> described;
			global.getDebugInfo(described);
			return !described.empty();
		}

		// Compiler-made slots are named "__..."; every other local is one the program wrote.
		bool WrittenByTheProgram(const HIRFunction::Slot& slot)
		{
			return slot.cls == SlotClass::Local && !slot.name.empty() && slot.name.rfind("__", 0) != 0;
		}

		const SourceLocation* DeclaredAt(const AstNode* node) { return node ? &node->startLocation : nullptr; }
	}

	DebugInfo::DebugInfo(llvm::Module& module, std::unordered_map<uint32, std::string> sourcePaths)
		: m_Module(module), m_Builder(module), m_SourcePaths(std::move(sourcePaths))
	{
		module.addModuleFlag(llvm::Module::Warning, "CodeView", 1);
		// Each object carries its types' global hashes (`.debug$H`), made once when a partition is assembled, which the
		// link would otherwise compute for every object on every build.
		module.addModuleFlag(llvm::Module::Warning, "CodeViewGHash", 1);
		module.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
		uint32 first = 0;
		for (const auto& [unit, path] : m_SourcePaths)
			if (first == 0 || unit < first) first = unit;
		const std::string producer = "C4 " + std::to_string(CompilerVersion::Major) + "." + std::to_string(CompilerVersion::Minor) + "." +
			std::to_string(CompilerVersion::Patch);
		m_Unit = m_Builder.createCompileUnit(llvm::DISourceLanguageName(llvm::dwarf::DW_LANG_C_plus_plus_14), File(first), producer,
			false, "", 0);
	}

	llvm::DIFile* DebugInfo::File(uint32 unit)
	{
		auto cached = m_Files.find(unit);
		if (cached != m_Files.end()) return cached->second;
		auto path = m_SourcePaths.find(unit);
		const std::filesystem::path file = path != m_SourcePaths.end() ? std::filesystem::path(path->second) : std::filesystem::path("<compiler>");
		llvm::DIFile* described = m_Builder.createFile(file.filename().string(), file.parent_path().string());
		m_Files[unit] = described;
		return described;
	}

	// A statement of `function` from another file (a template's body, say) is in a scope of that file.
	llvm::DIScope* DebugInfo::ScopeIn(llvm::DISubprogram* function, uint32 unit)
	{
		llvm::DIFile* file = File(unit);
		return file == function->getFile() ? static_cast<llvm::DIScope*>(function) : m_Builder.createLexicalBlockFile(function, file);
	}

	llvm::DILocation* DebugInfo::At(const SourceLocation& at, llvm::DISubprogram* function)
	{
		if (!function || at.line == 0) return nullptr;
		return llvm::DILocation::get(m_Module.getContext(), at.line, at.column, ScopeIn(function, at.sourceId));
	}

	llvm::DISubroutineType* DebugInfo::SignatureOf(const FunctionRecord& record)
	{
		std::vector<llvm::Metadata*> types = { record.type ? TypeOf(record.type->GetReturnType()) : nullptr };
		if (record.type)
			for (const auto& param : record.type->GetParameters()) types.push_back(TypeOf(param.m_Type));
		return m_Builder.createSubroutineType(m_Builder.getOrCreateTypeArray(types));
	}

	llvm::DISubprogram* DebugInfo::Describe(llvm::Function& fn, const FunctionRecord& record)
	{
		const SourceLocation* at = DeclaredAt(record.bodyNode);
		if (!at || at->line == 0) return nullptr;
		llvm::DIFile* file = File(at->sourceId);
		llvm::DISubprogram* function = m_Builder.createFunction(file, FunctionName(record), fn.getName(), file, at->line, SignatureOf(record),
			at->line, llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagDefinition);
		fn.setSubprogram(function);
		return function;
	}

	void DebugInfo::DeclareVariables(llvm::DISubprogram* function, const HIRFunction& hir, IRContext& ctx, llvm::BasicBlock& entry)
	{
		const SourceLocation opening = hir.root ? hir.root->origin : SourceLocation();
		llvm::DILocation* at = At(opening, function);
		if (!at) return;
		unsigned argument = 1;
		if (ctx.This())
		{
			// `this` arrives in a register; give it a home the debugger can read.
			llvm::AllocaInst* home = ctx.EntryAlloca(ctx.This()->getType(), "this.addr");
			ctx.builder().CreateStore(ctx.This(), home);
			const Type* self = hir.record->containingType ? static_cast<const Type*>(hir.record->containingType) : hir.record->primitiveReceiverType;
			llvm::DIType* pointer = m_Builder.createObjectPointerType(m_Builder.createPointerType(TypeOf(self), kPointerBits), true);
			auto* variable = m_Builder.createParameterVariable(function, "this", argument++, function->getFile(), opening.line, pointer, true,
				llvm::DINode::FlagArtificial | llvm::DINode::FlagObjectPointer);
			m_Builder.insertDeclare(home, variable, m_Builder.createExpression(), at, &entry);
		}
		const uint32 parameters = hir.record->type ? hir.record->type->GetParameterCount() : 0;
		for (uint32 i = 0; i < hir.SlotCount(); ++i)
		{
			const HIRFunction::Slot& slot = hir.GetSlot(i);
			if (i < parameters)
			{
				auto* variable = m_Builder.createParameterVariable(function, slot.name.c_str(), argument++, function->getFile(), opening.line,
					TypeOf(slot.type), true);
				m_Builder.insertDeclare(ctx.SlotAlloca(i), variable, m_Builder.createExpression(), at, &entry);
			}
			else if (WrittenByTheProgram(slot) && ctx.SlotAlloca(i))
				DeclareLocal(function, slot, ctx.SlotAlloca(i), entry);
		}
	}

	void DebugInfo::DeclareLocal(llvm::DISubprogram* function, const HIRFunction::Slot& slot, llvm::Value* storage, llvm::BasicBlock& entry)
	{
		llvm::DILocation* at = At(slot.declared, function);
		if (!at) return;
		auto* variable = m_Builder.createAutoVariable(at->getScope(), slot.name.c_str(), File(slot.declared.sourceId), slot.declared.line,
			TypeOf(slot.type), true);
		m_Builder.insertDeclare(storage, variable, m_Builder.createExpression(), at, &entry);
	}

	// A declaration and its definition register one global twice; the first describes it.
	void DebugInfo::DescribeGlobal(llvm::GlobalVariable& global, const SymbolInfo& symbol, const Scope& declaredIn)
	{
		if (HasDebugInfo(global)) return;
		const SourceLocation* at = DeclaredAt(symbol.declNode);
		llvm::DIFile* file = File(at ? at->sourceId : 0);
		const std::string name = ScopedName(&declaredIn, symbol.plainName.empty() ? symbol.mangledName : symbol.plainName);
		global.addDebugInfo(m_Builder.createGlobalVariableExpression(m_Unit, name, global.getName(), file, at ? at->line : 0,
			TypeOf(symbol.type), !global.hasExternalLinkage()));
	}

	llvm::DIType* DebugInfo::TypeOf(const Type* type)
	{
		if (!type) return nullptr;
		auto cached = m_Types.find(type);
		if (cached != m_Types.end()) return cached->second;
		llvm::DIType* described = nullptr;
		switch (type->GetKind())
		{
		case TypeKind::Void: return nullptr;
		case TypeKind::Object: return ObjectOf(static_cast<const ObjectType&>(*type)); // caches itself before its members
		case TypeKind::Union: return UnionOf(static_cast<const UnionType&>(*type));
		case TypeKind::Enum: described = EnumOf(static_cast<const EnumType&>(*type)); break;
		case TypeKind::Pointer:
			described = m_Builder.createPointerType(TypeOf(TypeHandle::GetPointedType(type)), kPointerBits);
			break;
		case TypeKind::Reference:
			described = m_Builder.createReferenceType(llvm::dwarf::DW_TAG_reference_type,
				TypeOf(static_cast<const ReferenceType*>(type)->GetReferentType()), kPointerBits);
			break;
		case TypeKind::Alias:
		{
			const auto* alias = static_cast<const AliasType*>(type);
			described = m_Builder.createTypedef(TypeOf(alias->GetAliasedType()), alias->GetName().c_str(), nullptr, 0, m_Unit);
			break;
		}
		case TypeKind::Array:
		{
			const auto* array = static_cast<const ArrayType*>(type);
			described = m_Builder.createArrayType(type->GetSize() * kBitsPerByte, static_cast<uint32_t>(type->GetAlignment() * kBitsPerByte),
				TypeOf(array->GetElementType()), SubrangeOf(array->GetElementCount()));
			break;
		}
		case TypeKind::Swizzle:
		{
			const auto* lanes = static_cast<const SwizzleType*>(type);
			described = m_Builder.createArrayType(type->GetSize() * kBitsPerByte, static_cast<uint32_t>(type->GetAlignment() * kBitsPerByte),
				TypeOf(lanes->GetElementType()), SubrangeOf(lanes->GetLaneCount()));
			break;
		}
		case TypeKind::FunPtr:
			described = m_Builder.createPointerType(SubroutineOf(static_cast<const SignatureType&>(*type)), kPointerBits);
			break;
		case TypeKind::Func: described = FuncOf(static_cast<const SignatureType&>(*type)); break;
		default:
			described = type->GetKind() <= TypeKind::Char32 ? PrimitiveOf(*type) : BytesOf(*type);
			break;
		}
		m_Types[type] = described;
		return described;
	}

	llvm::DIType* DebugInfo::PrimitiveOf(const Type& type)
	{
		unsigned encoding = llvm::dwarf::DW_ATE_signed;
		switch (type.GetKind())
		{
		case TypeKind::Bool: encoding = llvm::dwarf::DW_ATE_boolean; break;
		case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
		case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
			encoding = llvm::dwarf::DW_ATE_unsigned; break;
		case TypeKind::Float32: case TypeKind::Float64: encoding = llvm::dwarf::DW_ATE_float; break;
		case TypeKind::Char8: encoding = llvm::dwarf::DW_ATE_unsigned_char; break;
		case TypeKind::Char16: case TypeKind::Char32: encoding = llvm::dwarf::DW_ATE_UTF; break;
		default: break;
		}
		// CodeView has one narrow character type, which LLVM picks by this name; without it a char8 is a uint8.
		const std::string name = type.GetKind() == TypeKind::Char8 ? "char" : type.GetName().c_str();
		return m_Builder.createBasicType(name, type.GetSize() * kBitsPerByte, encoding);
	}

	llvm::DISubroutineType* DebugInfo::SubroutineOf(const SignatureType& signature)
	{
		std::vector<llvm::Metadata*> types = { TypeOf(signature.GetReturnType()) };
		for (const Type* param : signature.GetParameterTypes()) types.push_back(TypeOf(param));
		return m_Builder.createSubroutineType(m_Builder.getOrCreateTypeArray(types));
	}

	// `func<R(A)>` is { code, environment } (FuncEmitter): the code called with the environment as its `this`.
	llvm::DIType* DebugInfo::FuncOf(const SignatureType& signature)
	{
		const std::string name = signature.GetName().c_str();
		llvm::DIType* code = m_Builder.createPointerType(SubroutineOf(signature), kPointerBits);
		llvm::DIType* environment = m_Builder.createPointerType(nullptr, kPointerBits);
		llvm::DICompositeType* described = m_Builder.createStructType(m_Unit, name, nullptr, 0, signature.GetSize() * kBitsPerByte,
			static_cast<uint32_t>(signature.GetAlignment() * kBitsPerByte), llvm::DINode::FlagZero, nullptr, m_Builder.getOrCreateArray({}), 0,
			nullptr, name);
		llvm::Metadata* members[] = {
			m_Builder.createMemberType(described, "code", nullptr, 0, kPointerBits, kPointerBits, 0, llvm::DINode::FlagPublic, code),
			m_Builder.createMemberType(described, "environment", nullptr, 0, kPointerBits, kPointerBits, kPointerBits, llvm::DINode::FlagPublic,
				environment) };
		m_Builder.replaceArrays(described, m_Builder.getOrCreateArray(members));
		return described;
	}

	// A type the debugger cannot take apart (an initializer list): its bytes.
	llvm::DIType* DebugInfo::BytesOf(const Type& type)
	{
		llvm::DIType* byte = m_Builder.createBasicType("uint8", kBitsPerByte, llvm::dwarf::DW_ATE_unsigned);
		return m_Builder.createArrayType(type.GetSize() * kBitsPerByte, static_cast<uint32_t>(type.GetAlignment() * kBitsPerByte), byte,
			SubrangeOf(type.GetSize()));
	}

	llvm::DINodeArray DebugInfo::SubrangeOf(uint64 count)
	{
		llvm::Metadata* range = m_Builder.getOrCreateSubrange(0, static_cast<int64_t>(count));
		return m_Builder.getOrCreateArray({ range });
	}

	llvm::DIType* DebugInfo::EnumOf(const EnumType& enumeration)
	{
		const SourceLocation* at = DeclaredAt(enumeration.GetDeclNode());
		std::vector<llvm::Metadata*> values;
		const bool isUnsigned = enumeration.GetUnderlyingType() && enumeration.GetUnderlyingType()->GetKind() >= TypeKind::UInt8 &&
			enumeration.GetUnderlyingType()->GetKind() <= TypeKind::UInt64;
		for (const auto& member : enumeration.GetMembers())
			values.push_back(m_Builder.createEnumerator(member.mName.c_str(), member.m_Value, isUnsigned));
		return m_Builder.createEnumerationType(m_Unit, QualifiedName(&enumeration).c_str(), File(at ? at->sourceId : 0), at ? at->line : 0,
			enumeration.GetSize() * kBitsPerByte, static_cast<uint32_t>(enumeration.GetAlignment() * kBitsPerByte), m_Builder.getOrCreateArray(values),
			TypeOf(enumeration.GetUnderlyingType()));
	}

	llvm::DIType* DebugInfo::ObjectOf(const ObjectType& object)
	{
		auto cached = m_Types.find(&object);
		if (cached != m_Types.end()) return cached->second;
		const SourceLocation* at = DeclaredAt(object.GetDeclNode());
		llvm::DIFile* file = File(at ? at->sourceId : 0);
		const std::string name = QualifiedName(&object).c_str();
		llvm::DICompositeType* described = m_Builder.createStructType(m_Unit, name, file, at ? at->line : 0, object.GetSize() * kBitsPerByte,
			static_cast<uint32_t>(object.GetAlignment() * kBitsPerByte), llvm::DINode::FlagZero, nullptr, m_Builder.getOrCreateArray({}), 0,
			nullptr, name);
		m_Types[&object] = described; // a member may point back at the object
		std::vector<llvm::Metadata*> members;
		for (uint32 i = 0; i < object.GetBaseTypes().Count(); ++i)
			members.push_back(m_Builder.createInheritance(described, TypeOf(object.GetBaseTypes()[i]), object.GetBaseOffset(i) * kBitsPerByte, 0,
				llvm::DINode::FlagPublic));
		AddFields(described, object.GetFields(), members);
		m_Builder.replaceArrays(described, m_Builder.getOrCreateArray(members));
		return described;
	}

	llvm::DIType* DebugInfo::UnionOf(const UnionType& alternatives)
	{
		auto cached = m_Types.find(&alternatives);
		if (cached != m_Types.end()) return cached->second;
		const SourceLocation* at = DeclaredAt(alternatives.GetDeclNode());
		const std::string name = QualifiedName(&alternatives).c_str();
		llvm::DICompositeType* described = m_Builder.createUnionType(m_Unit, name, File(at ? at->sourceId : 0), at ? at->line : 0,
			alternatives.GetSize() * kBitsPerByte, static_cast<uint32_t>(alternatives.GetAlignment() * kBitsPerByte), llvm::DINode::FlagZero,
			m_Builder.getOrCreateArray({}), 0, name);
		m_Types[&alternatives] = described;
		std::vector<llvm::Metadata*> members;
		AddFields(described, alternatives.GetFields(), members);
		m_Builder.replaceArrays(described, m_Builder.getOrCreateArray(members));
		return described;
	}

	void DebugInfo::AddFields(llvm::DICompositeType* owner, const Core::Vector<ObjectType::FieldInfo>& fields, std::vector<llvm::Metadata*>& members)
	{
		for (const auto& field : fields)
		{
			if (field.m_IsStatic || !field.m_Type) continue;
			members.push_back(m_Builder.createMemberType(owner, field.mName.c_str(), owner->getFile(), owner->getLine(),
				field.m_Type->GetSize() * kBitsPerByte, static_cast<uint32_t>(field.m_Type->GetAlignment() * kBitsPerByte),
				field.m_Offset * kBitsPerByte, llvm::DINode::FlagPublic, TypeOf(field.m_Type)));
		}
	}
}
