#pragma once
#include "Macro/MacroPreprocessor.h"
#include "Macro/ConditionalStack.h"

namespace Scripting
{
	/// One file's preprocessing (MacroPreprocessor::Process): the reading position, the open #if groups,
	/// whether a block comment is open, and what has been produced. Lines: MacroPreprocessor.cpp;
	/// directives: MacroDirectives.cpp.
	class Preprocessor
	{
	public:
		Preprocessor(const std::string& text, const std::string& fileName, MacroTable& table,
			const MacroPreprocessor::InheritCallback& inheritMacros);

		PreprocessedFile Run();

	private:
		// --- Lines ---
		bool AtEnd() const { return m_Position >= m_Text.size(); }
		std::string PeekPhysicalLine() const;
		std::string NextPhysicalLine();
		std::string JoinContinuations(std::string line);
		void CodeLine(const std::string& line, unsigned number);
		size_t JoinOpenCall(std::string& code);
		void InheritInclude(const std::string& code);
		void TrackComments(const std::string& line);
		void Report(unsigned line, ErrorSeverity severity, const std::string& message);

		// --- Directives ---
		void Directive(const std::string& line, unsigned number);
		std::string DirectiveText(const std::string& line);
		bool ConditionalDirective(const std::string& name, const std::string& rest, unsigned number);
		bool Condition(const std::string& expression, unsigned number);
		std::string MacroName(const std::string& directive, const std::string& rest, unsigned number);
		void Define(const std::string& rest, unsigned number);
		void Undef(const std::string& rest, unsigned number);

		const std::string& m_Text;
		size_t m_Position = 0;
		unsigned m_Line = 1; // the number of the next physical line
		std::string m_FileName;
		MacroTable& m_Table;
		const MacroPreprocessor::InheritCallback& m_Inherit;
		ConditionalStack m_Conditions;
		bool m_InComment = false; // a /* comment is open at the start of the next line
		std::string m_Out;
		std::vector<MacroDiagnostic> m_Diagnostics;
	};
}
