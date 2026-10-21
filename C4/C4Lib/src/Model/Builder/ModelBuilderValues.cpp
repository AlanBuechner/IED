#include "Model/Builder/ModelBuilder.h"
#include "Model/Builder/LinkSymbol.h"
#include "Model/Templates/MemberTemplates.h"
#include "Model/Builder/ObjectIdentity.h"
#include "Model/Builder/ModelResolver.h" // pass 2: fixed-point resolution + GAP A body scan
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Model/ConstEvaluator.h"
#include "Model/OperatorAnnotations.h" // [group("name")] collection for reflection (plan 14)
#include <functional>

namespace Scripting
{
	void ModelBuilder::RegisterVariable(Context& ctx, const VariableDefinitionNode& var)
	{
		// A comma list (`int32 a = 1, b = 2;`) declares several variables of the SAME type. Every
		// declarator gets its own shell and pending entry so each name is queryable downstream.
		for (const auto& d : var.GetDeclarators())
		{
			const Core::String& name = d.m_NameToken.GetText();

			// Shell: the name exists now; the type resolves in pass 2 (forward refs work). The emitted symbol
			// name is fixed here from the declaring scope - it is stable across any later qualified use.
			SymbolInfo info;
			info.kind = SymbolKind::Variable;
			info.type = nullptr;
			info.isConst = var.IsConst();
			info.isStatic = var.IsStatic();
			info.isVolatile = var.IsVolatile();
			info.isThreadLocal = var.IsThreadLocal();
			info.definingUnit = ctx.unitId;
			info.declNode = &var;
			info.initializer = d.m_Initializer ? d.m_Initializer.Get() : nullptr;
			info.mangledName = CanonicalSymbolName(ctx.scope, name);
			// Same-named file-scope statics in different files must not share one symbol (as functions).
			if (info.isStatic && ctx.scope->GetKind() == Scope::Kind::File) info.mangledName += "_u" + std::to_string(ctx.unitId);
			info.plainName = name; // plain C4 name for reflection (plan 14)
			for (const auto& ga : var.GetAnnotations()) CollectGroupAnnotations(ga.Get(), info.groups);
			// [link("sym")] on a variable aliases the named foreign symbol (doc 04 interop): codegen emits an
			// EXTERNAL declaration for `sym` and never allocates its own storage. Only one declarator in a comma
			// list can carry it; capture it from the node's annotations (shared across all declarators).
			info.linkSymbol = LinkSymbolOf(var.GetAnnotations());
			if (!info.linkSymbol.empty() && info.initializer) ErrorAt(ctx, &var, LinkedInitializerMessage(name, info.linkSymbol));
			if (!ctx.scope->Register(name, info)) ErrorAt(ctx, &var, "redefinition of variable '" + name + "'");

			// The just-registered entry (match by declaration node: a same-name static adds a second).
			SymbolInfo* stored = ctx.scope->Entries(name)[0].get();
			for (const auto& e : ctx.scope->Entries(name))
				if (e->declNode == &var) { stored = e.get(); break; }
			m_PendingVars.Push({ stored, &var, ctx.unitId });
		}
	}

	void ModelBuilder::RegisterFunction(Context& ctx, const FunctionDefinitionNode& fn)
	{
		const auto& nameNode = *fn.GetName();
		const Core::String firstName = nameNode.GetSegments()[0].nameToken.GetText();

		// Qualified name (Class::Method or NS::Func): defer the classification to pass 2, where the
		// first segment can be resolved to a type or a namespace. Template methods are special: they
		// cannot resolve without a binding, so they are stored for attachment at instantiation time.
		if (nameNode.SegmentCount() > 1)
		{
			const Core::String lastName = nameNode.GetSegments()[nameNode.SegmentCount() - 1].nameToken.GetText();
			if (fn.IsTemplated())
			{
				// The owner is the segment before the method's (`Geo::Box<T>::Len` -> Box).
				const Core::String owner = nameNode.GetSegments()[nameNode.SegmentCount() - 2].nameToken.GetText();
				ctx.model->m_TemplateMethodDefs.Push({ owner, lastName, (uint32)fn.ParameterCount(), &fn, ctx.scope, ctx.unitId });
				return; // no shell: the owning template's instantiation registers the method
			}
			// Full path minus the last segment ("Ns::C" for "Ns::C::Set"): a multi-segment qualifier
			// must survive to pass 2 intact.
			Core::String prefix;
			for (uint32 s = 0; s < nameNode.SegmentCount() - 1; ++s)
			{
				if (!prefix.empty()) prefix += "::";
				prefix += nameNode.GetSegments()[s].nameToken.GetText();
			}
			m_PendingQualifiedFuncs.Push({ prefix, lastName, &fn, ctx.unitId, ctx.scope });
			return;
		}

		// Unqualified function. A function template is recorded for transparent instantiation and is
		// NOT resolved here (its parameters would be unbound). Non-template functions queue a shell.
		const auto& written = nameNode.GetSegments()[0].templateArgs;
		if (fn.IsTemplated() && written)
		{
			// `template<> int32 F<int32>(int32 v)`: the body F's instance for those arguments uses (D-20).
			if (fn.GetTemplateDefinition()->ParameterCount() > 0)
			{
				ErrorAt(ctx, &fn, "the function template '" + firstName + "' cannot be partially specialized: overload it instead");
				return;
			}
			ProgramModel::TemplateDef def;
			def.params = fn.GetTemplateDefinition().Get();
			def.functionBody = &fn;
			def.declScope = ctx.scope;
			def.pattern = written.Get();
			ctx.model->m_FunctionSpecializations[firstName].Push(def);
			return;
		}
		if (fn.IsTemplated())
		{
			ProgramModel::TemplateDef def;
			def.params = fn.GetTemplateDefinition().Get();
			def.functionBody = &fn;
			def.declScope = ctx.scope;
			ctx.model->m_FunctionTemplates[firstName].Push(def); // overloads and same-named templates in other namespaces coexist
			SymbolInfo marker;
			marker.kind = SymbolKind::Function;
			marker.definingUnit = ctx.unitId;
			marker.declNode = &fn;
			ctx.scope->Register(firstName, marker); // name exists; the set fills at instantiation
			return;
		}

		// Non-template free function / extension method: register the name now (so overloads and
		// "no function named X" are correct), resolve the signature in pass 2.
		SymbolInfo info;
			info.kind = SymbolKind::Function;
			info.definingUnit = ctx.unitId;
			info.declNode = &fn;
			if (!ctx.scope->Register(firstName, info)) ErrorAt(ctx, &fn, Redefinition(ctx, "function", firstName));

			// A body-less declaration in a HEADER: the entry may already exist with a different visibility
			// unit (a definition in another file was visited first). Re-point it to THIS declaration's file so
			// every unit that includes this header sees the symbol - the FunctionRecord keeps its own
			// definingUnit for body lowering. A definition does not re-point (its file already owns the entry).
			if (!fn.GetBody())
				ctx.scope->SetFunctionVisibilityUnit(firstName, ctx.unitId);

			m_PendingFuncs.Push({ ctx.scope, firstName, &fn, ctx.unitId });
	}
} // namespace Scripting
