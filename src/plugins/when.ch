// When-clauses on the server (docs/15 "Commands and keys"; the same grammar and meaning as frontend/src/shell/when.js):
// `!`, `&&`, `||`, parentheses, `==`, `!=`, `<`, `<=`, `>`, `>=`, `=~ /re/flags` (flags i, m, s), quoted strings,
// numbers, true/false; a bare key is true when its value is truthy. The server evaluates the `when` of contributions
// it acts on itself, such as language server profiles (docs/24).
//     WhenLookup lookup = [](Core::StringView key) -> WhenValue { ... };
//     if (WhenHolds(Core::StringView("workspaceTrusted && config.c4.languageServer"), lookup, error)) ...

include "bedrock.ch"

namespace Ied
{
	enum class WhenKind { None, Bool, Number, Text }

	// A context value: absent, a boolean, a number or text.
	struct WhenValue
	{
		public WhenKind kind;
		public bool flag;
		public double number;
		public Core::String text;
	}

	WhenValue WhenNone();
	WhenValue WhenBool(bool value);
	WhenValue WhenNumber(double value);
	WhenValue WhenText(Core::StringView value);
	// A value from JSON text (a setting's effective value): null, a boolean, a number or a string; anything else none.
	WhenValue WhenFromJson(Core::StringView json);

	alias WhenLookup = func<WhenValue(Core::StringView)>;

	// Whether `clause` holds under `lookup`; an empty clause does. False with `error` when it does not parse.
	bool WhenHolds(Core::StringView clause, const WhenLookup& lookup, Core::String& error);
}
