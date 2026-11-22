#pragma once
#include "Macro/MacroPreprocessor.h"
#include "Macro/ConditionalStack.h"

namespace C4Compiler
{
	/// One file's preprocessing (MacroPreprocessor::Process): the reading position, the open #if groups, whether a block
	/// comment is open, and the layout made so far. Lines and uses: MacroPreprocessor.cpp; directives: MacroDirectives.cpp.
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
		void CodeLine(const std::string& line, unsigned number, size_t begin);
		void InheritInclude(const std::string& code);
		void TrackComments(const std::string& line);
		void Report(unsigned line, unsigned column, ErrorSeverity severity, const std::string& message);
		void Report(unsigned line, ErrorSeverity severity, const std::string& message) { Report(line, 1, severity, message); }

		// --- Uses ---
		// Every use in the code from `at` to the end of its line expanded; a call that goes on to later lines takes them.
		void ExpandUses(size_t at, unsigned number);
		// One past the use whose name is [name, nameEnd): the name, or the call after it; 0 when the name is no use.
		size_t UseEnd(size_t name, size_t nameEnd, size_t lineEnd) const;
		// One past the `)` of the call whose `(` is at `open`; 0 when the file or a directive line comes first.
		size_t CallEnd(size_t open) const;
		bool NamesFunctionLike(const std::string& objectLike) const;
		void Expand(size_t begin, size_t end, unsigned number, size_t lineBegin);
		// The lines up to the one that holds `offset`, read (their comments followed); returns that line's end.
		size_t TakeLinesThrough(size_t offset);
		size_t LineEnd(size_t at) const;

		// --- Directives ---
		void Directive(const std::string& line, unsigned number);
		std::string DirectiveText(const std::string& line);
		bool ConditionalDirective(const std::string& name, const std::string& rest, unsigned number);
		bool Condition(const std::string& expression, unsigned number);
		std::string MacroName(const std::string& directive, const std::string& rest, unsigned number);
		void Define(const std::string& rest, unsigned number);
		void Undef(const std::string& rest, unsigned number);

		const std::string& m_Text;
		size_t m_Position = 0; // where the next physical line starts
		unsigned m_Line = 1;   // the number of the next physical line
		std::string m_FileName;
		MacroTable& m_Table;
		const MacroPreprocessor::InheritCallback& m_Inherit;
		ConditionalStack m_Conditions;
		bool m_InComment = false; // a /* comment is open at the start of the next line
		Ref<MacroLayout> m_Layout;
		std::vector<MacroDiagnostic> m_Diagnostics;
	};
}
