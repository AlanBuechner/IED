#include "LspNavigationTest.h"
#include <string>

using namespace C4LSP::Features;
using Testing::Lsp::Contains;

namespace Testing
{
	// A namespace with a struct, an interface and a class whose members are of every kind, one defined out of line.
	static const char* kRegistry =
		R"(namespace Ai
{
	struct Span { int32 extent; };

	interface ITool
	{
		public virtual int32 Run() = 0;
	};

	class ToolRegistry
	{
		/// Finds a tool by its name.
		public int64 Find(const Span& text, int64 start = 0) const { return start; }
		public static int32 Count() { return 0; }
		[operator("create")] public void Init() { size = 0; }
		public int32 size; // how many tools it holds
		public bool Add(int32 id);
	};

	bool ToolRegistry::Add(int32 id) { return id > 0; }
}

enum Color { Red, Green };

int32 main()
{
	Ai::ToolRegistry registry;
	Ai::Span span;
	int64 found = registry.Find(span, 1);
	return registry.size;
}
)";

	static const char* kMacros =
		R"(/// Twice its argument.
#define DOUBLE(x) ((x) * 2)
#define TRIPLE(x) (DOUBLE(x) + (x))
#define LIMIT 100
#define SUM(a, b) \
	((a) + (b))
int32 Twice() { return DOUBLE(21); }
int32 Thrice() { return TRIPLE(2) + LIMIT + SUM(1, 2); }
int32 main() { return 0; }
)";

	static const char* kCalls =
		R"(int32 Leaf(int32 v) { return v + 1; }
int32 Middle(int32 v) { return Leaf(v) + Leaf(v * 2); }
int32 Top() { return Middle(3); }
class Runner
{
	public int32 Go() { return Middle(1); }
};
int32 main() { Runner r; return Top() + r.Go(); }
)";

	static const char* kShapes =
		R"(interface IShape
{
	public virtual int32 Area() = 0;
};
class Square : IShape
{
	public override int32 Area() { return 4; }
};
class Tile : Square
{
	public int32 Grout() { return 1; }
};
int32 main() { Tile t; return t.Area(); }
)";

	// A method called unqualified, through a reference, and through `->` chains of pointer members and parameters.
	static const char* kPointers =
		R"(namespace Ied
{
	class Documents
	{
		public int32 ApplyEdit(int32 doc) { return doc; }
		public int32 OnDiskWrite() { return ApplyEdit(1); }
	};

	class Server
	{
		public Documents* documents;
	};

	class Hooks
	{
		public Server* server;
		public int32 ServerEdit() { return server->documents->ApplyEdit(2); }
	};
}

namespace IED
{
	int32 WriteThrough(Ied::Server* server) { return server->documents->ApplyEdit(3); }
	int32 Direct(Ied::Documents& docs) { return docs.ApplyEdit(4); }
}

