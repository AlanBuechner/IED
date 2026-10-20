#include "Macro/ConditionalStack.h"

namespace Scripting
{
	void ConditionalStack::Open(bool condition, unsigned line)
	{
		Group group;
		group.parentLive = Active();
		group.live = group.parentLive && condition;
		group.taken = group.live || !group.parentLive;
		group.line = line;
		m_Groups.push_back(group);
	}

	bool ConditionalStack::ElifNeedsCondition() const
	{
		return !m_Groups.empty() && m_Groups.back().parentLive && !m_Groups.back().taken && !m_Groups.back().sawElse;
	}

	std::string ConditionalStack::Elif(bool condition)
	{
		if (m_Groups.empty()) return "#elif with no #if before it";
		Group& group = m_Groups.back();
		if (group.sawElse) return "#elif after the #else of the #if on line " + std::to_string(group.line);
		group.live = group.parentLive && !group.taken && condition;
		group.taken = group.taken || group.live;
		return "";
	}

	std::string ConditionalStack::Else()
	{
		if (m_Groups.empty()) return "#else with no #if before it";
		Group& group = m_Groups.back();
		if (group.sawElse) return "a second #else for the #if on line " + std::to_string(group.line);
		group.live = group.parentLive && !group.taken;
		group.taken = true;
		group.sawElse = true;
		return "";
	}

	std::string ConditionalStack::Endif()
	{
		if (m_Groups.empty()) return "#endif with no #if before it";
		m_Groups.pop_back();
		return "";
	}

	std::vector<unsigned> ConditionalStack::OpenLines() const
	{
		std::vector<unsigned> lines;
		for (const Group& group : m_Groups)
			lines.push_back(group.line);
		return lines;
	}
}
