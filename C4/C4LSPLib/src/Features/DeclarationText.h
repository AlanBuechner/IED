#pragma once
#include "Core/Core.h"
#include "Lexer/Token.h"
#include <string>
#include <string_view>

namespace Scripting { struct AstNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// LSP SymbolKind values.
	namespace SymbolKinds
	{
		constexpr int Namespace = 3;
		constexpr int Class = 5;
		constexpr int Method = 6;
		constexpr int Field = 8;
		constexpr int Constructor = 9;
		constexpr int Enum = 10;
		constexpr int Interface = 11;
		constexpr int Function = 12;
		constexpr int Variable = 13;
		constexpr int Constant = 14;
		constexpr int EnumMember = 22;
		constexpr int Struct = 23;
	}

	/// What tools say about a declaration, read from the text it was parsed from (the macro-expanded source, whose
	/// declaration lines are the file's own): the declaration as written, its detail, its owner and its kind.
	class DeclarationText
	{
	public:
		/// The declaration as written up to its body, initializer or members - annotations, access, modifiers, type,
		/// name, parameters with their defaults, trailing qualifiers - continuation lines dedented. Empty for a node
		/// that declares nothing.
		static std::string Written(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// Written, with the name qualified by its owner: `public int64 Ai::ToolRegistry::Find(...) const`.
		static std::string Qualified(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// The declaration without its annotations, access and name, on one line: `static int64 (const StringView& text) const`.
		static std::string Detail(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// The namespaces and types around the declaration and the qualifier it is written with, "::"-joined:
		/// `Ai::ToolRegistry` for `ToolRegistry::Add` defined in `namespace Ai`. Empty at file scope.
		static std::string OwnerOf(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// The simple name it declares (`Add` of `ToolRegistry::Add`); empty for a node that declares none.
		static std::string NameOf(const Scripting::AstNode& decl);
		/// The name as written: `ToolRegistry::Add` for an out-of-line definition, else the simple name.
		static std::string WrittenName(const Scripting::AstNode& decl);
		struct NameLocation { Scripting::SourceLocation start, end, simpleStart; bool found = false; };
		/// Where the written name is (both segments of `ToolRegistry::Add`, `simpleStart` at `Add`); not found for a
		/// node that declares none.
		static NameLocation NameLocationOf(const Scripting::AstNode& decl);
		/// The LSP SymbolKind; `member` when it is declared in an object's body or defined out of line for a type.
		static int SymbolKindOf(const Scripting::AstNode& decl, bool member);
		/// Whether it is a type's: declared in an object's or enum's body, or defined out of line for a type.
		static bool IsMember(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// A function's declaration in its type (where its modifiers and comment are) for its out-of-line definition;
		/// `decl` itself for anything else.
		static const Scripting::AstNode& InItsType(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// The nodes from a unit's root down to `decl` (itself last); empty when no unit holds it.
		static Core::Vector<const Scripting::AstNode*> PathTo(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// The text a source was parsed from; empty when the session has none.
		static std::string_view SourceText(Server::CompilationSession& session, uint32 sourceId);

	private:
		struct Span { size_t start = 0; size_t end = 0; };
		/// Where the declaration's header ends: its body's `{`, its initializer or its `;`.
		static size_t HeaderEnd(std::string_view text, const Scripting::AstNode& decl);
		/// The written name's bytes.
		static Span NameSpan(const Scripting::AstNode& decl);
		/// Where the part after the annotations and the access modifier begins.
		static size_t AfterAccess(std::string_view text, const Scripting::AstNode& decl);
		/// The header from the declaration's start to HeaderEnd, its name written as `name`, dedented.
		static std::string Header(std::string_view text, const Scripting::AstNode& decl, const std::string& name);
		static bool FindPath(const Scripting::AstNode& node, const Scripting::AstNode& target, Core::Vector<const Scripting::AstNode*>& path);
	};
} // namespace C4LSP::Features
