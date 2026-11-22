#include "Model/CompileTimeMark.h"
#include "Model/SymbolTable.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"

namespace C4Compiler
{
	namespace
	{
		constexpr const char* c_CompileTime = "compile_time";
	}

	const AnnotationItem* CompileTimeMark(const FunctionRecord& record)
	{
		for (const AstNode* declaration : record.declarations)
			if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(declaration))
				for (const auto& annotation : fn->GetAnnotations())
					if (annotation)
						for (const auto& group : annotation->GetBracketGroups())
							for (const auto& item : group.annotations)
								if (item && item->GetName() == c_CompileTime) return item.Get();
		return nullptr;
	}
} // namespace C4Compiler
