#include "Backend/HIR/JumpScopes.h"

namespace Scripting
{
	void JumpScopes::EnterBlock(const BlockNode* block)
	{
		m_Path.push_back({ block, 0 });
	}

	void JumpScopes::LeaveBlock()
	{
		if (!m_Path.empty()) m_Path.pop_back();
	}

	void JumpScopes::NoteDeclaration(const Core::String& description)
	{
		if (m_Path.empty()) return;
		m_Declarations[m_Path.back().block].push_back(description);
		m_Path.back().declarations++;
	}

	QueryResult<void> JumpScopes::NoteLabel(uint32 labelId, const Core::String& name, const SourceLocation& loc)
	{
		if (m_Labels.count(labelId))
			return QueryResult<void>::Fail(QueryError::NotAValue, "label '" + name + "' is already defined in this function", loc);
		m_Labels[labelId] = { m_Path, labelId, name, loc };
		return QueryResult<void>::Success();
	}

	void JumpScopes::NoteGoto(uint32 labelId, const Core::String& name, const SourceLocation& loc)
	{
		m_Gotos.push_back({ m_Path, labelId, name, loc });
	}

	QueryResult<void> JumpScopes::Validate() const
	{
		for (const Point& jump : m_Gotos)
		{
			auto it = m_Labels.find(jump.labelId);
			if (it == m_Labels.end())
				return QueryResult<void>::Fail(QueryError::NotFound, "goto to undefined label '" + jump.name + "'", jump.loc);
			if (auto r = CheckGoto(jump, it->second); !r.Ok()) return r;
		}
		return QueryResult<void>::Success();
	}

	QueryResult<void> JumpScopes::CheckGoto(const Point& jump, const Point& label) const
	{
		if (label.path.empty()) return QueryResult<void>::Success();

		// The deepest block both are in: a declaration there before the label and after the goto is crossed.
		size_t shared = 0;
		while (shared < label.path.size() && shared < jump.path.size() && label.path[shared].block == jump.path[shared].block) shared++;
		if (shared > 0)
		{
			const size_t level = shared - 1;
			const uint32 atLabel = label.path[level].declarations;
			const uint32 atJump = jump.path[level].declarations;
			if (atLabel > atJump) return Crossed(jump, label.path[level].block, atJump);
		}

		// The blocks the jump enters: as in C++, entering one is fine while no declaration before the label is skipped.
		for (size_t level = shared; level < label.path.size(); ++level)
			if (label.path[level].declarations > 0) return Crossed(jump, label.path[level].block, 0);
		return QueryResult<void>::Success();
	}

	QueryResult<void> JumpScopes::Crossed(const Point& jump, const BlockNode* block, uint32 firstCrossed) const
	{
		auto names = m_Declarations.find(block);
		Core::String crossed = (names != m_Declarations.end() && firstCrossed < names->second.size()) ? names->second[firstCrossed] : "a declaration";
		return QueryResult<void>::Fail(QueryError::NotAValue, "jump to label '" + jump.name + "' crosses " + crossed, jump.loc);
	}
} // namespace Scripting
