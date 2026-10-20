#include "Backend/CodeGen/AliasTags.h"
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/MDBuilder.h>

namespace Scripting
{
	namespace
	{
		const char* NodeName(AliasClass c)
		{
			switch (c)
			{
			case AliasClass::Bool: return "bool";
			case AliasClass::Int16: return "int16";
			case AliasClass::Int32: return "int32";
			case AliasClass::Int64: return "int64";
			case AliasClass::Float32: return "float";
			case AliasClass::Float64: return "double";
			case AliasClass::Pointer: return "pointer";
			default: return "bytes";
			}
		}

		llvm::MDNode* TypeNode(llvm::MDBuilder& md, AliasClass c)
		{
			llvm::MDNode* bytes = md.createTBAAScalarTypeNode("bytes", md.createTBAARoot("C4 TBAA"));
			return c == AliasClass::Bytes ? bytes : md.createTBAAScalarTypeNode(NodeName(c), bytes);
		}
	}

	AliasTags::AliasTags(llvm::LLVMContext& context, std::function<bool(const Type*)> hasVptrs)
		: m_Context(context), m_Classes(std::move(hasVptrs))
	{
	}

	llvm::MDNode* AliasTags::OfAccess(const LValue* loc, const Type* accessed)
	{
		return m_Enabled ? Tag(m_Classes.OfAccess(loc, accessed)) : nullptr;
	}

	llvm::MDNode* AliasTags::OfType(const Type* type)
	{
		return m_Enabled ? Tag(m_Classes.Of(type)) : nullptr;
	}

	llvm::MDNode* AliasTags::Tag(AliasClass c)
	{
		llvm::MDNode*& tag = m_Tags[static_cast<size_t>(c)];
		if (!tag)
		{
			llvm::MDBuilder md(m_Context);
			llvm::MDNode* node = TypeNode(md, c);
			tag = md.createTBAAStructTagNode(node, node, 0);
		}
		return tag;
	}

	llvm::Instruction* Attach(llvm::Instruction* access, llvm::MDNode* tag)
	{
		if (access && tag) access->setMetadata(llvm::LLVMContext::MD_tbaa, tag);
		return access;
	}
} // namespace Scripting
