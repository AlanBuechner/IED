#include "ErrorTracker.h"

namespace Scripting
{
	void ErrorTracker::AddError(const SourceLocation& loc, const Core::String& message, ErrorSeverity severity)
	{
		// A diagnostic with no message is never useful (it renders as a bare "error:" line and adds
		// nothing the user can act on). These arise from speculative-parse commit paths that buffer a
		// placeholder; drop them so output stays clear and helpful. Real errors at the same location
		// are reported separately, so nothing actionable is lost.
		if (message.empty())
			return;

		CompilationError error;
		error.location = loc;
		error.message = message;
		error.severity = severity;

		// Suppressed errors go to the innermost suppression frame instead of being discarded, so a committed
		// speculative parse can still surface real diagnostics.
		Core::Vector<CompilationError>& target = AreErrorsSuppressed() ? m_SuppressFrames.Back().buffered : m_Errors;
		if (m_OncePerLocation && FollowsFrom(target, error)) return;
		target.Push(error);
	}

	bool ErrorTracker::FollowsFrom(const Core::Vector<CompilationError>& reported, const CompilationError& error)
	{
		if (reported.Empty() || error.severity == ErrorSeverity::Warning) return false;
		const CompilationError& last = reported.Back();
		return last.severity != ErrorSeverity::Warning && last.location == error.location;
	}

	ErrorTracker::Mark ErrorTracker::Now() const
	{
		return { m_SuppressFrames.Count(), Target().Count() };
	}

	bool ErrorTracker::ReportedSince(const Mark& mark) const
	{
		if (m_SuppressFrames.Count() != mark.depth) return true; // a speculation still open: what it holds is not settled
		return Target().Count() > mark.count;
	}

	bool ErrorTracker::LastErrorOnLineOf(const SourceLocation& at) const
	{
		const Core::Vector<CompilationError>& kept = Target();
		if (kept.Empty()) return false;
		const CompilationError& last = kept.Back();
		return last.severity != ErrorSeverity::Warning && last.location.sourceId == at.sourceId && last.location.line == at.line;
	}

	void ErrorTracker::SuppressErrors()
	{
		m_SuppressFrames.Push(SuppressionFrame());
	}

	void ErrorTracker::CommitSuppressedErrors()
	{
		if (!AreErrorsSuppressed())
			return;

		SuppressionFrame frame = m_SuppressFrames.Pop();
		if (AreErrorsSuppressed())
			m_SuppressFrames.Back().buffered.Append(frame.buffered);
		else
			m_Errors.Append(frame.buffered);
	}

	void ErrorTracker::DiscardSuppressedErrors()
	{
		if (AreErrorsSuppressed())
			m_SuppressFrames.Pop();
	}

	void ErrorTracker::Clear()
	{
		m_Errors.Clear();
		m_SuppressFrames.Clear();
	}
}
