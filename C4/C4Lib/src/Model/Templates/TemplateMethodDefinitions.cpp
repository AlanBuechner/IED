#include "Model/Templates/TemplateMethodDefinitions.h"
#include "Model/Templates/ObjectTemplateBody.h"
#include "Model/Templates/MemberTemplates.h"
#include "Model/TemplateInstantiator.h"
#include "Model/TypeStore.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include <cctype>
#include <unordered_map>

namespace Scripting
{
	namespace
	{
		using Renames = std::unordered_map<Core::String, Core::String>;

		// One template's view of a definition: its parameter names, and the definition's renamed to them.
		struct Parameters
		{
			Core::Vector<Core::String> names;
			Renames renames;
		};

		bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

		// A type's spelling with the definition's parameter names replaced by the declaration's (`U*` -> `T*`).
		Core::String Renamed(const Core::String& spelling, const Renames& renames)
		{
			Core::String out;
			for (size_t i = 0; i < spelling.size(); )
			{
				size_t end = i;
				while (end < spelling.size() && IsWordChar(spelling[end])) ++end;
				if (end == i) { out += spelling[i++]; continue; }
				const Core::String word = spelling.substr(i, end - i);
				auto renamed = renames.find(word);
				out += renamed != renames.end() ? renamed->second : word;
				i = end;
			}
			return out;
		}

		bool Contains(const Core::Vector<Core::String>& names, const Core::String& name)
		{
			for (const Core::String& n : names)
				if (n == name) return true;
			return false;
		}

		// The base type's single name (`T` in `const T&`), or empty for a qualified, template or function type.
		Core::String BaseWord(const QualTypeNode& type)
		{
			if (type.FuncPtrType() || !type.BaseName() || type.BaseName()->SegmentCount() != 1 || type.HasTemplateArgs()) return "";
			return type.BaseName()->GetSegments()[0].nameToken.GetText();
		}

		enum class Fit { Same, Different, Unknown };

		// Two spellings of one parameter type that no template argument can make equal: two parameter types spelled
		// differently (`const T&` and `T`), a parameter type and a built-in one (`T` and `float`), or two built-ins.
		Fit Compare(const ProgramModel& model, const QualTypeNode& declared, const QualTypeNode& defined, const Parameters& params)
		{
			if (declared.ToString() == Renamed(defined.ToString(), params.renames)) return Fit::Same;
			const Core::String a = BaseWord(declared);
			const Core::String b = Renamed(BaseWord(defined), params.renames);
			const bool aParameter = Contains(params.names, a), bParameter = Contains(params.names, b);
			const TypeStore& types = *model.TypeStorePtr();
			if (aParameter && bParameter) return Fit::Different;
			if (aParameter != bParameter) return types.FindBuiltin(aParameter ? b : a) ? Fit::Different : Fit::Unknown;
			const Type* builtinA = types.FindBuiltin(a);
			const Type* builtinB = types.FindBuiltin(b);
			if (!builtinA || !builtinB || declared.IndirectionCount() || defined.IndirectionCount()) return Fit::Unknown;
			return builtinA == builtinB && declared.IsLeadingConst() == defined.IsLeadingConst() ? Fit::Same : Fit::Different;
		}

		bool CanMatch(const ProgramModel& model, const FunctionDefinitionNode& declared, const FunctionDefinitionNode& defined, const Parameters& params)
		{
			if (declared.IsTemplated() || declared.ParameterCount() != defined.ParameterCount() || declared.IsConstMethod() != defined.IsConstMethod())
				return false;
			if (Compare(model, *declared.GetReturnType(), *defined.GetReturnType(), params) == Fit::Different) return false;
			for (uint32 i = 0; i < declared.ParameterCount(); ++i)
				if (Compare(model, *declared.GetParameters()[i]->GetType(), *defined.GetParameters()[i]->GetType(), params) == Fit::Different) return false;
			return true;
		}

		Parameters ParametersOf(const TemplateDefinitionNode* declared, const FunctionDefinitionNode& definition)
		{
			Parameters out;
			const TemplateDefinitionNode* own = ObjectTemplateBody::ClassParameters(definition);
			for (uint32 i = 0; declared && i < declared->ParameterCount(); ++i)
			{
				out.names.Push(declared->GetParameter(i)->GetName());
				if (own && i < own->ParameterCount()) out.renames[own->GetParameter(i)->GetName()] = declared->GetParameter(i)->GetName();
			}
			return out;
		}

		// The methods named `name` the template declares, the ones the definition may define, and how they differ.
		struct Candidates
		{
			uint32 named = 0;
			uint32 fitting = 0;
		};

