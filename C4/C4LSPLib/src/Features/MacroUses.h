#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include <string>
#include <string_view>

namespace Scripting { struct MacroDefinition; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Macros in a document's text (docs/language/02): its `#define` lines, and the macro used at a position with
	/// its definition and what that use expands to under the file's macros (`c4/expandMacro`, hover).
	class MacroUses
	{
	public:
		struct Definition
		{
			std::string name;
			std::string text;          // the directive as written, continuation lines joined
			std::string doc;           // the comment above it, or after it on its line
			Protocol::Range range;     // its lines with the comment above
			Protocol::Range nameRange;
		};

		struct Use
		{
			bool found = false;
			std::string name;
			std::string definition;    // `#define DOUBLE(x) ((x) * 2)`
			std::string expansion;     // what this use becomes: `((21) * 2)`
			std::string doc;           // the definition's comment, when this document defines it
			Protocol::Range range;     // the use: its name, through the `)` of a call
		};

		/// Every `#define` of `text`, in order.
		static Core::Vector<Definition> DefinitionsIn(std::string_view text);
		/// The macro used at `position` of the document, expanded with the macros the file has; not found when no
		/// macro is used there (a `#define` line defines, it does not use).
		static Use At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
		/// `#define NAME(params) body`, as the table holds it.
		static std::string Rendered(const Scripting::MacroDefinition& macro);

	private:
		/// The end of the call whose `(` is the first character from `at` past spaces: one past its `)`; npos when
		/// no `(` follows or it is never closed.
		static size_t CallEnd(std::string_view text, size_t at);
	};
} // namespace C4LSP::Features
