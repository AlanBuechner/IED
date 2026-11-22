#pragma once
#include "Core/Core.h"
#include "Features/MissingDefinitions.h"
#include <string>
#include <string_view>
#include <vector>

namespace C4Compiler { struct AstNode; struct FunctionDefinitionNode; struct FunctionRecord; struct ObjectDefinitionNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// How the file a definition goes into writes its functions.
	struct DefinitionStyle
	{
		std::string indent;           // of the definition's first line
		std::string indentUnit = "\t";
		bool braceOnOwnLine = false;  // `{` under the signature rather than after it
		bool writesModifiers = false; // `public static void Type::Method()`, as the file's own definitions are written
	};

	/// A definition as text, and where in it the cursor goes: its body's first line, where its code is written.
	struct WrittenDefinition
	{
		std::string text;
		size_t caret = 0;
	};

	/// What a declaration is declared in, outermost first: its namespaces, then its types.
	struct DeclarationOwners
	{
		std::vector<std::string> namespaces;
		std::vector<const C4Compiler::ObjectDefinitionNode*> types;
	};

	/// Writes the definition of a function declared without a body, from the declaration as written: its return type,
	/// parameters (without their defaults) and trailing qualifiers, under its template headers, with a body that compiles
	/// where its return type has a plain value (`return 0;`, `return nullptr;`, `return {};`).
	class DefinitionWriter
	{
	public:
		/// The definition out of its type's body, its name qualified as code inside `namespaces` (outermost first, a
		/// prefix of the declaration's own) names it: `PlayerControler::OnLoaded` inside `namespace Game`. Empty text when
		/// it cannot be named from there.
		static WrittenDefinition OutOfLine(Server::CompilationSession& session, const MissingDefinition& missing,
			const std::vector<std::string>& namespaces, const DefinitionStyle& style);
		/// The body that takes the place of the declaration's `;`, starting with what separates it from the `)`.
		static WrittenDefinition InPlaceBody(Server::CompilationSession& session, const MissingDefinition& missing, const DefinitionStyle& style);
		/// The statement a new body starts with: `return 0;` for an int32, nothing for void, a reference, or a type an
		/// instance of a template decides.
		static std::string DefaultReturn(const C4Compiler::FunctionRecord* record, std::string_view writtenReturnType, bool templated);

		static DeclarationOwners OwnersOf(Server::CompilationSession& session, const C4Compiler::FunctionDefinitionNode& declaration);
		/// The text a node was parsed from.
		static std::string TextOf(Server::CompilationSession& session, const C4Compiler::AstNode& node);

	private:
		static std::string Signature(Server::CompilationSession& session, const MissingDefinition& missing, const DeclarationOwners& owners,
			const std::vector<std::string>& namespaces, const DefinitionStyle& style);
		static std::string Parameters(Server::CompilationSession& session, const C4Compiler::FunctionDefinitionNode& declaration);
		static std::string TrailingQualifiers(Server::CompilationSession& session, const C4Compiler::FunctionDefinitionNode& declaration);
		static WrittenDefinition Body(const std::string& opening, const DefinitionStyle& style, const std::string& statement);
		static bool IsTemplated(const C4Compiler::FunctionDefinitionNode& declaration, const DeclarationOwners& owners);
	};
} // namespace C4LSP::Features
