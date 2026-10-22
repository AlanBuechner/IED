include "bedrock.ch"

namespace Ledger
{
	// Reads an amount such as "12.34", "-0.50" or "7" as cents; false for anything else.
	bool ParseCents(Core::StringView text, int64& cents);
	// Writes cents as an amount with two decimals: -1234 is "-12.34".
	void AppendMoney(Core::StringBuilder& out, int64 cents);
}
