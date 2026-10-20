#pragma once
#include <string>
#include <vector>

namespace Scripting
{
	/// The open #if groups of a file: whether the current line is live, and the error of a misplaced #elif,
	/// #else or #endif. A condition is read only where it can matter: inside a skipped region, or after a
	/// branch was taken, it is never evaluated (C), so it cannot report errors there.
	class ConditionalStack
	{
	public:
		bool Active() const { return m_Groups.empty() || m_Groups.back().live; }

		/// A #if / #ifdef / #ifndef on `line` opens a group; its condition counts only when the region is live.
		void Open(bool condition, unsigned line);
		/// A #elif's condition counts only when the group is live above and no branch was taken.
		bool ElifNeedsCondition() const;
		/// Each returns an error when the directive has no open group to belong to, else "".
		std::string Elif(bool condition);
		std::string Else();
		std::string Endif();

		/// The lines of the #if groups still open (no #endif) at the end of the file.
		std::vector<unsigned> OpenLines() const;

	private:
		struct Group
		{
			bool parentLive = true;
			bool live = true;
			bool taken = false; // a branch of the group was live, so every later branch is dead
			bool sawElse = false;
			unsigned line = 0;
		};

		std::vector<Group> m_Groups;
	};
}
