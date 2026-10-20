#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"

namespace C4LSP::Server { class CompilationSession; }

namespace Scripting { struct AstNode; }

namespace C4LSP::Features
{
	/// C4 Tidy - a clang-tidy-like static analysis pass for C4. It walks the parsed AST of every
	/// compiled source and reports non-error code-quality diagnostics (warnings/information) that
	/// the compiler itself does not flag: unused local variables, self-assignments, tautological
	/// comparisons, and empty loop bodies with no break/return. Diagnostics carry a "c4-tidy"
	/// source tag so clients can distinguish them from real compile errors ("c4"), and they are
	/// merged into the same publishDiagnostics pipeline as compiler diagnostics (see
	/// LspServer::RunRecompileAndPublish).
	///
	/// The whole pass is gated by one server-side flag that the client drives live through
	/// workspace/didChangeConfiguration from a plugin setting; with it off, ForDocument returns an
	/// empty vector and behavior is exactly as before this feature existed.
	class Tidy
	{
	public:
		static Core::Vector<Protocol::Diagnostic> ForDocument(Server::CompilationSession& session, const std::string& uri);

		// Server-side on/off flag (default ON). LspServer updates it from initializationOptions and
		// workspace/didChangeConfiguration; tests set it directly.
		static void SetEnabled(bool enabled);
		static bool IsEnabled();

	private:
		static Protocol::Diagnostic MakeDiag(const Server::CompilationSession& session, uint32 sourceId,
			const Scripting::SourceLocation& start, const Scripting::SourceLocation& end,
			Protocol::DiagnosticSeverity severity, const std::string& message);

		static void CheckUnusedLocals(Server::CompilationSession& session, uint32 sourceId,
			const Ref<Scripting::AstNode>& root, Core::Vector<Protocol::Diagnostic>& out);
		static void CheckExpressionTautologies(Server::CompilationSession& session, uint32 sourceId,
			const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode& node, Core::Vector<Protocol::Diagnostic>& out);
	};
} // namespace C4LSP::Features
