#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Document-highlight: return every occurrence of the symbol under `position` in the document,
	/// each tagged with its highlight kind (text/read/write). Empty when not on a resolvable symbol.
	class DocumentHighlight
	{
	public:
		static Core::Vector<Protocol::DocumentHighlight> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
	};
} // namespace C4LSP::Features
