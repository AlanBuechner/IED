// What the AI's code tools ask language servers (docs/13 "Navigating code", docs/24 "Plugin code asks language
// servers"): requests through the host, their answers read into symbols and spans, the symbol a model names
// (`Ring::Push`, `Ai.Ring.Push`) or the place it gives (path, line, column) found, and the text the answers point into,
// cached for one call. Lines here count from 0 and columns in UTF-16 units, as LSP does; what the model sees counts
// lines from 1.

include "Tool.ch"

namespace Ai
{
	const int64 c_CodeLineShownBytes = 160;

	// A stretch of a file.
	class CodeSpan
	{
		public Core::String m_Path;        // a workspace path, or the file: URI of a file outside the workspace
		public int64 m_StartLine;
		public int64 m_StartCharacter;
		public int64 m_EndLine;
		public int64 m_EndCharacter;
	}

	// A symbol as a server describes it.
	class CodeSymbol
	{
		public Core::String m_Name;
		public Core::String m_Container;   // its owner as the server names it ("Ring", "Ai::ToolRegistry"); may be empty
		public int64 m_Kind;               // LSP SymbolKind
		public Core::String m_Detail;
		public CodeSpan m_Range;           // the whole declaration; a workspace symbol's is often only its name
		public CodeSpan m_Selection;       // its name
		public Core::Array<CodeSymbol*> m_Children;

		// Its owners and name joined with "::" ("Ring::Push"); the name when a server qualified it already.
		public Core::String Qualified() const;
	}

	// LSP SymbolKind as a word: "class", "method", ...
	const char* SymbolKindName(int64 kind);
	// Whether a symbol of that kind holds members: a class, struct, interface, enum, namespace or module.
	bool HoldsMembers(int64 kind);
	// "src/ring.cf:20" for the span's first line.
	Core::String WhereOf(const CodeSpan& span, Core::StringView shown);
	// "20-31", or "20" for one line.
	Core::String LinesOf(const CodeSpan& span);
	// A request's params naming a place: {"position": {line, character}}, with `more` (JSON members) after it.
	Core::String PositionParams(const CodeSpan& at, Core::StringView more);
	// `text`'s lines `first` to `last` (0-based, inclusive) numbered from 1 as fs_read numbers them, at most `most`.
	Core::String NumberedSpan(Core::StringView text, int64 first, int64 last, int64 most);
	// `name` split at "::" and "." into its parts.
	Core::Array<Core::String> NameParts(Core::StringView name);
	// What a lookup of `type`::`missing` that found nothing says instead: the type's members, each name once (overloads),
	// at most 60; empty without members.
	Core::String MembersSaid(Core::StringView type, Core::StringView missing, const Core::Array<CodeSymbol*>& members);
	// What else of `found` is named exactly `missing`, for `Type::Missing` whose type has no such member: a free function beside
	// the type is often what was meant. Empty when none is.
	Core::String NamedElsewhereSaid(Core::StringView missing, const Core::Array<CodeSymbol*>& found);
	// The words of a name or phrase: split at anything but letters and digits and where a lower-case letter meets an
	// upper-case one ("LanguageServerPool" -> Language, Server, Pool; "LSPPool" -> LSP, Pool), each at least 3 bytes.
	Core::Array<Core::String> NameWords(Core::StringView text);
	// A qualified name as code writes it where it is defined out of its owner: its last two parts ("Ring::Push" of
	// "Ai::Ring::Push"); empty for a plain name.
	Core::String WrittenOwnerAndName(Core::StringView name);
	// Whether `qualified` ("Ai::Ring::Push") is `want` ("Ring::Push") or ends with "::" and it; "." counts as "::".
	bool QualifiedMatches(Core::StringView qualified, Core::StringView want);
	// Whether a declaration's line only gives another name to a type: C4's `alias A = B;`, C++'s `using A = B;`.
	bool DeclaresAlias(Core::StringView line);

	// One tool call's conversation with the workspace's language servers. Its failures throw IED::RpcError with what
	// the model should do instead.
	class CodeQuery
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(const IED::Workspace& ws);

		public IED::Workspace m_Ws;

