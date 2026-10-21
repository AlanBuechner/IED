#pragma once
#include "Core/Core.h"
#include <cstdint>
#include <vector>

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Semantic-token kinds emitted by the C4 LSP. The set and ordering mirror the standard
	/// "c++-like" token taxonomy used by Visual Studio / clangd so that a client's C++ colouring
	/// (e.g. VS Code's built-in C++ TextMate grammar or its semantic-token theme) applies to C4
	/// sources unchanged, plus one extension kind (`controlFlow`) appended at the END of the list: LSP
	/// token kinds are positional, so appending keeps every pre-existing index stable for any client
	/// that already indexes the standard legend. The legend sent in `initialize` lists these names in
	/// this order; the integer id of a kind is its index in that list.
	enum class SemanticTokenKind : uint32
	{
		Namespace = 0,      // namespace / module name
		Type = 1,           // a user-defined type reference (class names in any position) or an unresolvable type
		Class = 2,          // a user-defined `class` definition
		Enum = 3,           // an `enum` definition
		Interface = 4,      // an `interface` definition
		Struct = 5,         // a `struct` definition
		TypeParameter = 6,  // a template / generic parameter (`template<T>` declaration site + its uses)
		Parameter = 7,      // a function parameter
		Variable = 8,       // a local or global variable
		Field = 9,          // an object field (member) reference
		Function = 10,      // a free-function name / call
		Method = 11,        // a member-function call
		Property = 12,      // (unused by C4 today; reserved for parity)
		Macro = 13,         // an annotation / attribute ([test], [Serialize], ...)
		Keyword = 14,       // a language keyword that is not a type, constant or modifier
		Modifier = 15,      // const / static / virtual / override / access modifier
		Comment = 16,       // a line or block comment
		Number = 17,        // an integer, floating-point or character literal
		String = 18,        // a string (text) literal

		// --- C4 extension kinds (appended AFTER the standard legend so existing ids are stable) ---
		ControlFlow = 19,   // control-flow keywords: return / if-else / while / for / switch-case / break / continue / try-catch

		Count
	};

	/// Bit flags carried alongside each token (LSP `tokenModifiers`). Only the first two are
	/// emitted by C4 today; the rest exist so the legend matches the standard C++ legend.
	enum SemanticTokenModifiers : uint32
	{
		None = 0,
		Declaration = 1 << 0,
		Definition = 1 << 1,
		Static = 1 << 2,
		Readonly = 1 << 3,
		Deprecated = 1 << 4,
		Builtin = 1 << 5, // type reference whose resolved type is a language built-in (int32, bool, ...)
	};

	/// One semantic token: a span of the document tagged with a kind and modifier bits.
	struct SemanticToken
	{
		uint32 startLine = 0;      // 0-based
		uint32 startCharacter = 0; // 0-based, UTF-8 bytes (the server converts for a UTF-16 client)
		uint32 length = 0;         // UTF-8 bytes, like startCharacter
		uint32 kindId = 0;         // index into the legend (SemanticTokenKind)
		uint32 modifiers = None;   // SemanticTokenModifiers bit field
	};

	/// The full set of semantic tokens for a document, in ascending source order.
	struct SemanticTokensResult
	{
		std::vector<SemanticToken> tokens;
	};

	/// Computes the semantic tokens for one open document by re-lexing its editor text and
	/// classifying each token with the compiled AST + semantic result of that file. Empty when the
	/// document is unknown or has no parse tree.
	class SemanticTokens
	{
	public:
		static SemanticTokensResult ForDocument(Server::CompilationSession& session, const std::string& uri);

		/// The legend's token-type names in id order (used to build the `initialize` capability).
		static Core::Vector<Core::String> TokenTypeNames();
		/// The legend's modifier names.
		static Core::Vector<Core::String> ModifierNames();
	};
} // namespace C4LSP::Features
