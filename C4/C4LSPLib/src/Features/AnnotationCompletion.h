#pragma once
#include "Features/Completion.h" // CompletionItem
#include "Model/AnnotationPlaces.h"
#include "Model/DeclaredAnnotations.h"
#include <optional>
#include <string>

namespace C4LSP::Server { class SemanticResolver; }

namespace C4LSP::Features
{
	/// Completion of an annotation's name inside `[...]` (docs/language/12 section 4.4): the declared annotations that go
	/// where the caret is, each with how it is written, and the compiler's own; and of the places after `on` in a declaration.
	class AnnotationCompletion
	{
	public:
		/// The caret on an annotation's name: after `[`, or after a `,` in an open `[...]` group.
		struct Site
		{
			std::optional<Scripting::AnnotationPlace> place; // what the annotations are written on, when the text says
			bool onLabel = false;                            // on an access label (`[Property] private:`): its fields' and methods'
			std::string qualifier;                           // what is written before the name: `Scene::` of `[Scene::Ra`
		};

		/// The annotation name the caret at byte `caret` of `text` is on; nothing when it is on none. `inType`: the caret is in
		/// a type's body, so a function there is a method and a variable a field.
		static std::optional<Site> Find(const std::string& text, size_t caret, bool inType);

		/// The annotations `site` may name, listed before code's names (a `[` may also start a lambda or a return list); on a label,
		/// those that go on a field or a method, and none of the compiler's own.
		static void Add(const Server::SemanticResolver& resolver, const Site& site, const Scripting::AnnotationContext& at, Core::Vector<CompletionItem>& out);

		/// Whether the caret at byte `caret` of `text` is on a place of an annotation declaration: after its `on`, or after a
		/// `,` of the places (`annotation Range(double min, double max) on field, |`).
		static bool InPlaces(const std::string& text, size_t caret);
		/// Every place, in the order docs/language/12 section 4.1 lists them.
		static void AddPlaces(Core::Vector<CompletionItem>& out);
	};
} // namespace C4LSP::Features