		// `method` with `params` to the server serving `path`.
		public Serialization::DomNode Request(Core::StringView path, Core::StringView method, Core::StringView params, bool withDocument);
		// {profile, capabilities, aiRequests} of the server serving `path`; `profile` null when none does.
		public Serialization::DomNode Serving(Core::StringView path);
		// Whether the server serving `path` offers `capability` ("callHierarchyProvider").
		public bool Offers(Core::StringView path, const char* capability);
		// The file's text as edited.
		public Core::StringView Text(Core::StringView path);
		// One line of the file without its break; empty past its end.
		public Core::StringView Line(Core::StringView path, int64 line);
		// The identifier at a place; empty when none is there.
		public Core::String NameAt(const CodeSpan& at);
		// The file's lines; a final break starts no line.
		public int64 LineCount(Core::StringView path);
		// The file as the model is shown it: its workspace path, or its absolute path outside the workspace.
		public Core::String Shown(Core::StringView path) const;
		// The workspace path an answer's URI names, or the file: URI itself for a file outside it.
		public Core::String PathOf(Core::StringView uri) const;
		// The file a tool's `path` argument names: a workspace path, normalized (also when given as its absolute path); a
		// file outside the workspace - by its absolute path, as the tools show one, or its file: URI - as its file: URI.
		public Core::String FileOf(Core::StringView given) const;
		// FileOf's file; when the workspace has none there, the only source file of its name, which a path that missed most
		// likely meant (as fs_read reads it); else FileOf's, for the caller's "no file".
		public Core::String ExistingFileOf(Core::StringView given) const;
		// What to try when `path` (as FileOf gives it) names no file or folder: the files of its name, or how to find one;
		// empty when it names one.
		public Core::String MissedFileHint(Core::StringView path) const;
		// For a plain name no symbol has: the workspace's files named so (RenderShaders.ch for RenderShaders), whose outline
		// was likely meant; empty when there are none.
		public Core::String FileNamedHint(Core::StringView name) const;
		// A Location {uri, range} or LocationLink {targetUri, targetRange, targetSelectionRange}: its name's span with
		// `selection`, else its whole one.
		public CodeSpan SpanOf(const Serialization::DomNode& location, bool selection) const;
		// The file's symbols as a tree (textDocument/documentSymbol); empty when the server gives none.
		public Core::Array<CodeSymbol*> Outline(Core::StringView path);
		// The workspace's symbols for `text` (workspace/symbol of every server).
		public Core::Array<CodeSymbol*> Search(Core::StringView text);
		// The innermost symbol of `path`'s outline whose range holds `line`; null for none.
		public CodeSymbol* Enclosing(Core::StringView path, int64 line);
		// The symbols `name` names - in `path` when given, else in the workspace - each with its whole range when its
		// file's outline has it (a workspace symbol's own place when the outline does not come in the server's time):
		// definitions and declarations, overloads, same-named symbols of other owners.
		public Core::Array<CodeSymbol*> Find(Core::StringView name, Core::StringView path);
		// The members of the type `qualifier` names ("MeshEditor", "Geometry::Mesh"): its outline's children; empty when no type
		// is named so.
		public Core::Array<CodeSymbol*> MembersOf(Core::StringView qualifier);

	private:
		private CodeSymbol* Refined(CodeSymbol* found);
		private void FindInScope(Core::StringView last, Core::StringView qualifier, Core::StringView name, Core::Array<CodeSymbol*>& found);
		private Core::HashMap<Core::String, Core::String> m_Texts;
		private Core::HashMap<Core::String, Core::Array<CodeSymbol*>*> m_Outlines;
		private Core::HashMap<Core::String, Serialization::DomNode> m_Servers;
	}

	// Where a code tool's call points: what its `symbol` named, or the place its `path`, `line` and `column` give.
	class CodeTarget
	{
		public CodeSpan m_At;                         // where position requests are made: the name's first character
		public Core::Array<CodeSymbol*> m_Symbols;    // what `symbol` named; empty for a place
		public Core::String m_Label;                  // "Ring::Push", or "src/ring.cf:20"
	}

	// The target of a call's `symbol`, `path`, `line` and `column`; one symbol's when `symbol` names several of one
	// qualified name (its declaration and definition, overloads), else it fails listing them.
	CodeTarget TargetOf(CodeQuery& query, const Serialization::DomNode& args);
	// As TargetOf, but a plain name that symbols of up to `mostNames` qualified names have keeps them all, its label the name asked:
	// code_read_symbol shows each rather than ask which (GatherCandidates of two packages).
	CodeTarget TargetsOf(CodeQuery& query, const Serialization::DomNode& args, int64 mostNames);
	// The schema properties every code tool's target takes, for a tool's parameters.
	const char* c_CodeTargetProperties = "\"symbol\": {\"type\": \"string\", \"description\": \"a name, qualified to narrow it: Ring::Push\"}, \"path\": {\"type\": \"string\", \"description\": \"the file: narrows symbol, or with line names a place; needed for code outside the workspace (ied:/..., a package's), whose symbols are known only by their file\"}, \"line\": {\"type\": \"integer\", \"minimum\": 1}, \"column\": {\"type\": \"integer\", \"minimum\": 1, \"description\": \"without it, symbol's place on the line, else its first word\"}";
}
