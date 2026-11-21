#include "Model/Builder/FreeFunctionResolver.h"
#include "Model/Builder/DeclarationQueries.h"
#include "Model/TypeHandle.h"
#include "Model/Types/AliasType.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace Scripting
{
	// An unqualified qualifier (`C` in `C::Set` written inside a namespace) resolves from the declaring scope, where the
	// type lives; a qualified one (`Ns::C`) from the program root.
	const SymbolInfo* FreeFunctionResolver::QualifierOf(const ModelBuilder::PendingQualifiedFunc& f) const
	{
		const ProgramModel& model = *m_Ctx.model;
		if (f.prefix.find("::") != Core::String::npos) return model.FindVisibleQualified(f.prefix, f.unitId);
		const Scope* from = f.declScope ? f.declScope : model.Symbols()->GetRoot();
		return model.Symbols()->FindVisible(from, f.prefix, f.unitId);
	}

	QueryResult<void> FreeFunctionResolver::ResolveQualified(const ModelBuilder::PendingQualifiedFunc& f)
	{
		const SymbolInfo* qualifier = QualifierOf(f);
		if (!qualifier) return QueryResult<void>::Fail(QueryError::NotFound, "no namespace or type named '" + f.prefix + "'");
		if (qualifier->kind == SymbolKind::Namespace && qualifier->scope)
			return ResolveFree({ const_cast<Scope*>(qualifier->scope), f.last, f.fn, f.unitId });
		if (qualifier->kind == SymbolKind::Type && qualifier->type)
		{
			// `Alias::Method` defines a method of the type the alias names.
			if (qualifier->type->GetKind() == TypeKind::Alias)
				if (auto named = m_Queries.Alias(*static_cast<const AliasType*>(qualifier->type)); !named.Ok()) return named;
			if (const auto* owner = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(qualifier->type)))
				return ResolveOutOfLineMethod(f, *owner);
			return QueryResult<void>::Fail(QueryError::NotCallable, "'" + f.prefix + "' is a type without methods");
		}
		m_Builder.ErrorAt(m_Ctx, f.fn, "'" + f.prefix + "::" + f.last + "' names no function: '" + f.prefix + "' is not a namespace or a type");
		return QueryResult<void>::Success();
	}

	// The definition must match a method the type declares: the name, the parameter and return types, and const.
	QueryResult<void> FreeFunctionResolver::ResolveOutOfLineMethod(const ModelBuilder::PendingQualifiedFunc& f, const ObjectType& owner)
	{
		auto sig = ResolveSignature(*f.fn, f.unitId, m_Ctx.model->TypeScope(&owner));
		if (!sig.Ok()) return QueryResult<void>::Fail(sig.error, sig.message, sig.loc);
		if (const FunctionRecord* declared = DeclaredMethod(owner, f.last, sig.value.type, f.fn->IsConstMethod()))
			AttachDefinition(*declared, *f.fn, f.unitId, f.prefix + "::" + f.last);
		else
			m_Builder.ErrorAt(m_Ctx, f.fn, UnmatchedDefinition(f, owner, sig.value));
		return QueryResult<void>::Success();
	}

	// `the definition of 'Box::Set' matches no declaration in 'Box': it is void (float); 'Box' declares void (int32)`.
	Core::String FreeFunctionResolver::UnmatchedDefinition(const ModelBuilder::PendingQualifiedFunc& f, const ObjectType& owner, const Signature& sig) const
	{
		const Core::String start = "the definition of '" + f.prefix + "::" + f.last + "' matches no declaration in '" + f.prefix + "': ";
		Core::String declared;
		auto records = m_Ctx.model->m_MethodRecords.find(&owner);
		for (uint32 i = 0; records != m_Ctx.model->m_MethodRecords.end() && i < owner.GetMethodCount() && i < records->second.Count(); ++i)
		{
			const FunctionRecord* rec = records->second[i];
			if (!rec || owner.GetMethods()[i].mName != f.last) continue;
			declared += (declared.empty() ? "" : ", ") + rec->type->ToString() + (rec->isConst ? " const" : "");
		}
		if (declared.empty()) return start + "'" + f.prefix + "' declares no method named '" + f.last + "'";
		return start + "it is " + sig.type->ToString() + (f.fn->IsConstMethod() ? " const" : "") + "; '" + f.prefix + "' declares " + declared;
	}
} // namespace Scripting
