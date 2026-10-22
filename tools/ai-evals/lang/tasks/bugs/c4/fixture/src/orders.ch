include "orderline.ch"

namespace Invoice
{
	struct Order
	{
		public Core::String id;
		public Core::String customer;
		public Tier tier;
		public Core::Array<OrderLine> lines;
	}

	// The lines grouped by order id, the orders in the order they first appear.
	Core::Array<Order> GroupOrders(const Core::Array<OrderLine>& lines);
	// One "<order> <customer> <total>" line per order, then "orders <n> total <sum>" and "average <sum / n>".
	Core::String Report(const Core::Array<Order>& orders);
}
