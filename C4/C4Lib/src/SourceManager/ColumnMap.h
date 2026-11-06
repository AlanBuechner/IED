#pragma once
#include "Core/Core.h"
#include <map>
#include <vector>

namespace Scripting
{
	/// One macro use on a line: the bytes [writtenStart, writtenEnd) of the line as written became [expandedStart,
	/// expandedEnd) of the line the compiler reads. Columns from 0.
	struct ExpandedSpan
	{
		uint32 writtenStart = 0;
		uint32 writtenEnd = 0;
		uint32 expandedStart = 0;
		uint32 expandedEnd = 0;
	};

	/// The columns a file's lines have as written, for the lines its macro uses changed (BUG-446): the compiler reads the
	/// expanded text, and reports and answers in the file's own columns. Lines keep their numbers through the macro pass.
	class ColumnMap
	{
	public:
		/// `spans`, in order along `line` (from 1).
		void Add(uint32 line, std::vector<ExpandedSpan> spans);
		bool Empty() const { return m_Lines.empty(); }

		/// The column as written (from 1) of `column` of the expanded `line`; one inside an expansion is its use's.
		uint32 Written(uint32 line, uint32 column) const;
		/// The expanded column (from 1) of `column` as written; one inside a use is its expansion's first.
		uint32 Expanded(uint32 line, uint32 column) const;

	private:
		std::map<uint32, std::vector<ExpandedSpan>> m_Lines;
	};
}
