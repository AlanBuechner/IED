#include "Model/Builder/ModelResolver.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <set>

namespace C4Compiler
{
	namespace
	{
		// A template specialization `node` names in a type position, and those its children name: instantiated (no-op for
		// one that is not a template), each answer kept in `named` when it is given. Every node of every body passes here,
		// so the node's kind is read, not cast.
		void InstantiateNamedTypes(ProgramModel& model, const AstNode* node, uint32 unit, std::vector<const Type*>* named)
		{
			if (!node) return;
			if (node->Type() == NodeType::QualType)
				if (const auto* type = static_cast<const QualTypeNode*>(node); type->HasTemplateArgs())
				{
					const auto resolved = model.ResolveType(*type, unit);
					if (named) named->push_back(resolved.Ok() ? resolved.value : nullptr);
				}
			for (const auto& child : node->Children()) InstantiateNamedTypes(model, child.Get(), unit, named);
		}
	}

	std::vector<const Type*> ModelResolver::TypeInstancesNamedIn(ProgramModel& model, const AstNode* body, uint32 unit)
	{
		std::vector<const Type*> named;
		InstantiateNamedTypes(model, body, unit, &named);
		return named;
	}

	// One walk over the records, the ones the walk makes included: an instance made here adds its methods' records
	// after the ones known, and their bodies are opened in turn. A template's instances share its body, and
	// walking a body again in the same unit resolves the same names, so each (body, unit) is opened once.
	void ModelResolver::InstantiateBodyReferences(ProgramModel& model)
	{
		const auto& records = model.Symbols()->Records();
		std::set<std::pair<const AstNode*, uint32>> opened;
		for (uint32 i = 0; i < records.Count(); ++i)
		{
			const AstNode* body = records[i]->bodyNode;
			const uint32 unit = records[i]->LookupUnit();
			if (body && body->Type() == NodeType::CodeBlock && opened.emplace(body, unit).second)
				InstantiateNamedTypes(model, body, unit, nullptr);
		}
	}
} // namespace C4Compiler
