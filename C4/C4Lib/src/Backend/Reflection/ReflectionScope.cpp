#include "Backend/Reflection/ReflectionScope.h"
#include "Compiler/Projects.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"

namespace Scripting
{
	namespace
	{
		constexpr const char* kReflect = "reflect";

		std::string Place(const ProgramModel& model, const SourceLocation& where)
		{
			return std::string(model.SourceName(where.sourceId).c_str()) + " (" + std::to_string(where.line) + ":" + std::to_string(where.column) + ")";
		}

		const AstNode* DeclarationOf(const Type& type)
		{
			if (const auto* object = dynamic_cast<const ObjectType*>(&type)) return object->GetDeclNode();
			if (const auto* en = dynamic_cast<const EnumType*>(&type)) return en->GetDeclNode();
			if (const auto* un = dynamic_cast<const UnionType*>(&type)) return un->GetDeclNode();
			return nullptr;
		}

		// The type a scope holds the members of, the nearest outward: a member's or a nested type's owner.
		const ObjectType* OwnerOf(const Scope* scope)
		{
			for (; scope; scope = scope->GetParent())
				if (scope->GetKind() == Scope::Kind::Class) return scope->objectType;
			return nullptr;
		}

		uint64 KeyOf(const SourceLocation& where) { return (uint64(where.sourceId) << 32) | where.byteOffset; }
	}

	ReflectionScope::ReflectionScope(const ProgramModel& model) : m_Model(model)
	{
		for (const ProgramModel::AnnotatedNamespace& written : model.AnnotatedNamespaces())
		{
			Block block;
			block.setting = Written(written.node->GetAnnotations(), &block.written);
			if (block.setting == Setting::Unset) continue;
			block.start = written.node->startLocation;
			block.end = written.node->endLocation;
			m_Blocks[written.unit].push_back(block);
		}
	}

	// `[reflect("on")]` or `[reflect("off")]` among the annotations; a [reflect] with anything else is an error.
	ReflectionScope::Setting ReflectionScope::Written(const Core::Vector<Ref<AnnotationNode>>& annotations, SourceLocation* where) const
	{
		for (const Ref<AnnotationNode>& node : annotations)
			for (const auto& group : node->GetBracketGroups())
				for (const auto& item : group.annotations)
				{
					if (!item || item->GetName() != kReflect) continue;
					const auto* literal = item->GetArguments().Count() == 1 ? dynamic_cast<const LiteralNode*>(item->GetArguments()[0].Get()) : nullptr;
					const bool on = literal && literal->GetValue() == "on";
					if (!on && !(literal && literal->GetValue() == "off"))
					{
						if (m_Reported.emplace(KeyOf(item->startLocation), true).second)
							m_Errors.AddError(item->startLocation, "[reflect] takes \"on\" or \"off\": [reflect(\"off\")] takes the declaration out of "
								"runtime reflection, [reflect(\"on\")] brings it back (D-89)");
						continue;
					}
					if (where) *where = item->startLocation;
					return on ? Setting::On : Setting::Off;
				}
		return Setting::Unset;
	}

	ReflectionScope::Setting ReflectionScope::Written(const AstNode* declaration, SourceLocation* where) const
	{
		const Core::Vector<Ref<AnnotationNode>>* annotations = declaration ? DeclarationAnnotations(declaration) : nullptr;
		return annotations ? Written(*annotations, where) : Setting::Unset;
	}

	// The innermost namespace block around the declaration that says [reflect], else its project.
	ReflectionScope::Decision ReflectionScope::Around(const AstNode* declaration) const
	{
		if (!declaration) return {};
		const SourceLocation& at = declaration->startLocation;
		if (const auto blocks = m_Blocks.find(at.sourceId); blocks != m_Blocks.end())
		{
			const Block* innermost = nullptr;
			for (const Block& block : blocks->second)
				if (block.start.byteOffset <= at.byteOffset && at.byteOffset <= block.end.byteOffset &&
					(!innermost || block.start.byteOffset >= innermost->start.byteOffset))
					innermost = &block;
			if (innermost && innermost->setting == Setting::On) return {};
			if (innermost) return { false, "its namespace block's [reflect(\"off\")] at " + Place(m_Model, innermost->written) };
		}
		const Project* project = m_Model.ProjectOfUnit(at.sourceId);
		if (!project || project->reflect) return {};
		return { false, "project '" + project->name + "' says reflect = \"off\"" };
	}

	ReflectionScope::Decision ReflectionScope::Decide(const std::vector<const AstNode*>& declarations, const ObjectType* owner) const
	{
		for (const AstNode* declaration : declarations)
		{
			SourceLocation where;
			const Setting setting = Written(declaration, &where);
			if (setting == Setting::On) return {};
			if (setting == Setting::Off) return { false, "its [reflect(\"off\")] at " + Place(m_Model, where) };
		}
		if (owner) return OfType(*owner);
		return Around(declarations.empty() ? nullptr : declarations.front());
	}

	ReflectionScope::Decision ReflectionScope::OfType(const Type& type) const
	{
		if (const auto known = m_Types.find(&type); known != m_Types.end()) return known->second;
		const Scope* around = nullptr;
		if (const auto* en = dynamic_cast<const EnumType*>(&type)) around = en->GetDeclScope();
		else if (const auto* un = dynamic_cast<const UnionType*>(&type)) around = un->GetDeclScope();
		else if (const auto* object = dynamic_cast<const ObjectType*>(&type))
			if (const Scope* members = m_Model.TypeScope(object)) around = members->GetParent();
		const Decision decided = Decide({ DeclarationOf(type) }, OwnerOf(around));
		m_Types.emplace(&type, decided);
		return decided;
	}

	bool ReflectionScope::Has(const Type& type) const { return OfType(type).in; }

	bool ReflectionScope::Has(const FunctionRecord& function) const
	{
		std::vector<const AstNode*> declarations;
		for (const AstNode* declaration : function.declarations) declarations.push_back(declaration);
		return Decide(declarations, function.containingType).in;
	}

	bool ReflectionScope::Has(const SymbolInfo& variable) const
	{
		return Decide({ variable.declNode }, OwnerOf(variable.declaringScope)).in;
	}

	std::string ReflectionScope::WhyOut(const Type& type) const { return OfType(type).why; }
}
