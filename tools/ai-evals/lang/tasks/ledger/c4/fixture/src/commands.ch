include "transaction.ch"

namespace Ledger
{
	// Each account's balance, by account name: "checking 1234.56".
	Core::String Balance(const Core::Array<Transaction>& rows);
	// The transactions in file order, those of `account` only when it is not empty: "date account category amount memo".
	Core::String List(const Core::Array<Transaction>& rows, Core::StringView account);
}
