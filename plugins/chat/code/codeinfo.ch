// The `code` tool group (docs/13 "Navigating code"), deferred: what a language server says of one symbol - its hover
// (declaration, type, docs), a type's members each with its declaration as written (static, const, inline, virtual,
// return and parameter types) and modifiers, the type of what is at a place, implementations, callers and callees,
// base and derived types, what a macro use expands to, and a request a language plugin exposes to the AI. A tool the
// serving server cannot answer says so and names the tool that can.

include "codenav.ch"

namespace Ai
{
	const int64 kCodeMembersDefault = 80;
	const int64 kCodeMembersMost = 300;
	const int64 kCodeHierarchyNodesMost = 150;
	const int64 kCodeHierarchyDepthMost = 5;
	const int64 kCodeImplementationsMost = 40;
	const int64 kCodeCustomResultBytes = 16 * 1024;

	void AddCodeInfoTools(ToolRegistry& registry);
	// A hover's text: MarkupContent, a MarkedString or an array of them, as markdown.
	Core::String HoverText(const Serialization::DomNode& hover);
	// The declaration a hover's first code block holds, on one line, its comment lines left out; empty without a block.
	Core::String HoverDeclaration(Core::StringView markdown);
	// The first line of a hover's prose (a doc comment), at most 120 bytes; empty without one.
	Core::String HoverSummary(Core::StringView markdown);
	// Semantic tokens decoded: for each token, its line, column (UTF-16) and modifier names from `legend`.
	class CodeToken
	{
		public int64 line;
		public int64 character;
		public Core::Array<Core::String> modifiers;
	}
	Core::Array<CodeToken> DecodeTokens(const Serialization::DomNode& data, const Serialization::DomNode& legend);
}
