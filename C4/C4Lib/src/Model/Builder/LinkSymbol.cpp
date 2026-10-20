#include "Model/Builder/LinkSymbol.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"

namespace Scripting
{
	Core::String LinkedInitializerMessage(const Core::String& variable, const Core::String& symbol)
	{
		return "'" + variable + "' is the foreign variable '" + symbol + "' ([link]): it has no initializer, the foreign definition gives its value";
	}

	Core::String LinkSymbolOf(const Core::Vector<Ref<AnnotationNode>>& annotations)
	{
		for (const auto& annotation : annotations)
		{
			if (!annotation) continue;
			for (const auto& group : annotation->GetBracketGroups())
				for (const auto& item : group.annotations)
					if (item && item->GetName() == "link" && item->GetArguments().Count() > 0)
						if (const auto* literal = dynamic_cast<const LiteralNode*>(item->GetArguments()[0].Get()))
							return literal->GetValue();
		}
		return Core::String();
	}
} // namespace Scripting
