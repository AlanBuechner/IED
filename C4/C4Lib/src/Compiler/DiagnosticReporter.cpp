#include "DiagnosticReporter.h"
#include "Compiler/CompilerInstance.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdio>

namespace Scripting
{
	bool CompilationResult::HasErrors() const
	{
		for (const auto& err : errors)
			if (err.severity != ErrorSeverity::Warning)
				return true;
		return false;
	}

	void CompilationResult::AddBackendError(const Core::String& message)
	{
		CompilationError err;
		err.location = {}; // zeroed out = no source location
		err.severity = ErrorSeverity::Error;
		err.message = message;
		errors.Push(err);
	}

	static const char* SeverityName(ErrorSeverity severity)
	{
		switch (severity)
		{
		case ErrorSeverity::Warning: return "warning";
		case ErrorSeverity::Error:   return "error";
		case ErrorSeverity::Fatal:   return "fatal error";
		}
		return "error";
	}

	// Distinct source files carrying a hard diagnostic in the final result. Derived from
	// result.errors - not the raw per-unit trackers - so it always agrees with the
	// Compilation Succeeded/Failed banner; locationless backend errors are not attributed to a file.
	static uint32 CountFilesWithErrors(const CompilationResult& result)
	{
		uint32 count = 0;
		for (uint32 i = 0; i < result.errors.Count(); ++i)
		{
			const auto& err = result.errors[i];
			if (err.severity == ErrorSeverity::Warning)
				continue;
			if (err.location.sourceId == 0 || err.location.line == 0)
				continue; // backend error with no file
			bool seen = false;
			for (uint32 j = 0; j < i; ++j)
				if (result.errors[j].location.sourceId == err.location.sourceId)
					seen = true;
			if (!seen)
				++count;
		}
		return count;
	}

	void DiagnosticReporter::Report(const CompilationResult& result, const CompilerInstance* compiler)
	{
		for (const auto& err : result.errors)
			PrintOneError(err, compiler);

		PrintSummaryFooter(result, compiler);
	}

	void DiagnosticReporter::PrintOneError(const CompilationError& err, const CompilerInstance* compiler)
	{
		std::cout << Format(err, compiler);
	}

	std::string DiagnosticReporter::Format(const CompilationError& err, const CompilerInstance* compiler)
	{
		std::ostringstream out;
		if (err.location.sourceId == 0 || err.location.line == 0)
		{
			out << SeverityName(err.severity) << ": " << err.message << "\n\n"; // a backend error has no location
			return out.str();
		}
		const bool known = compiler && compiler->GetSourceManager().HasInfo(err.location.sourceId);
		const std::string filePath = known ? compiler->GetSourceManager().GetInfo(err.location.sourceId).displayName : "unknown";
		out << filePath << " (" << err.location.line << ":" << err.location.column << ") " << SeverityName(err.severity) << ": " << err.message << "\n";
		const std::string lineText = known ? std::string(compiler->GetSourceManager().GetLineText(err.location.sourceId, err.location.line).c_str()) : "";
		if (!lineText.empty())
		{
			std::string caretLine("  ");
			const int col = static_cast<int>(err.location.column);
			for (int i = 0; i < col - 1 && i < static_cast<int>(lineText.size()); ++i)
				caretLine += (lineText[static_cast<size_t>(i)] == '\t') ? '\t' : ' ';
			out << "  " << lineText << "\n" << caretLine << "^\n";
		}
		out << "\n";
		return out.str();
	}

	void DiagnosticReporter::PrintSummaryFooter(const CompilationResult& result, const CompilerInstance* compiler)
	{
		uint32 warningCount = 0;
		uint32 errorCount = 0;
		for (const auto& err : result.errors)
			(err.severity == ErrorSeverity::Warning ? ++warningCount : ++errorCount);

		std::ostringstream summary;
		summary << "========================================" << std::endl
			<< (result.HasErrors() ? "Compilation Failed" : "Compilation Succeeded") << std::endl
			<< "========================================" << std::endl;

		if (compiler)
		{
			const double totalTime = compiler->GetPhaseTotalSeconds();
			summary << "Total compile time: " << std::fixed << std::setprecision(2)
				<< totalTime << "s" << std::endl
				<< "Files parsed:        " << compiler->GetFilesCompleted() << std::endl;

			const uint32 filesWithErrors = CountFilesWithErrors(result);
			if (filesWithErrors > 0)
				summary << "Files with errors:   " << filesWithErrors << std::endl;

			if (result.timePhases && totalTime > 0.0)
			{
				summary << "Phase timing:" << std::endl;
				for (const auto& phase : compiler->GetPhaseTimes())
				{
					double pct = phase.second / totalTime * 100.0;
					char line[96];
					std::snprintf(line, sizeof(line), "  %-14s %8.3f s  (%5.1f%%)",
						phase.first ? phase.first : "?", phase.second, pct);
					summary << line << std::endl;
				}
			}
		}

		if (warningCount > 0)
			summary << "Warnings:            " << warningCount << std::endl;
		if (errorCount > 0)
			summary << "Errors:              " << errorCount << std::endl;
		summary << "========================================" << std::endl;
		std::cout << summary.str();
	}
}
