#include "Lexer/BraceRecovery.h"
#include <algorithm>

namespace C4Compiler
{
	namespace
	{
		struct Balance
		{
			uint32 missing = 0; // `}`s too few
			uint32 extra = 0;   // `}`s too many
		};

		Balance Count(const std::vector<BraceFact>& facts)
		{
			uint32 opens = 0, closes = 0;
			for (const BraceFact& fact : facts)
			{
				if (fact.type == TokenType::OpenCurl) ++opens;
				else if (fact.type == TokenType::CloseCurl) ++closes;
			}
			return { opens > closes ? opens - closes : 0, closes > opens ? closes - opens : 0 };
		}

		// The first token after `open` and before `limit` that starts a line indented no deeper than the `{`'s own.
		uint32 FirstOutdented(const std::vector<BraceFact>& facts, uint32 open, uint32 limit)
		{
			for (uint32 at = open + 1; at < limit; ++at)
				if (facts[at].startsLine && facts[at].indent <= facts[open].indent) return at;
			return limit;
		}

		class Planner
		{
		public:
			Planner(const std::vector<BraceFact>& facts) : m_Facts(facts), m_Balance(Count(facts)) {}

			BraceRepairs Plan()
			{
				if (m_Balance.missing == 0 && m_Balance.extra == 0) return {};
				for (uint32 at = 0; at < m_Facts.size(); ++at)
				{
					if (m_Facts[at].type == TokenType::OpenCurl) m_Open.push_back(at);
					else if (m_Facts[at].type == TokenType::CloseCurl) Close(at);
				}
				const uint32 end = static_cast<uint32>(m_Facts.size()) - 1; // the end of the file
				while (m_Balance.missing > 0 && !m_Open.empty()) CloseInnermostBefore(end);
				std::sort(m_Repairs.closings.begin(), m_Repairs.closings.end(),
					[](const BraceRepairs::Closing& a, const BraceRepairs::Closing& b) { return a.before < b.before || (a.before == b.before && a.open > b.open); });
				return std::move(m_Repairs);
			}

		private:
			void Close(uint32 at)
			{
				const BraceFact& closer = m_Facts[at];
				if (m_Balance.extra > 0 && !m_Open.empty() && closer.startsLine && closer.indent > m_Facts[m_Open.back()].indent)
				{
					m_Repairs.dropped.push_back(at);
					--m_Balance.extra;
					return;
				}
				while (m_Balance.missing > 0 && !m_Open.empty() && closer.startsLine && m_Facts[m_Open.back()].indent > closer.indent)
					CloseInnermostBefore(at);
				if (!m_Open.empty()) m_Open.pop_back();
			}

			void CloseInnermostBefore(uint32 limit)
			{
				const uint32 open = m_Open.back();
				m_Open.pop_back();
				m_Repairs.closings.push_back({ open, FirstOutdented(m_Facts, open, limit) });
				--m_Balance.missing;
			}

			const std::vector<BraceFact>& m_Facts;
			Balance m_Balance;
			std::vector<uint32> m_Open; // the `{`s not closed yet, innermost last
			BraceRepairs m_Repairs;
		};
	}

	BraceRepairs PlanBraceRepairs(const std::vector<BraceFact>& facts)
	{
		if (facts.empty()) return {};
		return Planner(facts).Plan();
	}
}
