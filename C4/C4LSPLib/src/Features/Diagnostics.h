#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Compiler/ErrorTracker.h" // C4Compiler::CompilationError, ErrorSeverity

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Converts this document's compiler diagnostics into LSP form for publishDiagnostics.
	/// Only errors located in this file are returned (included files report separately).
	class Diagnostics
	{
	public:
		static Core::Vector<Protocol::Diagnostic> ForDocument(Server::CompilationSession& session, const std::string& uri);

	private:
		static Protocol::Diagnostic ToLsp(Server::CompilationSession& session, const C4Compiler::CompilationError& error, uint32 sourceId);
		static Protocol::DiagnosticSeverity SeverityToLsp(C4Compiler::ErrorSeverity severity);
	};
} // namespace C4LSP::Features
