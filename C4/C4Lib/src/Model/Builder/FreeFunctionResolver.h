#pragma once
#include "Model/Builder/ModelBuilder.h"

namespace Scripting
{
	class DeclarationQueries;

	// How a pending function resolves (pass 2, docs/compiler/02-program-model.md): an unqualified free function or
	// extension method (m_PendingFuncs) and a qualified definition (m_PendingQualifiedFuncs: a namespaced function
	// `Ns::F` or an out-of-line method `C::M`). A signature resolves in its defining scope and is interned, so a
	// declaration and its definition become one record; a definition of a method first asks for the methods its type
	// declares. A failure is why the function does not resolve.
	class FreeFunctionResolver
	{
	public:
		FreeFunctionResolver(ModelBuilder& builder, ModelBuilder::Context& ctx, DeclarationQueries& queries)
			: m_Builder(builder), m_Ctx(ctx), m_Queries(queries) {}

		QueryResult<void> ResolveFree(const ModelBuilder::PendingFunc& f);
		QueryResult<void> ResolveQualified(const ModelBuilder::PendingQualifiedFunc& f);

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

		/// The namespace or type `Q` in `Q::F`, or null when nothing by that name is visible.
		const SymbolInfo* QualifierOf(const ModelBuilder::PendingQualifiedFunc& f) const;
		QueryResult<Signature> ResolveSignature(const FunctionDefinitionNode& fn, uint32 unitId, const Scope* scope) const;
		Receiver ReceiverOf(const ModelBuilder::PendingFunc& f) const;
		/// The method of that name, signature and constness `owner` declares in its body, its declarations resolved first.
		const FunctionRecord* DeclaredMethod(const ObjectType& owner, const Core::String& name, const FunctionType* signature, bool isConst) const;
		void AttachDefinition(const FunctionRecord& rec, const FunctionDefinitionNode& fn, uint32 unitId, const Core::String& shownName) const;
		/// A parameter's default may be written on the declaration or the definition, and applies to every call (7.4);
		/// written on both, it must be the same expression.
		void MergeDefaults(const FunctionRecord& rec, const FunctionDefinitionNode& fn, const Core::String& shownName) const;

		const FunctionRecord* MergeWithDeclaration(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver) const;
		const FunctionRecord* AddFreeRecord(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver);
		FunctionRecord FreeRecord(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver) const;
		void CheckFreeModifiers(const ModelBuilder::PendingFunc& f, const FunctionRecord& rec) const;
		void AddExtensionMethod(const ModelBuilder::PendingFunc& f, const Signature& sig, const FunctionRecord* stored, const ObjectType& target);

		QueryResult<void> ResolveOutOfLineMethod(const ModelBuilder::PendingQualifiedFunc& f, const ObjectType& owner);
		Core::String UnmatchedDefinition(const ModelBuilder::PendingQualifiedFunc& f, const ObjectType& owner, const Signature& sig) const;

		ModelBuilder& m_Builder;
		ModelBuilder::Context& m_Ctx;
		DeclarationQueries& m_Queries;
	};
} // namespace Scripting
