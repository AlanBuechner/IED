#pragma once
#include "Compiler/ErrorTracker.h"
#include "Parser/IncludeAsked.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "SourceManager/Source.h"
#include "Core/Core.h"
#include <unordered_map>

namespace Scripting
{
	/// What parsing a file gave: its tree, the errors its text has and the includes it asked for. A language server
	/// compiles a program again on each edit, every file but the edited one as it was, and a compile given the parses of
	/// the one before takes a file's tree from there when its text is the same, asking its includes again instead of
	/// parsing it (docs/tools/03 "When a program is analyzed"). A tree is never changed once parsed, so compiles share it.
	struct PriorParse
	{
		Ref<Source> source;                          // what was parsed, as written and as expanded
		Ref<NamespaceNode> root;
		Core::Vector<Ref<AstNode>> originalChildren;
		Core::Vector<CompilationError> errors;       // the lexer's and the parser's: not the answers to its includes
		Core::Vector<IncludeAsked> includes;
	};

	/// The parses of one compile, by source id: a tree's locations name its file by id, so a parse is taken only by the
	/// file registered with the same id and the same text.
	class PriorParses
	{
	public:
		/// The parse of the file `sourceId` when its text was `source`'s; null otherwise.
		const PriorParse* Find(uint32 sourceId, const Source& source) const;
		void Keep(uint32 sourceId, PriorParse parse);
		size_t Count() const { return m_Parses.size(); }

	private:
		std::unordered_map<uint32, PriorParse> m_Parses;
	};
} // namespace Scripting