		Candidates CandidatesIn(const ProgramModel& model, const TemplateDefinitionNode* declared, const ObjectDefinitionNode& body,
			const Core::String& name, const FunctionDefinitionNode& definition)
		{
			Candidates out;
			const Parameters params = ParametersOf(declared, definition);
			for (const auto& member : body.GetBodyMembers())
			{
				const auto* method = dynamic_cast<const FunctionDefinitionNode*>(member.Get());
				if (!method || method->GetName()->GetFirstIdentifierToken().GetText() != name) continue;
				++out.named;
				if (CanMatch(model, *method, definition, params)) ++out.fitting;
			}
			return out;
		}
	}

	Core::Vector<TemplateMethodDefinitions::Mismatch> TemplateMethodDefinitions::Unmatched(const ProgramModel& model)
	{
		Core::Vector<Mismatch> out;
		for (const auto& def : model.m_TemplateMethodDefs)
		{
			if (def.definition->GetOwnerTemplateDefinition()) continue;
			const Core::String qualifier = Qualifier(*def.definition);
			const Core::String start = "the definition of '" + qualifier + "::" + def.name + "' ";
			auto templates = model.m_TypeTemplates.find(def.owner);
			const Scope* ownerScope = OwnerScope(model, def);
			if (templates == model.m_TypeTemplates.end() || !ownerScope)
			{
				if (!DeclaresMemberTemplate(model, def)) out.Push({ def.definition, start + "names no class template '" + qualifier + "'" });
				continue;
			}
			Candidates all;
			for (const ProgramModel::TemplateDef& candidate : templates->second)
			{
				if (!candidate.objectBody || candidate.declScope != ownerScope) continue;
				const Candidates found = CandidatesIn(model, candidate.params, *candidate.objectBody, def.name, *def.definition);
				all.named += found.named;
				all.fitting += found.fitting;
			}
			if (all.fitting) continue;
			out.Push({ def.definition, start + "matches no declaration in '" + qualifier + "': " +
				(all.named ? "its parameters, return type or const differ from every '" + def.name + "' it declares"
					: "'" + qualifier + "' declares no method named '" + def.name + "'") });
		}
		return out;
	}

	const Scope* TemplateMethodDefinitions::OwnerScope(const ProgramModel& model, const ProgramModel::TemplateMethodDef& def)
	{
		if (def.ownerKnown) return def.ownerScope;
		model.Writes().Ask("a template method's owner");
		const Scope* from = QualifierScope(model, def);
		const ProgramModel::TemplateDef* chosen = from ? TemplateInstantiator().ChooseTypeDefinition(model, def.owner, from) : nullptr;
		def.ownerScope = chosen ? chosen->declScope : nullptr;
		def.ownerKnown = true;
		return def.ownerScope;
	}

	// Where the template's name is looked up: the definition's own scope, or the namespace path before the name
	// (`Geo` in `Geo::Box<T>::Set`).
	const Scope* TemplateMethodDefinitions::QualifierScope(const ProgramModel& model, const ProgramModel::TemplateMethodDef& def)
	{
		const auto& segments = def.definition->GetName()->GetSegments();
		if (segments.Count() <= 2) return def.scope;
		Core::String path;
		for (uint32 i = 0; i + 2 < segments.Count(); ++i) path += (i ? "::" : "") + segments[i].nameToken.GetText();
		const SymbolInfo* ns = path.find("::") != Core::String::npos ? model.FindVisibleQualified(path, def.unitId)
			: model.Symbols()->FindVisible(def.scope, path, def.unitId);
		return ns && ns->kind == SymbolKind::Namespace ? ns->scope : nullptr;
	}

	// `template<U> void Plain::F(U u)` defines a member template of a class that is not itself a template.
	bool TemplateMethodDefinitions::DeclaresMemberTemplate(const ProgramModel& model, const ProgramModel::TemplateMethodDef& def)
	{
		for (const auto& entry : model.m_MemberTemplates)
			if (entry.first->GetName() == def.owner && MemberTemplates::Declares(model, entry.first, def.name)) return true;
		return false;
	}

	// `Geo::Box` for `Geo::Box<T>::Set`.
	Core::String TemplateMethodDefinitions::Qualifier(const FunctionDefinitionNode& definition)
	{
		const auto& segments = definition.GetName()->GetSegments();
		Core::String qualifier;
		for (uint32 i = 0; i + 1 < segments.Count(); ++i) qualifier += (i ? "::" : "") + segments[i].nameToken.GetText();
		return qualifier;
	}
} // namespace Scripting
