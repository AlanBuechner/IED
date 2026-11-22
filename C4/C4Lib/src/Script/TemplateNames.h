#pragma once
#include "Core/Core.h"
#include "Parser/AstNode.h"
#include <string>
#include <vector>

namespace C4Compiler
{
	/// The templates a file's declarations declare - a class with a member template counts, its members being defined
	/// out of line by name - by qualified name (`Store::Box`), and those they add to by a specialization, a member defined
	/// out of line or an extension (InterfaceFile::templates and extendedTemplates), each once. An addition is written
	/// `<namespace it is in>|<name as written>` (`Game|Core::KeyTraits`, `|Box` at file scope): the name resolves from that
	/// namespace outward, as a lookup does (TemplateAdditionTargets).
	struct TemplateNames
	{
		std::vector<std::string> declared;
		std::vector<std::string> extended;
	};

	/// What `nodes` - a file's own declarations - declare and add to.
	TemplateNames TemplateNamesOf(const Core::Vector<Ref<AstNode>>& nodes);

	/// The qualified names the addition `extended` (as TemplateNames writes one) can name: written in namespace `A::B` as
	/// `Q::T`, `A::B::Q::T`, `A::Q::T` and `Q::T`, innermost first.
	std::vector<std::string> TemplateAdditionTargets(const std::string& extended);
} // namespace C4Compiler
