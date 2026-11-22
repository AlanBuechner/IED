#pragma once
#include "Compiler/ErrorTracker.h"
#include "Parser/IncludeAsked.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "SourceManager/SourceManager.h"
#include "Core/Core.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace C4Compiler
{
	/// What parsing a file gave: its tree, the errors its text has and the includes it asked for. A language server
	/// compiles a program again on each edit, every file but the edited one as it was, and a compile given the parses of
	/// the one before takes a file's tree from there when what it read is the same, asking its includes again instead of
	/// parsing it (docs/tools/03 "When a program is analyzed"). A tree is never changed once parsed, so compiles share it.
	struct PriorParse
	{
		Ref<Source> source;                          // what was parsed: the file's text,
		Ref<MacroLayout> macros;                     // what the macro pass made of it (R8)
		std::vector<std::string> bindings;           // and the binding units it included
		Ref<NamespaceNode> root;
		Core::Vector<Ref<AstNode>> originalChildren;
		Core::Vector<CompilationError> errors;       // the lexer's and the parser's: not the answers to its includes
		Core::Vector<IncludeAsked> includes;
	};

	/// The parses of one compile, by source id: a tree's locations name its file by id, so a parse is taken only by the
	/// file registered with the same id that reads the same: its text, its macro layout and its bindings.
	class PriorParses
	{
	public:
		/// The parse of the file `sourceId` when it read what `info` (of `sources`) reads now; null otherwise.
		const PriorParse* Find(uint32 sourceId, const SourceManager& sources) const;
		void Keep(uint32 sourceId, PriorParse parse);
		size_t Count() const { return m_Parses.size(); }

	private:
		std::unordered_map<uint32, PriorParse> m_Parses;
	};
} // namespace C4Compiler
