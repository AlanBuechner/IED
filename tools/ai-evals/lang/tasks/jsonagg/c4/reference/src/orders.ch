include "bedrock.ch"

namespace JsonAgg
{
	enum class Status { Paid, Cancelled, Refunded }

	// One line of an order; its price in whole cents.
	struct Item
	{
		public Core::String sku;
		public int64 qty;
		public int64 cents;
	}

	struct Order
	{
		public Core::String customer;
		public Status status;
		public Core::Array<Item> items;
	}

	// The orders of a JSON document {"orders": [...]}; false when the text is not JSON of that shape.
	bool ParseOrders(Core::StringView text, Core::Array<Order>& orders);
}
