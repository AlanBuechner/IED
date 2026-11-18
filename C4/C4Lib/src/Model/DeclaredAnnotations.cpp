#include "Model/DeclaredAnnotations.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/Types/ObjectType.h"
#include "Parser/NearNames.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"

namespace Scripting
{
	bool DeclaresAnnotation(const AstNode* declaration)
	{
		const Core::Vector<Ref<AnnotationNode>>* annotations = DeclarationAnnotations(declaration);
		if (!annotations) return false;
		for (const auto& node : *annotations)
			if (node)
				for (const auto& group : node->GetBracketGroups())
					for (const auto& item : group.annotations)
						if (item && item->GetName() == kAnnotationDeclaration) return true;
		return false;
	}

	// A template's instances are no declarations: [annotation] on a template is an error (ModelBuilder::RegisterObject).
	DeclaredAnnotations::DeclaredAnnotations(const ProgramModel& model)
	{
		for (const ObjectType* object : model.ObjectTypes())
		{
			const auto* written = object ? dynamic_cast<const ObjectDefinitionNode*>(object->GetDeclNode()) : nullptr;
			if (!written || written->IsTemplated() || !DeclaresAnnotation(written)) continue;
			m_Declaring.emplace(written->GetNameToken().GetText().c_str(), object);
		}
	}

	std::string DeclaredAnnotations::OwnName(const std::string& name)
	{
		const size_t colon = name.rfind("::");
		return colon == std::string::npos ? name : name.substr(colon + 2);
	}

	const ObjectType* DeclaredAnnotations::Find(const std::string& name) const
	{
		auto found = m_Declaring.find(OwnName(name));
		return found == m_Declaring.end() ? nullptr : found->second;
	}

	const ObjectType* DeclaredAnnotations::MisspeltBy(const std::string& name) const
	{
		const std::string own = OwnName(name);
		if (m_Declaring.count(own)) return nullptr;
		const ObjectType* closest = nullptr;
		std::string closestName;
		size_t closestEdits = 0;
		for (const auto& [declared, type] : m_Declaring)
		{
			if (!IsNearMiss(own, declared)) continue;
			const size_t edits = EditDistance(own, declared);
			if (closest && (edits > closestEdits || (edits == closestEdits && declared > closestName))) continue; // the same answer in any map order
			closest = type;
			closestName = declared;
			closestEdits = edits;
		}
		return closest;
	}
} // namespace Scripting
