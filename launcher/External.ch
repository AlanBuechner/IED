// The system's default browser.

include "bedrock.ch"

namespace Launch
{
	// http, https and mailto URLs: what may be handed to the system (a file: or other URL could start a program).
	bool IsExternalUrl(Core::StringView url);
	// Opens `url` in the default browser (url.dll's handler keeps the '#' part, which the web client reads); false
	// when it is no external URL or the handler did not start.
	bool OpenExternal(Core::StringView url);
}
