#pragma once
#include "Compiler/ErrorTracker.h"
#include "Core/Core.h"

namespace Scripting
{
	/// A preprocessor error or warning at a 1-based line and column of the file as written.
	struct MacroDiagnostic
	{
		uint32 line = 0;
		uint32 column = 1;
		ErrorSeverity severity = ErrorSeverity::Error;
		Core::String message;
	};
}
