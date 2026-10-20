#pragma once
#include "Compiler/ErrorTracker.h"
#include "Core/Vector.h"

namespace Scripting
{
	/// Aggregates everything a compilation run produced: every diagnostic from every phase
	/// (front end, backend, exceptions) plus the outcome of the code generation step.
	///
	/// Front-end diagnostics carry a source location; backend/codegen errors do not (their
	/// `location` is zeroed). The reporter treats a zeroed location as "no source context".
	struct CompilationResult
	{
		Core::Vector<CompilationError> errors;
		bool codegenSuccess = false;
		bool timePhases = false; // -ftime: footer prints a per-phase timing breakdown

		/// True when any collected diagnostic is not a warning. Code generation and HIR
		/// lowering are gated on this: lowering a partially built AST can dereference broken
		/// nodes, so the pipeline stops after the front end whenever hard errors exist.
		bool HasErrors() const;

		/// Add a backend/codegen error (no source location) to the result set.
		void AddBackendError(const Core::String& message);
	};
}
