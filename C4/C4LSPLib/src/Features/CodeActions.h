#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include <string>
#include <vector>

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// One text edit of a code action, in the document `uri` names (the asking document's or another's).
	struct CodeActionEdit
	{
		static constexpr size_t c_NoCaret = static_cast<size_t>(-1);

		std::string uri;
		Protocol::Range range{}; // zero-width for pure insertions
		std::string newText;
		size_t caret = c_NoCaret; // where in newText the cursor goes once it is in: a snippet's $0, for a client that takes them
	};

	/// The squiggle a quickfix fixes, as published in textDocument/publishDiagnostics: clients scope the lightbulb entry to
	/// its marker (where the problem reads, which for an insertion differs from the edit's point).
	struct FixedDiagnostic
	{
		Protocol::Range range{};
		int severity = 0; // LSP severity (1 = Error ..)
		std::string message;
	};

	/// One LSP code action: a title plus the edits that apply it.
	struct CodeAction
	{
		std::string title;
		std::string kind;       // "quickfix" or "source.refactor"
		bool preferred = false; // the fix a client applies by itself ("auto fix") among those for one squiggle
		Core::Vector<FixedDiagnostic> diagnostics; // a quickfix's squiggles (the client filters by marker); empty otherwise
		Core::Vector<CodeActionEdit> edits;
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
	///   * quickfix  "Create definition of 'Name' in File.cf" / "... in place" / "Create the N missing definitions
	///               in File.cf" - for a function declared without a body that nothing defines (MissingDefinitions):
	///               the body goes beside its neighbours' definitions, as Visual Assist's Create Implementation
	///               places it, or replaces the declaration's `;` (DefinitionSites, DefinitionWriter).
	/// The families are independently toggleable; the client drives them live through workspace/didChangeConfiguration
	/// ("quickfixes" / "createDefinition"), so a user can revert to the editor's plain behavior per feature. With both
	/// off, ForDocument returns empty and the server behaves exactly as if this feature never existed (override-only).
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
