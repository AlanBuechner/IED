#include "Backend/CodeGen/UseSites.h"
#include "Model/Types/QualifiedName.h"

namespace Scripting
{
	namespace
	{
		const FunctionRecord* Target(const HIRNode& node)
		{
			if (node.kind == HIRNode::Kind::Call) return static_cast<const CallNode&>(node).callee;
			if (node.kind == HIRNode::Kind::FunctionAddress) return static_cast<const FunctionAddressNode&>(node).fn;
			return nullptr;
		}

		Core::String InstanceName(const FunctionRecord& made)
		{
			const Core::String name = made.name.empty() ? made.mangledName : made.name;
			return made.containingType ? QualifiedName(made.containingType) + "::" + name : name;
		}
	}

	SourceLocation UseSite(const HIRFunction& user, const FunctionRecord& callee)
	{
		for (const std::unique_ptr<HIRNode>& node : user.m_Nodes)
			if (Target(*node) == &callee && node->origin.line != 0) return node->origin;
		for (const std::unique_ptr<HIRFunction>& nested : user.nestedFunctions)
			if (SourceLocation found = UseSite(*nested, callee); found.line != 0) return found;
		return SourceLocation();
	}

	Core::String UseNote(const ProgramModel& model, const FunctionRecord& made, const HIRFunction* user)
	{
		if (made.templateOrigin == TemplateOrigin::None || !user) return Core::String();
		const SourceLocation at = UseSite(*user, made);
		if (at.line == 0) return " (in '" + InstanceName(made) + "')";
		return " (in '" + InstanceName(made) + "', used at " + model.SourceName(at.sourceId) + " (" + std::to_string(at.line) + ":" +
			std::to_string(at.column) + "))";
	}
}
