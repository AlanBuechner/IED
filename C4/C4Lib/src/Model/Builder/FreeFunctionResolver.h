#pragma once
#include "Model/Builder/ModelBuilder.h"

namespace Scripting
{
	// The function work-lists of pass 2 (doc 02), one of the fixed-point resolvers ModelResolver drives: unqualified
	// free functions and extension methods (m_PendingFuncs) and qualified definitions (m_PendingQualifiedFuncs: a
	// namespaced function `Ns::F` or an out-of-line method `C::M`). A signature resolves in its defining scope and is
	// interned, so a declaration and its definition become one record.
	class FreeFunctionResolver
	{
	public:
		FreeFunctionResolver(ModelBuilder& builder, ModelBuilder::Context& ctx) : m_Builder(builder), m_Ctx(ctx) {}

		/// One pass over both work-lists; true when anything settled.
		bool Resolve();

		/// The namespace or type `Q` in `Q::F`, or null while nothing by that name is visible.
		static const SymbolInfo* QualifierOf(const ProgramModel& model, const ModelBuilder::PendingQualifiedFunc& f);

	private:
		struct Signature
		{
			const Type* returnType = nullptr;
			const FunctionType* type = nullptr; // interned: a declaration and its definition share it
			Core::Vector<FunctionType::ParameterInfo> params;
			uint32 minArity = 0;
		};

		// The type an extension method extends: an object, or a number passed by reference as `this`.
		struct Receiver
		{
			const ObjectType* object = nullptr;
			const Type* primitive = nullptr;
		};

		bool ResolveSignature(const FunctionDefinitionNode& fn, uint32 unitId, const Scope* scope, Signature& out) const;
		Receiver ReceiverOf(const ModelBuilder::PendingFunc& f) const;
		const FunctionRecord* DeclaredMethod(const ObjectType& owner, const Core::String& name, const FunctionType* signature, bool isConst) const;
		void AttachDefinition(const FunctionRecord& rec, const FunctionDefinitionNode& fn, uint32 unitId, const Core::String& shownName) const;

		bool ResolveFree(const ModelBuilder::PendingFunc& f);
		const FunctionRecord* MergeWithDeclaration(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver) const;
		const FunctionRecord* AddFreeRecord(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver);
		FunctionRecord FreeRecord(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver) const;
		void CheckFreeModifiers(const ModelBuilder::PendingFunc& f, const FunctionRecord& rec) const;
		void AddExtensionMethod(const ModelBuilder::PendingFunc& f, const Signature& sig, const FunctionRecord* stored, const ObjectType& target);

		bool ResolveQualified(const ModelBuilder::PendingQualifiedFunc& f);
		bool ResolveOutOfLineMethod(const ModelBuilder::PendingQualifiedFunc& f, const ObjectType& owner);
		Core::String UnmatchedDefinition(const ModelBuilder::PendingQualifiedFunc& f, const ObjectType& owner, const Signature& sig) const;

		ModelBuilder& m_Builder;
		ModelBuilder::Context& m_Ctx;
	};
} // namespace Scripting
