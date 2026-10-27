// A reply caught in a loop (docs/12 "Taxonomy", `repetition`): a local model can write one passage over and over until
// the step cap - thinking, prose or a tool call's arguments - so the tail of what streams is checked as it grows.

include "bedrock.ch"

namespace Ai
{
	const int64 c_RepetitionCheckBytes = 1024;      // a stream's text is checked again after this much more
	const int64 c_RepetitionTailBytes = 4096;       // thinking or prose ending in this much of one repeated passage loops
	const int64 c_RepetitionArgumentBytes = 8192;   // a tool call's arguments: files may repeat lines on purpose
	const int64 c_RepetitionMostPeriod = 1024;      // longer passages are not looked for

	// The length of the passage that `text`'s last `tailBytes` repeat, again and again, or 0 when they do not.
	int64 RepeatedTailPeriod(Core::StringView text, int64 tailBytes);
}
