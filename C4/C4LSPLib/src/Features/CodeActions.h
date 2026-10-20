#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include <string>
#include <vector>

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// One LSP code action: a title plus the edits that apply it. Quickfixes carry the squiggle they
	/// fix so clients can scope lightbulb entries to their marker (the diagnostic range is where the
	/// error sits, which for insertions differs from the edit's zero-width insertion point).
	struct CodeAction
	{
		std::string title;
		std::string kind;      // "quickfix" or "source.refactor"
		bool quickfix = false; // true when a diagnostic is attached (client filters by marker)
		int diagnosticSeverity = 0;   // LSP severity of the squiggle this fixes (1=Error..)
		std::string diagnosticMessage;
		bool hasDiagnosticRange = false; // set when diagnosticRange is meaningful (otherwise fall back to editStartEnd)
		Protocol::Range diagnosticRange{}; // squiggle range as published in textDocument/publishDiagnostics

		Protocol::Range editStartEnd{}; // zero-width for pure insertions
		std::string newText;
	};

	/// textDocument/codeAction support - the VS2022 lightbulb. The editor's frontend registers a
	/// Monaco code-action provider gated on the `codeActionProvider` capability and forwards
	/// context.diagnostics, so everything C4-specific lives here in the LSP (plugin-owned):
	///   * quickfix  "Use '->' (pointer dereference)" - fixes the compiler's dot-on-pointer error by
	///               replacing that one '.' token with '->'. The member name is taken from the error
	///               message itself, so the right access is edited even in files with other errors.
	///   * quickfix  "Create definition of 'Name'" - for "Undefined identifier" errors on free-function
	///               calls: inserts a compilable stub (`void Name(...) { }`) immediately before the
	///               statement containing the first use (C4 requires declarations before use). The call
	///               is verified against the AST so variables/types/member calls never get stubs. It is
	///               emitted as a diagnostic-scoped `quickfix` (not source.*): Monaco's inline lightbulb
	///               only surfaces marker-scoped quickfixes and filters out source-kind actions, which is
	///               also how VS2022/Visual Assist X show "create method" directly on the error.
	/// Both are independently toggleable; the client drives them live through workspace/didChangeConfiguration
	/// from plugin settings ("feature-code-action-quickfixes" / "feature-create-definition"), so a user
	/// can revert to the editor's plain behavior per feature. With both off, ForDocument returns empty and the
	/// server behaves exactly as if this feature never existed (override-only).
	class CodeActions
	{
	public:
		static Core::Vector<CodeAction> ForDocument(Server::CompilationSession& session, const std::string& uri);

		// Server-side on/off flags (default ON - the plugin's settings push live overrides; a missing
		// key from a client keeps these defaults). Tests set them directly.
		static void SetQuickfixesEnabled(bool enabled);
		static bool QuickfixesEnabled();
		static void SetCreateDefinitionEnabled(bool enabled);
		static bool CreateDefinitionEnabled();

		/// "Undefined identifier 'Name'" -> Name (identifiers only). False for any other message.
		static bool IsUndefinedIdentifierMessage(const std::string& message, std::string& nameOut);
	};
} // namespace C4LSP::Features
