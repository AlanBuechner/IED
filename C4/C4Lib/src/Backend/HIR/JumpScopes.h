#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/QueryResult.h"
#include <unordered_map>
#include <vector>

namespace Scripting
{
	/// The goto rules (docs/language/09-statements-and-control-flow.md section 5): a goto may target a label
	/// in its own block or an enclosing one, never a nested block, and may not jump forward past a declaration
	/// or a defer of the label's block. Lowering reports the block path as it walks a body; Validate checks
	/// every goto against its label once the whole body is known.
	class JumpScopes
	{
	public:
		void EnterBlock(const BlockNode* block);
		void LeaveBlock();
		void NoteDeclaration(const Core::String& description); // "the declaration of 'x'", "a defer"
		QueryResult<void> NoteLabel(uint32 labelId, const Core::String& name, const SourceLocation& loc);
		void NoteGoto(uint32 labelId, const Core::String& name, const SourceLocation& loc);
		QueryResult<void> Validate() const;

	private:
		struct Level { const BlockNode* block = nullptr; uint32 declarations = 0; };
		struct Point { std::vector<Level> path; uint32 labelId = 0; Core::String name; SourceLocation loc; };

		QueryResult<void> CheckGoto(const Point& jump, const Point& label) const;

		std::vector<Level> m_Path;
		std::unordered_map<const BlockNode*, std::vector<Core::String>> m_Declarations;
		std::unordered_map<uint32, Point> m_Labels;
		std::vector<Point> m_Gotos;
	};
} // namespace Scripting
