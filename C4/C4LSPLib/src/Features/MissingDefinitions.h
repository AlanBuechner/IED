#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include <string>

namespace C4Compiler { struct FunctionDefinitionNode; struct FunctionRecord; struct ObjectDefinitionNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// A function declared without a body that no file of its program defines.
	struct MissingDefinition
	{
		const C4Compiler::FunctionDefinitionNode* declaration = nullptr;
		const C4Compiler::FunctionRecord* record = nullptr; // the compiler's function; null for a template no use made yet
	};

	/// Visual Studio's "Function definition for 'Name' not found": a warning on the name of each function a document
	/// declares without a body when nothing in its program gives it one. What counts as defined is the compiler's own
	/// answer (FunctionRecord's HasCode): a body in any file, a body the compiler writes, a foreign function (`[link]`,
	/// bindings) or the script host's. A pure method (`abstract`, `= 0`, an interface's) needs none. A template no use made
	/// yet has no function, so its out-of-line definitions are looked for by name, parameter count and owner.
	///
	/// Published with the compiler's diagnostics (source "c4"); `missingDefinitions` in the initialization options or
	/// workspace/didChangeConfiguration turns it off (on by default). The fixes are CodeActions'.
	class MissingDefinitions
	{
	public:
		static Core::Vector<MissingDefinition> InDocument(Server::CompilationSession& session, uint32 sourceId);
		static Core::Vector<Protocol::Diagnostic> ForDocument(Server::CompilationSession& session, const std::string& uri);

		/// A body written out of line that could be the template `declaration`'s, a member of `owner` (null for a free
		/// function): the same name, owned by a type of its type's name, as many parameters unless a pack makes them any.
		/// What finds a template's definition before any use made a function of it. Null when there is none.
		static const C4Compiler::FunctionDefinitionNode* TemplateBodyFor(Server::CompilationSession& session,
			const C4Compiler::FunctionDefinitionNode& declaration, const C4Compiler::ObjectDefinitionNode* owner);

		/// Where the warning goes: the declaration's simple name.
		static Protocol::Range NameRange(const Server::CompilationSession& session, const C4Compiler::FunctionDefinitionNode& declaration);
		static std::string MessageFor(const C4Compiler::FunctionDefinitionNode& declaration);

		static void SetEnabled(bool enabled);
		static bool IsEnabled();
	};
} // namespace C4LSP::Features
