#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"

namespace Scripting { class AstNode; class ProgramModel; struct Source; struct FunctionDefinitionNode; struct ParameterNode; class Type; }
namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	// One overload's parameter list, as shown in the signature-help popup.
	struct SignatureParameter
	{
		std::string label; // "int32 x"
	};

	struct SignatureInfo
	{
		std::string label;       // full signature, e.g. "Add(int32 a, int32 b)"
		Core::Vector<SignatureParameter> parameters;
	};

	// Result of textDocument/signatureHelp: the active overload and which parameter the caret is in.
	struct SignatureHelpResult
	{
		bool found = false;
		Core::Vector<SignatureInfo> signatures;
		uint32 activeSignature = 0;
		uint32 activeParameter = 0;
	};

	/// Shows the callee's parameter list with the current argument highlighted, like VS's
	/// signature help. Works for free functions, methods (p.M(...)), and static access (T::M(...)).
	class SignatureHelp
	{
	public:
		static SignatureHelpResult At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

	private:
		static bool FindEnclosingCall(const Scripting::Source& source, const Server::LocatedNode& located, uint32 sourceId,
			const Protocol::Position& position, const Scripting::AstNode*& outCallee, uint32& activeArg);

		// Lexical fallback for a call that has NOT yet parsed into a FunctionCall node because it is still being
		// typed (e.g. `Add(`, `Add(1`, or an auto-closed `Add()` with no trailing `;` - C4 requires the `;`). VS2022 /
		// Visual Assist show parameter hints on the instant you type `(` even though the statement is incomplete, so we
		// recover the callee from raw text: find the nearest unclosed `(` before the caret, read the identifier/member/
		// static chain that precedes it (the callee), and count top-level commas inside those parens to get the active
		// argument slot. Returns false when there is no such call-shaped pattern at the caret.
		static bool LexicalCall(const Scripting::Source& source, size_t caretOffset, std::string& outCalleeName, uint32& activeArg);

		// Resolve a free function (or method) definition by its simple name so the lexical fallback can build a
		// signature. Prefers the unit that contains the caret; falls back to any other unit in the project (the call's
		// callee is often defined in an included file). Returns nullptr when no FunctionDefinition matches.
		static const Scripting::AstNode* FindFunctionByName(Server::CompilationSession& session, uint32 sourceId, const std::string& name);

		static SignatureInfo BuildSignature(const Server::SemanticResolver& resolver, const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode& callee);
		static std::string CalleeName(const Scripting::AstNode& callee);
		static SignatureInfo BuildFromDeclaration(const Scripting::FunctionDefinitionNode& decl, const std::string& name);
		static SignatureInfo BuildFromParameterTypes(const Core::Vector<const Scripting::Type*>& paramTypes, bool variadic, const std::string& name);
		static std::string ParameterText(const Scripting::ParameterNode& param);
	};
} // namespace C4LSP::Features
