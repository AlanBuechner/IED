#pragma once
#include "Core/Core.h"

namespace Scripting
{
	struct AstNode;
	struct TemplateDefinitionNode;
	struct TemplateArgumentListNode;

	/// The patterns that identify a template for ordering: a class template specialization's arguments
	/// (`Box<T*>` -> `T*`), or a function template's parameter types.
	struct TemplatePatterns
	{
		Core::Vector<const AstNode*> patterns;          // the ones before a trailing pack
		const AstNode* expansion = nullptr;             // the trailing pack's pattern: `Rest` of `Rest...`, `Ts` of `Ts... args`
		const TemplateDefinitionNode* params = nullptr; // the template's own parameters (may have none)

		/// A template argument list as patterns: `Tuple<T, Rest...>` is `T`, then the expansion `Rest`.
		static TemplatePatterns OfArguments(const TemplateArgumentListNode* arguments, const TemplateDefinitionNode* params);
	};

	/// C++ partial ordering by patterns (docs/language/08 7): `specific` is at least as specialized as `general`
	/// when every pattern of `general` - its template parameters standing for anything - matches the
	/// corresponding pattern of `specific`, whose parameters stand for one unknown type each. A trailing
	/// expansion of `general` takes each pattern of `specific` past its own and `specific`'s expansion; one of
	/// `specific` that `general` has no expansion for matches nothing, so `Tuple<T>` is more specialized than
	/// `Tuple<T, Rest...>`.
	namespace PartialOrdering
	{
		bool AtLeastAsSpecialized(const TemplatePatterns& specific, const TemplatePatterns& general);
		/// +1 when `a` is more specialized than `b`, -1 when `b` is more specialized than `a`, 0 when neither is.
		int Compare(const TemplatePatterns& a, const TemplatePatterns& b);
	}
} // namespace Scripting