int32 main() { return 0; }
)";

	static const DocumentSymbol* Named(const Core::Vector<DocumentSymbol>& symbols, const std::string& name)
	{
		for (const auto& symbol : symbols)
			if (symbol.name == name)
				return &symbol;
		return nullptr;
	}

	static const WorkspaceSymbol* Found(const Core::Vector<WorkspaceSymbol>& symbols, const std::string& name, const std::string& container)
	{
		for (const auto& symbol : symbols)
			if (symbol.name == name && symbol.containerName == container)
				return &symbol;
		return nullptr;
	}

	static C4LSP::Protocol::Position After(const std::string& text, const std::string& needle, uint32 offset, int occurrence = 1)
	{
		C4LSP::Protocol::Position position = Testing::Lsp::PositionOf(text, needle, occurrence);
		position.character += offset;
		return position;
	}

	void LspNavigationTest::TestSymbolKindsSayWhatAMemberIs()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		auto symbols = DocumentSymbols::ForDocument(session, Testing::Lsp::UriFor("Registry.cf"));
		const DocumentSymbol* ai = Named(symbols, "Ai");
		TEST_ASSERT_NOT_NULL(ai);
		TEST_ASSERT_EQ(ai->kind, SymbolKinds::Namespace);
		TEST_ASSERT_EQ(Named(ai->children, "Span")->kind, SymbolKinds::Struct);
		TEST_ASSERT_EQ(Named(ai->children, "ITool")->kind, SymbolKinds::Interface);
		const DocumentSymbol* registry = Named(ai->children, "ToolRegistry");
		TEST_ASSERT_NOT_NULL(registry);
		TEST_ASSERT_EQ(registry->kind, SymbolKinds::Class);
		TEST_ASSERT_EQ(Named(registry->children, "Find")->kind, SymbolKinds::Method);
		TEST_ASSERT_EQ(Named(registry->children, "Count")->kind, SymbolKinds::Method);
		TEST_ASSERT_EQ(Named(registry->children, "Init")->kind, SymbolKinds::Constructor);
		TEST_ASSERT_EQ(Named(registry->children, "size")->kind, SymbolKinds::Field);
		const DocumentSymbol* color = Named(symbols, "Color");
		TEST_ASSERT_EQ(color->kind, SymbolKinds::Enum);
		TEST_ASSERT_EQ(Named(color->children, "Red")->kind, SymbolKinds::EnumMember);
		TEST_ASSERT_EQ(Named(symbols, "main")->kind, SymbolKinds::Function);
		TEST_PASSED();
	}

	void LspNavigationTest::TestSymbolDetailIsTheDeclarationWithoutItsName()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		auto symbols = DocumentSymbols::ForDocument(session, Testing::Lsp::UriFor("Registry.cf"));
		const DocumentSymbol* registry = Named(Named(symbols, "Ai")->children, "ToolRegistry");
		TEST_ASSERT_EQ(Named(registry->children, "Find")->detail, std::string("int64 (const Span& text, int64 start = 0) const"));
		TEST_ASSERT_EQ(Named(registry->children, "Count")->detail, std::string("static int32 ()"));
		TEST_ASSERT_EQ(Named(registry->children, "Init")->detail, std::string("void ()")); // no annotation, no access
		TEST_ASSERT_EQ(Named(registry->children, "size")->detail, std::string("int32"));
		TEST_ASSERT_EQ(registry->detail, std::string("class"));
		TEST_ASSERT_EQ(Named(symbols, "Ai")->detail, std::string("namespace"));
		TEST_PASSED();
	}

	void LspNavigationTest::TestOutOfLineDefinitionsAreNamedWithTheirOwner()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		auto symbols = DocumentSymbols::ForDocument(session, Testing::Lsp::UriFor("Registry.cf"));
		const DocumentSymbol* add = Named(Named(symbols, "Ai")->children, "ToolRegistry::Add");
		TEST_ASSERT_NOT_NULL(add);
		TEST_ASSERT_EQ(add->kind, SymbolKinds::Method);
		TEST_ASSERT_EQ(add->detail, std::string("bool (int32 id)"));
		// The selection is the function's own name, after its owner.
		const auto name = Testing::Lsp::PositionOf(kRegistry, "Add(int32 id) {");
		TEST_ASSERT_EQ(add->selectionRange.start.line, name.line);
		TEST_ASSERT_EQ(add->selectionRange.start.character, name.character);

		auto adds = WorkspaceSymbols::ForQuery(session, "Add");
		const WorkspaceSymbol* found = Found(adds, "Add", "Ai::ToolRegistry");
		TEST_ASSERT_NOT_NULL(found);
		TEST_ASSERT_EQ(found->kind, SymbolKinds::Method);
		uint32 inThisFile = 0;
		for (const auto& symbol : adds)
			if (symbol.location.uri == Testing::Lsp::UriFor("Registry.cf"))
			{
				TEST_ASSERT(symbol.name == "Add" && symbol.containerName == "Ai::ToolRegistry"); // the declaration in the class and the definition
				++inThisFile;
			}
		TEST_ASSERT_EQ(inThisFile, 2u);
		TEST_PASSED();
	}

	void LspNavigationTest::TestWorkspaceSymbolContainersAreQualifiedNames()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		const auto finds = WorkspaceSymbols::ForQuery(session, "find");
		const WorkspaceSymbol* find = Found(finds, "Find", "Ai::ToolRegistry");
		TEST_ASSERT_NOT_NULL(find);
		TEST_ASSERT_EQ(find->kind, SymbolKinds::Method);
		const auto sizes = WorkspaceSymbols::ForQuery(session, "size");
		const WorkspaceSymbol* size = Found(sizes, "size", "Ai::ToolRegistry");
		TEST_ASSERT_NOT_NULL(size);
		TEST_ASSERT_EQ(size->kind, SymbolKinds::Field);
		const auto greens = WorkspaceSymbols::ForQuery(session, "green");
		TEST_ASSERT_NOT_NULL(Found(greens, "Green", "Color"));
		TEST_PASSED();
	}

	void LspNavigationTest::TestHoverShowsTheDeclarationAsWrittenWithItsOwnerAndComment()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Registry.cf"), Testing::Lsp::PositionOf(kRegistry, "Find(span"));
		TEST_ASSERT(hover.found);
		TEST_ASSERT_EQ(hover.contents, std::string("```c4\npublic int64 Ai::ToolRegistry::Find(const Span& text, int64 start = 0) const\n```\n\nFinds a tool by its name."));

		// The out-of-line definition hovers as its declaration in the class: there are its modifiers.
		auto add = Hover::At(session, Testing::Lsp::UriFor("Registry.cf"), Testing::Lsp::PositionOf(kRegistry, "Add(int32 id) {"));
		TEST_ASSERT(add.found);
		TEST_ASSERT(Contains(add.contents, "public bool Ai::ToolRegistry::Add(int32 id)"));

		auto type = Hover::At(session, Testing::Lsp::UriFor("Registry.cf"), Testing::Lsp::PositionOf(kRegistry, "ToolRegistry registry"));
		TEST_ASSERT(type.found);
		TEST_ASSERT(Contains(type.contents, "class Ai::ToolRegistry"));
		TEST_PASSED();
	}

	void LspNavigationTest::TestHoverOnAFieldUsesTheCommentAfterIt()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Registry.cf"), After(kRegistry, "registry.size", 9));
		TEST_ASSERT(hover.found);
		TEST_ASSERT_EQ(hover.contents, std::string("```c4\npublic int32 Ai::ToolRegistry::size\n```\n\nhow many tools it holds"));

		auto local = Hover::At(session, Testing::Lsp::UriFor("Registry.cf"), Testing::Lsp::PositionOf(kRegistry, "found"));
		TEST_ASSERT(local.found);
		TEST_ASSERT(Contains(local.contents, "local `found`"));
		TEST_ASSERT(Contains(local.contents, "int64 found = registry.Find(span, 1)"));
		TEST_PASSED();
	}

	void LspNavigationTest::TestHoverOnAMacroUseShowsItsDefinitionAndExpansion()
	{
		auto session = Testing::Lsp::MakeSession("Macros.cf", kMacros);
		auto hover = Hover::At(session, Testing::Lsp::UriFor("Macros.cf"), Testing::Lsp::PositionOf(kMacros, "DOUBLE(21)"));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Contains(hover.contents, "#define DOUBLE(x) ((x) * 2)"));
		TEST_ASSERT(Contains(hover.contents, "((21) * 2)"));
		TEST_ASSERT(Contains(hover.contents, "Twice its argument."));
		TEST_ASSERT_EQ(hover.range.end.character - hover.range.start.character, static_cast<uint32>(std::string("DOUBLE(21)").size()));
		TEST_PASSED();
	}

	void LspNavigationTest::TestCallHierarchyFindsCallersAndCallees()
	{
		auto session = Testing::Lsp::MakeSession("Calls.cf", kCalls);
		const std::string uri = Testing::Lsp::UriFor("Calls.cf");
		auto items = CallHierarchy::Prepare(session, uri, Testing::Lsp::PositionOf(kCalls, "Middle(int32"));
		TEST_ASSERT_EQ(items.Count(), 1u);
		TEST_ASSERT_EQ(items[0].name, std::string("Middle"));
		TEST_ASSERT_EQ(items[0].kind, SymbolKinds::Function);

		// From a call too: the function it calls.
		auto fromCall = CallHierarchy::Prepare(session, uri, Testing::Lsp::PositionOf(kCalls, "Middle(3)"));
		TEST_ASSERT_EQ(fromCall.Count(), 1u);
		TEST_ASSERT_EQ(fromCall[0].selectionRange.start.line, items[0].selectionRange.start.line);

		auto incoming = CallHierarchy::Incoming(session, items[0]);
		TEST_ASSERT_EQ(incoming.Count(), 2u);
		bool top = false, go = false;
		for (const auto& call : incoming)
		{
			if (call.item.name == "Top")
				top = call.fromRanges.Count() == 1 && call.fromRanges[0].start.line == Testing::Lsp::PositionOf(kCalls, "Middle(3)").line;
			if (call.item.name == "Go")
				go = call.item.kind == SymbolKinds::Method && call.item.detail == "Runner::Go";
		}
		TEST_ASSERT(top);
		TEST_ASSERT(go);

		auto outgoing = CallHierarchy::Outgoing(session, items[0]);
		TEST_ASSERT_EQ(outgoing.Count(), 1u);
		TEST_ASSERT_EQ(outgoing[0].item.name, std::string("Leaf"));
		TEST_ASSERT_EQ(outgoing[0].fromRanges.Count(), 2u); // Leaf(v) and Leaf(v * 2)
		TEST_PASSED();
	}

	void LspNavigationTest::TestCallHierarchyOffAFunctionIsEmpty()
	{
		auto session = Testing::Lsp::MakeSession("Calls.cf", kCalls);
		const std::string uri = Testing::Lsp::UriFor("Calls.cf");
		TEST_ASSERT_EQ(CallHierarchy::Prepare(session, uri, Testing::Lsp::PositionOf(kCalls, "v + 1")).Count(), 0u); // a parameter
		TEST_ASSERT_EQ(CallHierarchy::Prepare(session, uri, Testing::Lsp::PositionOf(kCalls, "Runner")).Count(), 0u); // a type
		HierarchyItem nowhere;
		nowhere.uri = uri;
		nowhere.selectionRange.start.line = 9999;
		TEST_ASSERT_EQ(CallHierarchy::Incoming(session, nowhere).Count(), 0u);
		TEST_ASSERT_EQ(CallHierarchy::Outgoing(session, nowhere).Count(), 0u);
		TEST_PASSED();
	}

	void LspNavigationTest::TestTypeHierarchyFindsSupertypesAndSubtypes()
	{
		auto session = Testing::Lsp::MakeSession("Shapes.cf", kShapes);
		const std::string uri = Testing::Lsp::UriFor("Shapes.cf");
		auto items = TypeHierarchy::Prepare(session, uri, Testing::Lsp::PositionOf(kShapes, "Square :"));
		TEST_ASSERT_EQ(items.Count(), 1u);
		TEST_ASSERT_EQ(items[0].name, std::string("Square"));
		TEST_ASSERT_EQ(items[0].kind, SymbolKinds::Class);

		auto supertypes = TypeHierarchy::Supertypes(session, items[0]);
		TEST_ASSERT_EQ(supertypes.Count(), 1u);
		TEST_ASSERT_EQ(supertypes[0].name, std::string("IShape"));
		TEST_ASSERT_EQ(supertypes[0].kind, SymbolKinds::Interface);

		auto subtypes = TypeHierarchy::Subtypes(session, items[0]);
		TEST_ASSERT_EQ(subtypes.Count(), 1u);
		TEST_ASSERT_EQ(subtypes[0].name, std::string("Tile"));
		TEST_ASSERT_EQ(TypeHierarchy::Subtypes(session, subtypes[0]).Count(), 0u);
		TEST_ASSERT_EQ(TypeHierarchy::Prepare(session, uri, Testing::Lsp::PositionOf(kShapes, "return 4")).Count(), 0u);
		TEST_PASSED();
	}

	void LspNavigationTest::TestExpandMacroExpandsTheUseAtAPosition()
	{
		auto session = Testing::Lsp::MakeSession("Macros.cf", kMacros);
		const std::string uri = Testing::Lsp::UriFor("Macros.cf");
		auto triple = MacroUses::At(session, uri, Testing::Lsp::PositionOf(kMacros, "TRIPLE(2)"));
		TEST_ASSERT(triple.found);
		TEST_ASSERT_EQ(triple.name, std::string("TRIPLE"));
		TEST_ASSERT(Contains(triple.expansion, "((2) * 2)")); // the macro it uses expands too
		TEST_ASSERT(!Contains(triple.expansion, "DOUBLE"));

		auto limit = MacroUses::At(session, uri, After(kMacros, "LIMIT +", 2));
		TEST_ASSERT(limit.found);
		TEST_ASSERT_EQ(limit.expansion, std::string("100"));

		auto sum = MacroUses::At(session, uri, Testing::Lsp::PositionOf(kMacros, "SUM(1, 2)"));
		TEST_ASSERT(sum.found);
		TEST_ASSERT(Contains(sum.expansion, "((1) + (2))")); // a definition continued over two lines

		TEST_ASSERT(!MacroUses::At(session, uri, Testing::Lsp::PositionOf(kMacros, "Twice")).found); // a function
		TEST_ASSERT(!MacroUses::At(session, uri, Testing::Lsp::PositionOf(kMacros, "DOUBLE")).found); // its definition
		TEST_PASSED();
	}

	void LspNavigationTest::TestSymbolRangesCoverDocCommentsAndAnnotations()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		auto symbols = DocumentSymbols::ForDocument(session, Testing::Lsp::UriFor("Registry.cf"));
		const DocumentSymbol* registry = Named(Named(symbols, "Ai")->children, "ToolRegistry");
		const DocumentSymbol* find = Named(registry->children, "Find");
		TEST_ASSERT_EQ(find->range.start.line, Testing::Lsp::PositionOf(kRegistry, "/// Finds").line);
		TEST_ASSERT_EQ(find->range.start.character, 0u);
		TEST_ASSERT_EQ(find->selectionRange.start.line, Testing::Lsp::PositionOf(kRegistry, "Find(const").line);
		const DocumentSymbol* init = Named(registry->children, "Init");
		TEST_ASSERT_EQ(init->range.start.character, Testing::Lsp::PositionOf(kRegistry, "[operator").character); // its annotation
		TEST_PASSED();
	}

	void LspNavigationTest::TestMacrosAreDocumentSymbols()
	{
		auto session = Testing::Lsp::MakeSession("Macros.cf", kMacros);
		auto symbols = DocumentSymbols::ForDocument(session, Testing::Lsp::UriFor("Macros.cf"));
		// BUG-343: an in-memory source is parsed with its macros expanded, so a `#define` line declares nothing.
		uint32 doubles = 0;
		for (const auto& symbol : symbols)
			if (symbol.name == "DOUBLE")
				++doubles;
		TEST_ASSERT_EQ(doubles, 1u);
		const DocumentSymbol* doubled = Named(symbols, "DOUBLE");
		TEST_ASSERT_NOT_NULL(doubled);
		TEST_ASSERT_EQ(doubled->kind, SymbolKinds::Function);
		TEST_ASSERT_EQ(doubled->detail, std::string("macro"));
		TEST_ASSERT_EQ(doubled->range.start.line, 0u); // its comment
		TEST_ASSERT_EQ(doubled->selectionRange.start.line, 1u);
		TEST_ASSERT_EQ(doubled->selectionRange.start.character, 8u);
		const DocumentSymbol* sum = Named(symbols, "SUM");
		TEST_ASSERT_NOT_NULL(sum);
		TEST_ASSERT_EQ(sum->range.end.line, sum->range.start.line + 1); // both lines of the definition
		TEST_ASSERT_NOT_NULL(Named(symbols, "LIMIT"));
		TEST_ASSERT_NOT_NULL(Named(symbols, "Twice"));
		TEST_PASSED();
	}

	void LspNavigationTest::TestReferencesFromADeclarationsOwnName()
	{
		auto session = Testing::Lsp::MakeSession("Calls.cf", kCalls);
		const std::string uri = Testing::Lsp::UriFor("Calls.cf");
		TEST_ASSERT_EQ(References::At(session, uri, Testing::Lsp::PositionOf(kCalls, "Leaf(int32")).Count(), 2u); // Leaf(v), Leaf(v * 2)
		TEST_ASSERT_EQ(References::At(session, uri, Testing::Lsp::PositionOf(kCalls, "Leaf(v)")).Count(), 2u);
		TEST_ASSERT_EQ(References::At(session, uri, After(kCalls, "Leaf(int32", 4)).Count(), 2u); // a caret at the name's end
		TEST_ASSERT_EQ(References::At(session, uri, Testing::Lsp::PositionOf(kCalls, "int32 Leaf")).Count(), 0u); // its return type
		TEST_ASSERT_EQ(DocumentHighlight::At(session, uri, Testing::Lsp::PositionOf(kCalls, "Leaf(int32")).Count(), 3u); // and itself

		auto registry = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		const std::string registryUri = Testing::Lsp::UriFor("Registry.cf");
		// The out-of-line definition's own name is the method: its declaration and definition.
		auto add = References::At(registry, registryUri, After(kRegistry, "ToolRegistry::Add", 14), true);
		TEST_ASSERT_EQ(add.Count(), 2u);
		TEST_PASSED();
	}

	void LspNavigationTest::TestReferencesOfATypeIncludeWhereItIsWritten()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		const std::string uri = Testing::Lsp::UriFor("Registry.cf");
		const auto use = Testing::Lsp::PositionOf(kRegistry, "ToolRegistry registry");
		auto fromClass = References::At(session, uri, Testing::Lsp::PositionOf(kRegistry, "ToolRegistry"));
		TEST_ASSERT_EQ(fromClass.Count(), 1u);
		TEST_ASSERT_EQ(fromClass[0].range.start.line, use.line);
		TEST_ASSERT_EQ(fromClass[0].range.start.character, use.character);
		// From the qualifier of an out-of-line definition: the type it names.
		auto fromQualifier = References::At(session, uri, Testing::Lsp::PositionOf(kRegistry, "ToolRegistry::Add"));
		TEST_ASSERT_EQ(fromQualifier.Count(), 1u);
		TEST_ASSERT_EQ(fromQualifier[0].range.start.line, use.line);
		TEST_PASSED();
	}

	void LspNavigationTest::TestTypeDefinitionOfAVariable()
	{
		auto session = Testing::Lsp::MakeSession("Registry.cf", kRegistry);
		const std::string uri = Testing::Lsp::UriFor("Registry.cf");
		const uint32 classLine = Testing::Lsp::PositionOf(kRegistry, "class ToolRegistry").line;
		auto atDeclaration = TypeDefinition::At(session, uri, Testing::Lsp::PositionOf(kRegistry, "registry;"));
		TEST_ASSERT_EQ(atDeclaration.Count(), 1u);
		TEST_ASSERT_EQ(atDeclaration[0].range.start.line, classLine);
		auto atUse = TypeDefinition::At(session, uri, Testing::Lsp::PositionOf(kRegistry, "registry.Find"));
		TEST_ASSERT_EQ(atUse.Count(), 1u);
		TEST_ASSERT_EQ(atUse[0].range.start.line, classLine);
		auto parameter = TypeDefinition::At(session, uri, Testing::Lsp::PositionOf(kRegistry, "text, int64"));
		TEST_ASSERT_EQ(parameter.Count(), 1u); // a `const Span&` parameter: Span
		TEST_ASSERT_EQ(parameter[0].range.start.line, Testing::Lsp::PositionOf(kRegistry, "struct Span").line);
		TEST_ASSERT_EQ(TypeDefinition::At(session, uri, Testing::Lsp::PositionOf(kRegistry, "found")).Count(), 0u); // an int64
		TEST_PASSED();
	}

	void LspNavigationTest::TestImplementationOfAMethodIsWhatOverridesIt()
	{
		auto session = Testing::Lsp::MakeSession("Shapes.cf", kShapes);
		const std::string uri = Testing::Lsp::UriFor("Shapes.cf");
		auto overrides = Implementation::At(session, uri, Testing::Lsp::PositionOf(kShapes, "Area() = 0"));
		TEST_ASSERT_EQ(overrides.Count(), 1u);
		TEST_ASSERT_EQ(overrides[0].range.start.line, Testing::Lsp::PositionOf(kShapes, "Area() { return 4").line);
		TEST_ASSERT_EQ(Implementation::At(session, uri, Testing::Lsp::PositionOf(kShapes, "Area() { return 4")).Count(), 0u); // Tile does not override it
		TEST_ASSERT_EQ(Implementation::At(session, uri, Testing::Lsp::PositionOf(kShapes, "IShape")).Count(), 2u); // the type: Square and Tile
		TEST_PASSED();
	}

	void LspNavigationTest::TestWorkspaceSymbolsIncludeMacros()
	{
		auto session = Testing::Lsp::MakeSession("Macros.cf", kMacros);
		const auto doubles = WorkspaceSymbols::ForQuery(session, "doub");
		const WorkspaceSymbol* doubled = Found(doubles, "DOUBLE", "");
		TEST_ASSERT_NOT_NULL(doubled);
		TEST_ASSERT_EQ(doubled->kind, SymbolKinds::Function);
		TEST_ASSERT_EQ(doubled->location.uri, Testing::Lsp::UriFor("Macros.cf"));
		TEST_ASSERT_EQ(doubled->location.range.start.line, 1u);
		TEST_ASSERT_EQ(doubled->location.range.start.character, 8u);
		const auto sums = WorkspaceSymbols::ForQuery(session, "SUM");
		TEST_ASSERT_NOT_NULL(Found(sums, "SUM", ""));
		TEST_PASSED();
	}

	void LspNavigationTest::TestMembersReachedThroughPointersResolve()
	{
		auto session = Testing::Lsp::MakeSession("Pointers.cf", kPointers);
		const std::string uri = Testing::Lsp::UriFor("Pointers.cf");
		auto hover = Hover::At(session, uri, Testing::Lsp::PositionOf(kPointers, "ApplyEdit(2)"));
		TEST_ASSERT(hover.found);
		TEST_ASSERT(Contains(hover.contents, "public int32 Ied::Documents::ApplyEdit(int32 doc)"));
		auto member = Hover::At(session, uri, Testing::Lsp::PositionOf(kPointers, "documents->ApplyEdit(3)"));
		TEST_ASSERT(Contains(member.contents, "public Documents* Ied::Server::documents"));

		TEST_ASSERT_EQ(References::At(session, uri, Testing::Lsp::PositionOf(kPointers, "ApplyEdit(int32")).Count(), 4u);
		auto items = CallHierarchy::Prepare(session, uri, Testing::Lsp::PositionOf(kPointers, "ApplyEdit(int32"));
		TEST_ASSERT_EQ(items.Count(), 1u);
		auto callers = CallHierarchy::Incoming(session, items[0]);
		TEST_ASSERT_EQ(callers.Count(), 4u); // OnDiskWrite, ServerEdit, WriteThrough, Direct
		bool hooks = false, through = false;
		for (const auto& call : callers)
		{
			hooks = hooks || call.item.detail == "Ied::Hooks::ServerEdit";
			through = through || call.item.detail == "IED::WriteThrough";
		}
		TEST_ASSERT(hooks);
		TEST_ASSERT(through);
		TEST_PASSED();
	}
}
