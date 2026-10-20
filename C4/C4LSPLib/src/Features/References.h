#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Find-references: return every occurrence of the symbol under `position` in the document.
	/// The result is empty when the position is not on a resolvable symbol.
	class References
	{
	public:
		static Core::Vector<Protocol::Location> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
	};
} // namespace C4LSP::Features
