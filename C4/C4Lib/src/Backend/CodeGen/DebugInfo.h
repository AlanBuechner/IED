#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/Types/ObjectType.h"
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/Module.h>
#include <string>
#include <unordered_map>

namespace Scripting
{
	class IRContext;
	class SignatureType;
	class UnionType;

	/// The debug information of one module (B28), as CodeView for the Windows debuggers: a compile unit, the source
	/// files, a subprogram per function written in C4, the C4 types, the parameters and locals, and a line for every
	/// statement. Compiler-written functions get none.
	class DebugInfo
	{
	public:
		/// `sourcePaths`: each unit's absolute path.
		DebugInfo(llvm::Module& module, std::unordered_map<uint32, std::string> sourcePaths);

		/// The subprogram of `record`'s body, attached to `fn`; null for a function the compiler wrote.
		llvm::DISubprogram* Describe(llvm::Function& fn, const FunctionRecord& record);
		/// The parameters (`this` first, when the function has one) and the named locals of `hir` at their storage.
		void DeclareVariables(llvm::DISubprogram* function, const HIRFunction& hir, IRContext& ctx, llvm::BasicBlock& entry);
		/// A global's name, type and place.
		void DescribeGlobal(llvm::GlobalVariable& global, const SymbolInfo& symbol, const Scope& declaredIn);
		/// The line and column of `at` in `function`; null when `at` has no line.
		llvm::DILocation* At(const SourceLocation& at, llvm::DISubprogram* function);
		/// Completes the metadata; the module verifies after this.
		void Finish() { m_Builder.finalize(); }

	private:
		llvm::DIFile* File(uint32 unit);
		llvm::DIScope* ScopeIn(llvm::DISubprogram* function, uint32 unit);
		llvm::DIType* TypeOf(const Type* type);
		llvm::DIType* PrimitiveOf(const Type& type);
		llvm::DIType* ObjectOf(const ObjectType& object);
		llvm::DIType* UnionOf(const UnionType& alternatives);
		void AddFields(llvm::DICompositeType* owner, const Core::Vector<ObjectType::FieldInfo>& fields, std::vector<llvm::Metadata*>& members);
		llvm::DIType* EnumOf(const EnumType& enumeration);
		llvm::DIType* BytesOf(const Type& type);
		llvm::DINodeArray SubrangeOf(uint64 count);
		llvm::DISubroutineType* SignatureOf(const FunctionRecord& record);
		llvm::DISubroutineType* SubroutineOf(const SignatureType& signature);
		llvm::DIType* FuncOf(const SignatureType& signature);
		void DeclareLocal(llvm::DISubprogram* function, const HIRFunction::Slot& slot, llvm::Value* storage, llvm::BasicBlock& entry);

		llvm::Module& m_Module;
		llvm::DIBuilder m_Builder;
		std::unordered_map<uint32, std::string> m_SourcePaths;
		llvm::DICompileUnit* m_Unit = nullptr;
		std::unordered_map<uint32, llvm::DIFile*> m_Files;
		std::unordered_map<const Type*, llvm::DIType*> m_Types;
	};
}
