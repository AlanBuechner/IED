#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Find-references: every use of the symbol under `position` in the program, and with `includeDeclaration`
	/// the name of each of its declarations first (a function's declaration and its out-of-line definition).
	/// The result is empty when the position is not on a resolvable symbol.
	class References
	{
	public:
		static Core::Vector<Protocol::Location> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position, bool includeDeclaration = false);
	};
} // namespace C4LSP::Features
