#pragma once
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{

	enum class ErrorSeverity { Warning, Error, Fatal };

	struct CompilationError
	{
		SourceLocation location;
		ErrorSeverity severity = ErrorSeverity::Error;
		Core::String message;
	};

	class ErrorTracker
	{
	public:
		void AddError(const SourceLocation& loc, const Core::String& message, ErrorSeverity severity = ErrorSeverity::Error);
		const Core::Vector<CompilationError>& GetErrors() const { return m_Errors; }
		/// An error or fatal error was reported; warnings alone are not failures.
		bool HasErrors() const
		{
			for (const CompilationError& e : m_Errors)
				if (e.severity != ErrorSeverity::Warning) return true;
			return false;
		}
		Core::Vector<CompilationError>& GetErrorsMutable() { return m_Errors; }
		void Clear();

		// ------------------------------------------------------------------
		// Suppression for speculative parsing.
		//
		// Errors reported while suppressed are NOT discarded - they are buffered
		// in a per-level frame so that a *committed* (successful) speculative parse
		// can surface its real diagnostics, while a *rolled-back* (failed) attempt
		// throws them away. This is what lets the parser stay silent during a probe
		// yet still report genuine errors inside a class/function body it kept.
		// ------------------------------------------------------------------

		/// Begin buffering suppressed errors. Stackable for nested speculative parses.
		void SuppressErrors();

		/// A speculative parse SUCCEEDED and was committed: flush this level's buffered
		/// errors to the parent level (or to the real error list if this was the
		/// outermost suppression frame).
		void CommitSuppressedErrors();

		/// A speculative parse FAILED and was rolled back: discard this level's buffered
		/// errors so transient probe diagnostics do not leak.
		void DiscardSuppressedErrors();

		bool AreErrorsSuppressed() const { return !m_SuppressFrames.Empty(); }

		/// True when the innermost speculative parse has recorded an error: what it tried did not parse.
		bool SpeculationFailed() const { return !m_SuppressFrames.Empty() && !m_SuppressFrames.Back().buffered.Empty(); }

	private:
		struct SuppressionFrame
		{
			Core::Vector<CompilationError> buffered;
		};

		Core::Vector<CompilationError> m_Errors;
		Core::Vector<SuppressionFrame> m_SuppressFrames; // one frame per active suppression level
	};
}
