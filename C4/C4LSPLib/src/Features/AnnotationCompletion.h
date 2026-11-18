#pragma once
#include "Features/Completion.h" // CompletionItem
#include "Model/AnnotationPlaces.h"
#include <optional>
#include <string>

namespace C4LSP::Server { class SemanticResolver; }

namespace C4LSP::Features
{
	/// Completion of an annotation's name inside `[...]` (docs/language/12 section 4.4): the declared annotations that go
	/// where the caret is, each with how it is written, and the compiler's own.
	class AnnotationCompletion
	{
	public:
		/// The caret on an annotation's name: after `[`, or after a `,` in an open `[...]` group.
		struct Site
		{
			std::optional<Scripting::AnnotationPlace> place; // what the annotations are written on, when the text says
		};

		/// The annotation name the caret at byte `caret` of `text` is on; nothing when it is on none. `inType`: the caret is in
		/// a type's body, so a function there is a method and a variable a field.
		static std::optional<Site> Find(const std::string& text, size_t caret, bool inType);

		/// The annotations `site` may name, listed before code's names (a `[` may also start a lambda or a return list).
		static void Add(const Server::SemanticResolver& resolver, const Site& site, Core::Vector<CompletionItem>& out);
	};
} // namespace C4LSP::Features
