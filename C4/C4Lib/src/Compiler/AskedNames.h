#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Scripting
{
	/// A set of names a script compile asks about - its host's table, the types its host registered (docs/runtime/04
	/// section 4.6) - that keeps each question and its answer: what the compiled code assumed of the host, which a cached
	/// script checks against the host it would link to. Questions may come from any thread.
	class AskedNames
	{
	public:
		explicit AskedNames(const std::unordered_set<std::string>& names) : m_Names(&names) {}
		AskedNames(const AskedNames&) = delete;
		AskedNames& operator=(const AskedNames&) = delete;

		/// Whether the set holds `name`, kept as asked.
		bool Has(const std::string& name) const;
		/// Each name asked, once, with its answer, in no order.
		std::vector<std::pair<std::string, bool>> Asked() const;

	private:
		const std::unordered_set<std::string>* m_Names;
		mutable std::mutex m_Lock;
		mutable std::unordered_map<std::string, bool> m_Asked;
	};
} // namespace Scripting
