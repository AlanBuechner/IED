#include "Model/TargetAnnotation.h"
#include "Model/SymbolTable.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunctionType.h"
#include "Model/Types/SwizzleType.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		constexpr const char* kTarget = "target";
		constexpr size_t kXmmBytes = 16;

		// Each [target] item on the declarations, in order.
		std::vector<const AnnotationItem*> TargetItems(const FunctionRecord& function)
		{
			std::vector<const AnnotationItem*> items;
			for (const AstNode* declaration : function.declarations)
				if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(declaration))
					for (const auto& annotation : fn->GetAnnotations())
						if (annotation)
							for (const auto& group : annotation->GetBracketGroups())
								for (const auto& item : group.annotations)
									if (item && item->GetName() == kTarget) items.push_back(item.Get());
			return items;
		}

		std::string Name(const Ref<AstNode>& argument)
		{
			const auto* literal = dynamic_cast<const LiteralNode*>(argument.Get());
			return literal ? std::string(literal->GetValue().c_str()) : std::string();
		}
	}

	std::vector<std::string> TargetFeaturesOf(const FunctionRecord& function)
	{
		std::vector<std::string> features;
		for (const AnnotationItem* item : TargetItems(function))
			for (const auto& argument : item->GetArguments())
				if (const std::string name = Name(argument); std::find(features.begin(), features.end(), name) == features.end())
					features.push_back(name);
		return features;
	}

	SourceLocation TargetAnnotationAt(const FunctionRecord& function)
	{
		const std::vector<const AnnotationItem*> items = TargetItems(function);
		return items.empty() ? SourceLocation() : items.front()->startLocation;
	}

	bool ReturnsTargetDependentVector(const FunctionRecord& function)
	{
		const auto* signature = dynamic_cast<const FunctionType*>(function.type);
		const Type* returned = signature ? TypeHandle::UnwrapToValue(signature->GetReturnType()) : nullptr;
		const auto* vector = returned && returned->GetKind() == TypeKind::Swizzle ? static_cast<const SwizzleType*>(returned) : nullptr;
		return vector && vector->IsPacked() && vector->GetSize() > kXmmBytes;
	}
}
