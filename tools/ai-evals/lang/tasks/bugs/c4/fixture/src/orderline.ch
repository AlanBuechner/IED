include "bedrock.ch"

namespace Invoice
{
	const int64 kCentsPerUnit = 100;

	enum class Tier { Standard, Gold }

	struct OrderLine
	{
		public Core::String order;
		public Core::String customer;
		public Tier tier;
		public Core::String sku;
		public int64 quantity;
		public int64 unitCents;
	}

	struct ParseResult
	{
		public Core::Array<OrderLine> lines;
		public Core::String error;   // empty when every line read
	}

	// One CSV line "order,customer,tier,sku,quantity,unit_price"; false with `why` when it is not one.
	bool ParseLine(Core::StringView text, OrderLine& line, Core::String& why);
	// Every order line of the CSV text, whose first line is the header.
	ParseResult ParseOrders(Core::StringView text);
}
