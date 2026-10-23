include "spans.ch"

namespace Spans
{
	// Runs one command line against `set`, appending its answer (with a newline) to `out`. False for a bad line.
	bool RunCommand(SpanSet& set, Core::StringView line, Core::StringBuilder& out);
}
