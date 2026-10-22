include "bedrock.ch"

namespace Ledger
{
	struct Transaction
	{
		public Core::String date;       // YYYY-MM-DD
		public Core::String account;
		public Core::String category;
		public int64 cents;             // negative for spending
		public Core::String memo;
	}

	struct LoadResult
	{
		public Core::Array<Transaction> rows;
		public Core::String error;      // empty when the file read whole
	}

	// One CSV line "date,account,category,amount,memo"; false with `why` when it is not one.
	bool ParseTransaction(Core::StringView line, Transaction& row, Core::String& why);
	// Every transaction of the CSV text, whose first line is the header.
	LoadResult ParseLedger(Core::StringView text);
}
