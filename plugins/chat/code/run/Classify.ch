// What a step's reply was (docs/12 "Taxonomy"): tool calls, an answer, or one of the reasons a run stops - each
// recoverable by its own policy. The run decides what an answer means in its mode.

include "Step.ch"

namespace Ai
{
	enum class StepVerdict
	{
		ToolCalls, Answer, Cancelled,
		ConnectFailed, HttpError, ContextOverflow, Disconnected, StalledPrefill, StalledStream, StepCap,
		MaxTokens, Empty, ReasoningOnly, AnnouncedNoAction, UnparsedToolCall, Repetition
	}

	StepVerdict Classify(const StepResult& step);
	// The stop reason a verdict is shown and recorded as ("max_tokens", "announced_no_action").
	const char* VerdictName(StepVerdict verdict);
	// Whether `text` ends by announcing an action it did not take ("Now I'll edit src/a.c:"); `action` gets the action.
	bool AnnouncesAction(Core::StringView text, Core::String& action);
	// Whether a server's error is about the prompt being too long for the context.
	bool IsContextError(Core::StringView error);
	// The context size a context-length error names; 0 when it names none.
	int64 ContextLimitIn(Core::StringView error);
}
