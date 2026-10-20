#pragma once
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"

namespace Scripting
{
	/// The foreign symbol a `[link("sym")]` annotation names (doc 13): a function or variable declared with it is
	/// that symbol, defined outside C4. Empty when the declaration has no `[link]`.
	Core::String LinkSymbolOf(const Core::Vector<Ref<AnnotationNode>>& annotations);

	/// The error for a `[link]` variable written with an initializer: the foreign definition owns its value.
	Core::String LinkedInitializerMessage(const Core::String& variable, const Core::String& symbol);
} // namespace Scripting
