// terminal.text (docs/04 "Attachments and @-mentions"): a terminal's last lines as plain text, what a chat reads for an
// `@terminal:` mention. Another plugin calls it through the binding; its terminals are this plugin's.

include "terminal.ch"

namespace Terminal
{
	const int64 kTextDefaultLines = 200;
	const int64 kTextMostLines = 2000;

	// `text` without terminal escape sequences (CSI, OSC and the other ESC ones), carriage returns or bells.
	Core::String PlainText(Core::StringView text);
	// The last `most` lines of `text`, without a final empty one.
	Core::String LastLines(Core::StringView text, int64 most);
	// {terminal, lines?} -> {terminal, title, alive, text}: the terminal's last lines (200 by default); refused for a
	// terminal there is not.
	IED::Result TerminalText(Terminals* terminals, const Serialization::DomNode& args);
}
