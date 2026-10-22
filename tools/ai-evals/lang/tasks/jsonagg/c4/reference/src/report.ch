include "orders.ch"

namespace JsonAgg
{
	const int64 kTopSkuCount = 5;

	struct SkuUnits
	{
		public Core::String sku;
		public int64 units;
	}

	// One customer's orders; money and units come from its paid orders only.
	struct CustomerTotals
	{
		public int64 orders;
		public int64 paidOrders;
		public int64 spentCents;
		public Core::HashMap<Core::String, int64> units;   // by sku
	}

	struct OrderTotals
	{
		public int64 orders;
		public int64 paid;
		public int64 cancelled;
		public int64 refunded;
		public int64 revenueCents;
	}

	struct Report
	{
		public Core::HashMap<Core::String, CustomerTotals> customers;
		public OrderTotals totals;
		public Core::Array<SkuUnits> topSkus;
	}

	Report Aggregate(const Core::Array<Order>& orders);
	// The `limit` skus with the most units, most first, equal units by sku.
	Core::Array<SkuUnits> TopSkus(const Core::HashMap<Core::String, int64>& units, int64 limit);
	// The sku with the most units, ties to the smallest; null when there are none. Valid while `units` is unchanged.
	const Core::String* FavoriteSku(const Core::HashMap<Core::String, int64>& units);
}
