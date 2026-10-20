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

		if (AreErrorsSuppressed())
		{
			// Buffer into the innermost suppression frame instead of discarding, so a
			// committed speculative parse can still surface real diagnostics.
			m_SuppressFrames.Back().buffered.Push(error);
			return;
		}

		m_Errors.Push(error);
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
