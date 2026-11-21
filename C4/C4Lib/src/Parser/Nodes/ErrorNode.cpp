#include "ErrorNode.h"

namespace Scripting
{
	ErrorNode::ErrorNode(const SourceLocation& start, const SourceLocation& end, Core::String why) : m_Why(std::move(why))
	{
		startLocation = start;
		endLocation = end;
	}

	Core::String ErrorNode::ToString() const
	{
		return "<error: " + m_Why + ">";
	}
}
