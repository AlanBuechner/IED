// Error hints (docs/13 "Error hints"): a language plugin's advice for the compiler errors a model makes in its language
// - a C++ habit in C4, a library name another language spells differently - shown under the error where the model
// meets it (build_run, problems_list, shell_run's output) instead of up front in every system message.

include "bedrock.ch"

namespace Ai
{
	const int64 kHintsMostPerResult = 6;

	class ErrorHint
	{
		public Core::Regex pattern;   // searched for in one line of a tool's output
		public Core::String text;
	}

	// `text` with, under each line a hint's pattern finds, the hint - each hint once, at most kHintsMostPerResult.
	Core::String WithHints(Core::StringView text, const Core::Array<ErrorHint*>& hints);
}
