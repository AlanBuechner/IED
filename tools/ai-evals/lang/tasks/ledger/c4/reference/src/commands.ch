include "transaction.ch"

namespace Ledger
{
	// The transactions of `account` (every one when empty) in `month` (YYYY-MM; every one when empty).
	Core::Array<Transaction> Filter(const Core::Array<Transaction>& rows, Core::StringView account, Core::StringView month);
	// Each account's balance, by account name: "checking 1234.56".
	Core::String Balance(const Core::Array<Transaction>& rows);
	// The transactions in file order: "date account category amount memo".
	Core::String List(const Core::Array<Transaction>& rows);
	// Spending per category, the most first: "groceries 218.75 6.4%", then "total 3428.05"; "no spending" when none.
	Core::String Report(const Core::Array<Transaction>& rows);
	// A share of `whole` in tenths of a percent, rounded half up: 1 of 2000 is 1 (0.1%).
	int64 PercentTenths(int64 part, int64 whole);
}
