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
		size_t level = label.path.size() - 1;
		if (level >= jump.path.size() || jump.path[level].block != label.path[level].block)
			return QueryResult<void>::Fail(QueryError::NotAValue,
				"goto '" + jump.name + "' jumps into a nested block; a label must be in the goto's block or an enclosing one", jump.loc);

		uint32 atLabel = label.path[level].declarations;
		uint32 atJump = jump.path[level].declarations;
		if (atLabel <= atJump) return QueryResult<void>::Success();

		auto names = m_Declarations.find(label.path[level].block);
		Core::String crossed = (names != m_Declarations.end() && atJump < names->second.size()) ? names->second[atJump] : "a declaration";
		return QueryResult<void>::Fail(QueryError::NotAValue, "jump to label '" + jump.name + "' crosses " + crossed, jump.loc);
	}
} // namespace Scripting
