// A page's links made absolute (docs/29 "Conversion"), so the model can fetch what a page points at.

include "bedrock.ch"

namespace WebFetch
{
	// `href` resolved against `base` (an absolute URL): "" for what is no link to follow (javascript:, data:, nothing).
	Core::String ResolveUrl(Core::StringView base, Core::StringView href);
	// The URL's scheme in lowercase ("https"); "" when it has none.
	Core::String SchemeOf(Core::StringView url);
}
