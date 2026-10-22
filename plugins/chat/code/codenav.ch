// The code tools of the core set (docs/13 T-LSP-1..4, "Navigating code"): a file's outline or the workspace's symbols
// by name, the whole source of one symbol (a function's body, a type), where a symbol is defined and where it is used -
// so the model reads code by symbol instead of whole files. They ask the workspace's language servers (lspquery.ch);
// the `code` group's tools (codeinfo.ch) say more about one symbol.

include "lspquery.ch"

namespace Ai
{
	const int64 kCodeOutlineMost = 400;        // lines of an outline or a search
	const int64 kCodeReadMostLines = 400;
	const int64 kCodeReferencesDefault = 100;
	const int64 kCodeReferencesMost = 500;
	const int64 kCodeReferenceFilesNamed = 20; // files whose references are named by the symbol they sit in
	const int64 kCodeUnlistedNameBytes = 3;    // shorter names are not checked against the text
	const int64 kCodeUnlistedSearched = 200;
	const int64 kCodeUnlistedShown = 8;
	const int64 kCodeUnlistedMost = 25;        // a name more lines use is a common one: they are counted, not listed
	const int64 kCodeWordMatchesMost = 25;     // symbols a search by a name's words shows
	const int64 kCodeSearchSourceLines = 40;   // a search's lone match this short comes with its source

	void AddCodeNavTools(ToolRegistry& registry);
	// A code tool: read class, offered in Plan mode, `deferred` in the `code` group.
	ToolSpec* CodeToolSpec(const char* name, const char* description, Core::StringView parameters, bool deferred);
	// The plan of a code tool: the subject "code(<symbol or path>)" for the permission rules, and `verb` and it.
	ToolPlan CodePlan(const ToolInvocation& call, const char* verb);
	// The lines a declaration starts with: from its range's start through the line that opens its body or ends it, at
	// most 6, numbered; the comment lines right above it first.
	Core::String DeclarationLines(CodeQuery& query, const CodeSpan& range);
	// "method Ring::Push - src/ring.cf lines 20-31".
	Core::String SymbolHeading(CodeQuery& query, const CodeSymbol* symbol);
	// The spans of a Location / Location[] / LocationLink[] answer, their names' when `selection`.
	Core::Array<CodeSpan> SpansOf(const CodeQuery& query, const Serialization::DomNode& answer, bool selection);
	// The symbol of `span.path`'s outline holding `span`, or null; a file a server cannot outline has none.
	CodeSymbol* SymbolAt(CodeQuery& query, const CodeSpan& span);
	// A place an answer named: the symbol there (SymbolHeading) and its declaration's lines, or the place and its lines.
	void AppendPlace(Core::StringBuilder& out, CodeQuery& query, const CodeSpan& span);
	// The lines of the workspace's files that use `name` as code (UsesAsCode) and `listed` leaves out - uses a language
	// server can miss (through a pointer or a macro) or other symbols of that name - as a note to check them: listed when
	// no other symbol has the name or few lines use it, else only counted, as a common name; nothing when none.
	void AppendUnlisted(Core::StringBuilder& out, CodeQuery& query, Core::StringView name, const Core::Array<CodeSpan>& listed);
	// Whether `line` uses `name` as code does - calls it, reaches it through ".", "->" or "::", or declares a pointer or
	// reference of it ("Ring* ring") - outside a comment and a string.
	bool UsesAsCode(Core::StringView line, Core::StringView name);
	// The name a target names: its symbol's last part, or the identifier at its place.
	Core::String TargetName(CodeQuery& query, const CodeTarget& target);
	// A code tool's parameters: the target's properties (kCodeTargetProperties) and `more` (", "-led members).
	Core::String CodeTargetSchema(const char* more);
	// The first line of the comment lines right above `line`, or `line` when there are none.
	int64 CommentStart(CodeQuery& query, Core::StringView path, int64 line);
}
